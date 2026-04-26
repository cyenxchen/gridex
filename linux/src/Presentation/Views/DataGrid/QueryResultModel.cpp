#include "Presentation/Views/DataGrid/QueryResultModel.h"

#include <QColor>
#include <QFont>

namespace gridex {

// ---- helpers ----

RowValue QueryResultModel::effectiveCellValue(int row, int col) const {
    auto it = pendingEdits_.find({row, col});
    if (it != pendingEdits_.end()) return it->second;

    if (isInsertedRow(row)) {
        const int insertedIdx = row - static_cast<int>(result_.rows.size());
        if (insertedIdx < static_cast<int>(insertedRows_.size()) &&
            col < static_cast<int>(insertedRows_[insertedIdx].size())) {
            return insertedRows_[insertedIdx][col];
        }
        return RowValue::makeNull();
    }

    if (row < static_cast<int>(result_.rows.size()) &&
        col < static_cast<int>(result_.rows[row].size())) {
        return result_.rows[row][col];
    }
    return RowValue::makeNull();
}

void QueryResultModel::addEmptyRow() {
    const int cols = columnCount();
    std::vector<RowValue> emptyRow(cols, RowValue::makeNull());
    const int newRowIdx = rowCount();
    beginInsertRows({}, newRowIdx, newRowIdx);
    insertedRows_.push_back(std::move(emptyRow));
    endInsertRows();
    emit pendingChangesChanged();
}

void QueryResultModel::markRowDeleted(int row) {
    if (row < 0 || row >= rowCount()) return;
    if (isInsertedRow(row)) {
        // Cancelling an inserted row: remove it directly.
        const int insertedIdx = row - static_cast<int>(result_.rows.size());
        beginRemoveRows({}, row, row);
        insertedRows_.erase(insertedRows_.begin() + insertedIdx);
        endRemoveRows();
        // Remove any pending edits for that inserted row and shift higher-row keys.
        std::map<CellKey, RowValue> adjusted;
        for (auto& [k, v] : pendingEdits_) {
            if (k.first < row) {
                adjusted[k] = v;
            } else if (k.first > row) {
                adjusted[{k.first - 1, k.second}] = v;
            }
            // k.first == row: drop it
        }
        pendingEdits_ = std::move(adjusted);
    } else {
        if (deletedRows_.count(row)) {
            // Toggle: undelete
            deletedRows_.erase(row);
        } else {
            deletedRows_.insert(row);
        }
        // Repaint the row
        if (columnCount() > 0) {
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        }
    }
    emit pendingChangesChanged();
}

// ---- QAbstractTableModel overrides ----

Qt::ItemFlags QueryResultModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    // Deleted rows remain selectable but not editable
    if (isRowDeleted(index.row())) {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }
    return Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QVariant QueryResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int r = index.row();
    const int c = index.column();
    if (r < 0 || r >= rowCount() || c < 0 || c >= columnCount()) return {};

    const bool hasPendingEdit = pendingEdits_.count({r, c}) > 0;
    const bool isDeleted      = isRowDeleted(r);
    const bool isNew          = isInsertedRow(r);
    const RowValue cell       = effectiveCellValue(r, c);

    switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return QString::fromUtf8(cell.displayString().c_str());

        case Qt::ToolTipRole: {
            const auto full = cell.tryStringValue().value_or("NULL");
            if (full.size() > 500) return QString::fromUtf8(full.substr(0, 500).c_str()) + QStringLiteral("…");
            return QString::fromUtf8(full.c_str());
        }

        case Qt::BackgroundRole:
            if (isDeleted)      return QColor(255, 200, 200);   // red tint for deletions
            if (isNew)          return QColor(200, 255, 200);   // green tint for new rows
            if (hasPendingEdit) return QColor(255, 255, 200);   // yellow for modified cells
            return {};

        case Qt::ForegroundRole:
            if (isDeleted) return QColor(150, 50, 50);
            if (cell.isNull()) return QColor(150, 150, 150);
            return {};

        case Qt::FontRole: {
            QFont f;
            if (isDeleted) {
                f.setStrikeOut(true);
                return f;
            }
            if (cell.isNull() && !hasPendingEdit) {
                f.setItalic(true);
                return f;
            }
            return {};
        }

        case Qt::TextAlignmentRole:
            if (cell.isNumeric()) return int(Qt::AlignVCenter | Qt::AlignRight);
            return int(Qt::AlignVCenter | Qt::AlignLeft);

        default:
            return {};
    }
}

bool QueryResultModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole) return false;
    if (!index.isValid()) return false;
    const int r = index.row();
    const int c = index.column();
    if (r < 0 || r >= rowCount() || c < 0 || c >= columnCount()) return false;
    if (isRowDeleted(r)) return false;

    const QString str = value.toString();
    RowValue newVal = str.isEmpty() ? RowValue::makeNull()
                                    : RowValue::makeString(str.toStdString());

    if (isInsertedRow(r)) {
        // Write directly into the inserted row buffer.
        const int insertedIdx = r - static_cast<int>(result_.rows.size());
        if (insertedIdx < static_cast<int>(insertedRows_.size()) &&
            c < static_cast<int>(insertedRows_[insertedIdx].size())) {
            insertedRows_[insertedIdx][c] = std::move(newVal);
            emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole});
            emit pendingChangesChanged();
            return true;
        }
        return false;
    }

    pendingEdits_[{r, c}] = std::move(newVal);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole, Qt::ForegroundRole, Qt::FontRole});
    emit pendingChangesChanged();
    return true;
}

QVariant QueryResultModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            if (section < 0 || section >= columnCount()) return {};
            return QString::fromUtf8(result_.columns[section].name.c_str());
        }
        if (orientation == Qt::Vertical) {
            return section + 1;
        }
    }
    if (role == Qt::ToolTipRole && orientation == Qt::Horizontal) {
        if (section < 0 || section >= columnCount()) return {};
        const auto& h = result_.columns[section];
        return QString::fromUtf8((h.name + " : " + h.dataType).c_str());
    }
    return {};
}

void QueryResultModel::sort(int column, Qt::SortOrder order) {
    if (column < 0 || column >= columnCount()) return;
    if (result_.rows.empty()) return;
    // Don't sort if there are pending changes — too complex to remap indices.
    if (hasPendingChanges()) return;

    const auto col = static_cast<std::size_t>(column);
    beginResetModel();
    std::stable_sort(result_.rows.begin(), result_.rows.end(),
        [col, order](const std::vector<RowValue>& a, const std::vector<RowValue>& b) {
            if (col >= a.size() || col >= b.size()) return false;
            const auto& va = a[col];
            const auto& vb = b[col];
            // NULLs always sort last.
            if (va.isNull() && vb.isNull()) return false;
            if (va.isNull()) return order == Qt::DescendingOrder;
            if (vb.isNull()) return order == Qt::AscendingOrder;
            // Numeric comparison when both are numeric.
            if (va.isNumeric() && vb.isNumeric()) {
                const double da = va.tryDoubleValue().value_or(0);
                const double db = vb.tryDoubleValue().value_or(0);
                return order == Qt::AscendingOrder ? da < db : da > db;
            }
            // Fall back to string comparison.
            const auto sa = va.displayString();
            const auto sb = vb.displayString();
            return order == Qt::AscendingOrder ? sa < sb : sa > sb;
        });
    endResetModel();
}

}
