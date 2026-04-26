#pragma once

#include <QAbstractTableModel>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "Core/Models/Database/RowValue.h"
#include "Core/Models/Query/QueryResult.h"

namespace gridex {

// Editable table model wrapping a QueryResult. Rows are materialized up-front
// (bounded by adapter fetchRows limit). Cell text comes from RowValue::displayString.
//
// Pending changes:
//   pendingEdits_   — {row, col} → new RowValue for modified cells
//   insertedRows_   — new rows appended via addEmptyRow(); row index = result_.rows.size() + i
//   deletedRows_    — set of row indices (original rows) marked for deletion
class QueryResultModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit QueryResultModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setResult(QueryResult result) {
        beginResetModel();
        result_ = std::move(result);
        pendingEdits_.clear();
        insertedRows_.clear();
        deletedRows_.clear();
        endResetModel();
    }

    void clear() {
        beginResetModel();
        result_ = {};
        pendingEdits_.clear();
        insertedRows_.clear();
        deletedRows_.clear();
        endResetModel();
    }

    [[nodiscard]] const QueryResult& result() const noexcept { return result_; }

    // ---- Pending changes interface ----
    [[nodiscard]] bool hasPendingChanges() const noexcept {
        return !pendingEdits_.empty() || !insertedRows_.empty() || !deletedRows_.empty();
    }

    using CellKey = std::pair<int, int>;
    [[nodiscard]] const std::map<CellKey, RowValue>& pendingEdits() const noexcept { return pendingEdits_; }
    [[nodiscard]] const std::vector<std::vector<RowValue>>& insertedRows() const noexcept { return insertedRows_; }
    [[nodiscard]] const std::set<int>& deletedRows() const noexcept { return deletedRows_; }

    void clearPendingChanges() {
        pendingEdits_.clear();
        insertedRows_.clear();
        deletedRows_.clear();
        // Repaint entire view
        if (rowCount() > 0 && columnCount() > 0) {
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
        }
    }

    // Returns the effective RowValue for a cell (pending edit or original).
    [[nodiscard]] RowValue effectiveCellValue(int row, int col) const;

    // Adds an empty row (all NULLs) at the bottom of the model.
    void addEmptyRow();

    // Marks a row as pending deletion.
    void markRowDeleted(int row);
    [[nodiscard]] bool isRowDeleted(int row) const noexcept { return deletedRows_.count(row) > 0; }

    // Returns true if row is an inserted (not yet committed) row.
    [[nodiscard]] bool isInsertedRow(int row) const noexcept {
        return row >= static_cast<int>(result_.rows.size());
    }

    // ---- QAbstractTableModel overrides ----
    int rowCount(const QModelIndex& parent = {}) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(result_.rows.size()) + static_cast<int>(insertedRows_.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(result_.columns.size());
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

signals:
    void pendingChangesChanged();

private:
    QueryResult result_;
    std::map<CellKey, RowValue>          pendingEdits_;
    std::vector<std::vector<RowValue>>   insertedRows_;
    std::set<int>                        deletedRows_;
};

}
