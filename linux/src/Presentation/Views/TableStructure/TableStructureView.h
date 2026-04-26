#pragma once

#include <QWidget>

#include "Core/Models/Schema/SchemaSnapshot.h"

class QScrollArea;
class QTableView;
class QStandardItemModel;

namespace gridex {

// Read-only structure viewer: single scrollable page with Columns + Indexes + Foreign Keys
// sections stacked vertically — matches macOS InlineStructureView (no separate tabs).
// Call loadStructure() whenever the active table changes; call clear() when no table selected.
class TableStructureView : public QWidget {
    Q_OBJECT

public:
    explicit TableStructureView(QWidget* parent = nullptr);

    void loadStructure(const TableDescription& desc);
    void clear();

private:
    void buildUi();
    void populateColumns(const std::vector<ColumnInfo>& columns);
    void populateIndexes(const std::vector<IndexInfo>& indexes);
    void populateForeignKeys(const std::vector<ForeignKeyInfo>& fks);

    static QTableView* makeTableView(QWidget* parent);

    QScrollArea* scrollArea_ = nullptr;

    // Columns section
    QTableView*        colView_   = nullptr;
    QStandardItemModel* colModel_ = nullptr;

    // Indexes section
    QTableView*        idxView_   = nullptr;
    QStandardItemModel* idxModel_ = nullptr;

    // Foreign Keys section
    QWidget*           fkSection_ = nullptr;
    QTableView*        fkView_    = nullptr;
    QStandardItemModel* fkModel_  = nullptr;
};

}
