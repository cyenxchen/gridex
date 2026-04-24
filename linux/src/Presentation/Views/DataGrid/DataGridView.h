#pragma once

#include <QStringList>
#include <QWidget>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Core/Models/Query/FilterExpression.h"
#include "Core/Models/Schema/SchemaSnapshot.h"

class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableView;

namespace gridex {

class FilterBarView;
class IDatabaseAdapter;
class QueryLogPanel;
class QueryResultModel;
class TableStructureView;

// Editable table data viewer:
//   Top 28px DataGridToolbar: [pending label] [Discard] [Commit] .... [Reload]
//   FilterBar (hidden by default, toggled by Filters button)
//   Center: QStackedWidget — page 0: QTableView (data), page 1: TableStructureView
//   QueryLogPanel (collapsible, logs executed queries with timestamps)
//   Bottom 34px BottomTabBar: [Data|Structure] [+Row] [X-Y of Z rows] [Filters] [<][>] [Rows spin]
class DataGridView : public QWidget {
    Q_OBJECT

public:
    explicit DataGridView(QWidget* parent = nullptr);

    void setAdapter(IDatabaseAdapter* adapter);

public slots:
    void loadTable(const QString& schema, const QString& table);
    void reload();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

signals:
    void loadFailed(const QString& message);
    void rowCountChanged(int rowsShown, int durationMs);
    void rowSelected(const std::vector<std::pair<std::string, std::string>>& fields);

private slots:
    void onRowSelectionChanged();
    void onPrevPage();
    void onNextPage();
    void onFiltersToggled();
    void onModeClicked(int mode);  // 0 = Data, 1 = Structure
    void onExportCsv();
    void onExportJson();
    void onExportSql();

    // Editing actions
    void onAddRow();
    void onCommit();
    void onDiscard();
    void onTableContextMenu(const QPoint& pos);
    void onCopyCell();
    void onCopyRow();
    void onCopyAsInsert();
    void onDeleteRows();
    void onPendingChangesChanged();

private:
    void buildUi();
    void fetchAndRender();
    void loadStructure();
    void setPagingLabel(int rowsShown, int durationMs);
    void fitColumnsToViewport();
    void updatePendingLabel();

    // PK detection: called once per loadTable, cached in pkColumns_
    void cachePrimaryKeys();

    // Returns SQL statements for all pending changes without executing them.
    QStringList buildPendingStatements() const;

    IDatabaseAdapter* adapter_ = nullptr;

    // Current table state
    std::optional<std::string> currentSchema_;
    std::string currentTable_;
    int offset_          = 0;
    int totalRowCount_   = -1;  // -1 = unknown

    // Cached primary key column names for the current table
    std::vector<std::string> pkColumns_;

    // UI — top toolbar
    QLabel*      pendingStub_   = nullptr;
    QPushButton* discardBtn_    = nullptr;
    QPushButton* commitBtn_     = nullptr;
    QPushButton* reloadBtn_     = nullptr;

    // Filter bar (hidden until Filters toggled)
    FilterBarView* filterBar_     = nullptr;
    QFrame*        filterBarDiv_  = nullptr;  // separator below filterBar_

    // Active filter (nullopt = no filter applied)
    std::optional<FilterExpression> activeFilter_;

    // Center: stacked pages — index 0 = data, index 1 = structure
    QStackedWidget*     centerStack_    = nullptr;
    QTableView*         tableView_      = nullptr;
    QueryResultModel*   model_          = nullptr;
    TableStructureView* structureView_  = nullptr;

    // SQL log panel (collapsible, below center stack)
    QueryLogPanel* logPanel_ = nullptr;

    // Bottom bar
    QPushButton* dataTabBtn_   = nullptr;
    QPushButton* structTabBtn_ = nullptr;
    QPushButton* addRowBtn_    = nullptr;
    QLabel*      pageLabel_    = nullptr;
    QPushButton* filtersBtn_   = nullptr;
    QPushButton* exportBtn_    = nullptr;
    QMenu*       exportMenu_   = nullptr;
    QPushButton* prevBtn_      = nullptr;
    QPushButton* nextBtn_      = nullptr;
    QSpinBox*    limitSpin_    = nullptr;

    // Context menu for tableView_
    QMenu* tableContextMenu_   = nullptr;
};

}
