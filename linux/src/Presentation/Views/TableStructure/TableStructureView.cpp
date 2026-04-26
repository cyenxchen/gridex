#include "Presentation/Views/TableStructure/TableStructureView.h"

#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace gridex {

namespace {

QStandardItem* cell(const QString& text) {
    auto* item = new QStandardItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    return item;
}

QStandardItem* boolCell(bool value) {
    return cell(value ? QStringLiteral("✓") : QString{});
}

QString fromStdStr(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QString optStr(const std::optional<std::string>& opt) {
    return opt ? fromStdStr(*opt) : QString{};
}

QString joinVec(const std::vector<std::string>& v) {
    QString out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += QStringLiteral(", ");
        out += fromStdStr(v[i]);
    }
    return out;
}

// Section header label matching macOS InlineStructureView divider style.
QLabel* makeSectionHeader(const QString& title, QWidget* parent) {
    auto* lbl = new QLabel(title, parent);
    return lbl;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TableStructureView::TableStructureView(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void TableStructureView::buildUi() {
    auto* outerV = new QVBoxLayout(this);
    outerV->setContentsMargins(0, 0, 0, 0);
    outerV->setSpacing(0);

    // Wrap everything in a QScrollArea so the single-page layout scrolls.
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* container = new QWidget(scrollArea_);
    auto* vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 8);
    vbox->setSpacing(0);

    // ---- Columns section ----
    vbox->addWidget(makeSectionHeader(tr("Columns"), container));

    auto* colDiv = new QFrame(container);
    colDiv->setFrameShape(QFrame::HLine);
    vbox->addWidget(colDiv);

    colModel_ = new QStandardItemModel(0, 7, this);
    colModel_->setHorizontalHeaderLabels({
        tr("Name"), tr("Type"), tr("Nullable"),
        tr("Default"), tr("PK"), tr("Auto-Increment"), tr("Comment")
    });
    colView_ = makeTableView(container);
    colView_->setModel(colModel_);
    vbox->addWidget(colView_);

    // ---- Indexes section ----
    vbox->addSpacing(6);
    vbox->addWidget(makeSectionHeader(tr("Indexes"), container));

    auto* idxDiv = new QFrame(container);
    idxDiv->setFrameShape(QFrame::HLine);
    vbox->addWidget(idxDiv);

    idxModel_ = new QStandardItemModel(0, 4, this);
    idxModel_->setHorizontalHeaderLabels({
        tr("Name"), tr("Columns"), tr("Unique"), tr("Type")
    });
    idxView_ = makeTableView(container);
    idxView_->setModel(idxModel_);
    vbox->addWidget(idxView_);

    // ---- Foreign Keys section (hidden when empty) ----
    fkSection_ = new QWidget(container);
    auto* fkV = new QVBoxLayout(fkSection_);
    fkV->setContentsMargins(0, 0, 0, 0);
    fkV->setSpacing(0);

    fkV->addSpacing(6);
    fkV->addWidget(makeSectionHeader(tr("Foreign Keys"), fkSection_));

    auto* fkDiv = new QFrame(fkSection_);
    fkDiv->setFrameShape(QFrame::HLine);
    fkV->addWidget(fkDiv);

    fkModel_ = new QStandardItemModel(0, 6, this);
    fkModel_->setHorizontalHeaderLabels({
        tr("Name"), tr("Column"),
        tr("Referenced Table"), tr("Referenced Column"),
        tr("On Delete"), tr("On Update")
    });
    fkView_ = makeTableView(fkSection_);
    fkView_->setModel(fkModel_);
    fkV->addWidget(fkView_);

    fkSection_->setVisible(false);
    vbox->addWidget(fkSection_);

    vbox->addStretch();

    scrollArea_->setWidget(container);
    outerV->addWidget(scrollArea_);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TableStructureView::loadStructure(const TableDescription& desc) {
    populateColumns(desc.columns);
    populateIndexes(desc.indexes);
    populateForeignKeys(desc.foreignKeys);
}

void TableStructureView::clear() {
    colModel_->removeRows(0, colModel_->rowCount());
    idxModel_->removeRows(0, idxModel_->rowCount());
    fkModel_->removeRows(0, fkModel_->rowCount());
    if (fkSection_) fkSection_->setVisible(false);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void TableStructureView::populateColumns(const std::vector<ColumnInfo>& columns) {
    colModel_->removeRows(0, colModel_->rowCount());
    colModel_->setRowCount(static_cast<int>(columns.size()));
    for (int row = 0; row < static_cast<int>(columns.size()); ++row) {
        const auto& c = columns[row];
        colModel_->setItem(row, 0, cell(fromStdStr(c.name)));
        colModel_->setItem(row, 1, cell(fromStdStr(c.dataType)));
        colModel_->setItem(row, 2, boolCell(c.isNullable));
        colModel_->setItem(row, 3, cell(optStr(c.defaultValue)));
        colModel_->setItem(row, 4, boolCell(c.isPrimaryKey));
        colModel_->setItem(row, 5, boolCell(c.isAutoIncrement));
        colModel_->setItem(row, 6, cell(optStr(c.comment)));
    }
    colView_->resizeColumnsToContents();
    colView_->horizontalHeader()->setStretchLastSection(true);
    // Adjust table height to content so scroll area calculates correctly.
    const int rowH = colView_->verticalHeader()->defaultSectionSize();
    const int hdrH = colView_->horizontalHeader()->height();
    const int rows = colModel_->rowCount();
    colView_->setFixedHeight(hdrH + rowH * std::max(rows, 1) + 4);
}

void TableStructureView::populateIndexes(const std::vector<IndexInfo>& indexes) {
    idxModel_->removeRows(0, idxModel_->rowCount());
    idxModel_->setRowCount(static_cast<int>(indexes.size()));
    for (int row = 0; row < static_cast<int>(indexes.size()); ++row) {
        const auto& idx = indexes[row];
        idxModel_->setItem(row, 0, cell(fromStdStr(idx.name)));
        idxModel_->setItem(row, 1, cell(joinVec(idx.columns)));
        idxModel_->setItem(row, 2, boolCell(idx.isUnique));
        idxModel_->setItem(row, 3, cell(optStr(idx.type)));
    }
    idxView_->resizeColumnsToContents();
    idxView_->horizontalHeader()->setStretchLastSection(true);
    const int rowH = idxView_->verticalHeader()->defaultSectionSize();
    const int hdrH = idxView_->horizontalHeader()->height();
    const int rows = idxModel_->rowCount();
    idxView_->setFixedHeight(hdrH + rowH * std::max(rows, 1) + 4);
}

void TableStructureView::populateForeignKeys(const std::vector<ForeignKeyInfo>& fks) {
    fkModel_->removeRows(0, fkModel_->rowCount());
    fkModel_->setRowCount(static_cast<int>(fks.size()));
    for (int row = 0; row < static_cast<int>(fks.size()); ++row) {
        const auto& fk = fks[row];
        fkModel_->setItem(row, 0, cell(optStr(fk.name)));
        fkModel_->setItem(row, 1, cell(joinVec(fk.columns)));
        fkModel_->setItem(row, 2, cell(fromStdStr(fk.referencedTable)));
        fkModel_->setItem(row, 3, cell(joinVec(fk.referencedColumns)));
        fkModel_->setItem(row, 4, cell(QString::fromUtf8(rawValue(fk.onDelete).data(),
                                                          static_cast<qsizetype>(rawValue(fk.onDelete).size()))));
        fkModel_->setItem(row, 5, cell(QString::fromUtf8(rawValue(fk.onUpdate).data(),
                                                          static_cast<qsizetype>(rawValue(fk.onUpdate).size()))));
    }
    fkView_->resizeColumnsToContents();
    fkView_->horizontalHeader()->setStretchLastSection(true);

    const bool hasFk = !fks.empty();
    fkSection_->setVisible(hasFk);
    if (hasFk) {
        const int rowH = fkView_->verticalHeader()->defaultSectionSize();
        const int hdrH = fkView_->horizontalHeader()->height();
        fkView_->setFixedHeight(hdrH + rowH * static_cast<int>(fks.size()) + 4);
    }
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------

QTableView* TableStructureView::makeTableView(QWidget* parent) {
    auto* view = new QTableView(parent);
    view->setFrameShape(QFrame::NoFrame);
    view->setAlternatingRowColors(true);
    view->setShowGrid(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->setDefaultSectionSize(30);
    view->verticalHeader()->hide();
    // Disable internal scroll so the outer QScrollArea drives scrolling.
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return view;
}

} // namespace gridex
