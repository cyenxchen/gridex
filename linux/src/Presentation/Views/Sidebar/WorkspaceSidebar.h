#pragma once

#include <memory>
#include <QWidget>
#include <string>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QStackedWidget;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

namespace gridex { class TableGridView; }

namespace gridex {

class AppDatabase;
class SavedQueryRepository;
class WorkspaceState;

// Left sidebar shown inside WorkspaceView once a connection is open.
// Mirrors macOS SidebarView layout:
//   [tab bar: Items | Queries | History]
//   [search bar]
//   [tree: schemas (expand -> tables lazy loaded)]
//   [bottom bar: schema selector ▾ | DB info | disconnect]
class WorkspaceSidebar : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceSidebar(WorkspaceState* state,
                              std::shared_ptr<AppDatabase> appDb,
                              QWidget* parent = nullptr);
    ~WorkspaceSidebar();

    void refreshTree();
    void logQuery(const QString& sql, int rowCount, int elapsedMs);
    void promptSaveQuery(const QString& sql);

signals:
    void tableSelected(const QString& schema, const QString& table);
    void tableDeleted(const QString& schema, const QString& table);
    void newTableRequested(const QString& schema);
    void disconnectRequested();
    void loadSavedQueryRequested(const QString& sql);
    void functionSelected(const QString& schema, const QString& name);
    void procedureSelected(const QString& schema, const QString& name);

private slots:
    void onConnectionOpened();
    void onConnectionClosed();
    void onItemExpanded(const QModelIndex& index);
    void onItemDoubleClicked(const QModelIndex& index);
    void onTabClicked(int tab);
    void onSearchChanged(const QString& text);
    void onSchemaChanged(int index);
    void onContextMenuRequested(const QPoint& pos);
    void onSavedQueryContextMenu(const QPoint& pos);

private:
    void buildUi();
    void loadSchemas();
    void loadTablesForSchema(QStandardItem* schemaItem, const QString& schemaName);
    void loadFunctionsForSchema(QStandardItem* parent, const QString& schemaName);
    void loadProceduresForSchema(QStandardItem* parent, const QString& schemaName);
    void reloadActiveSchema();
    void loadHistoryFromDb();
    void reloadSavedQueriesTree();

    // Data import / backup actions (wired via context menu).
    void runSqlFile();
    void importCsv(const QString& schema, const QString& table);
    void backupDatabase();
    void restoreDatabase();

    WorkspaceState* state_;
    std::shared_ptr<AppDatabase> appDb_;
    std::unique_ptr<SavedQueryRepository> savedQueryRepo_;

    // Tab bar (0=Items, 1=Queries, 2=History, 3=Saved)
    QPushButton* itemsTabBtn_   = nullptr;
    QPushButton* queriesTabBtn_ = nullptr;
    QPushButton* historyTabBtn_ = nullptr;
    QPushButton* savedTabBtn_   = nullptr;
    QStackedWidget* body_ = nullptr;
    int activeTab_ = 0;

    // Items tab
    QLineEdit*      searchEdit_     = nullptr;
    QStackedWidget* itemsViewStack_ = nullptr;   // 0=tree, 1=grid
    QPushButton*    gridToggleBtn_  = nullptr;
    QTreeView*      tree_           = nullptr;
    QStandardItemModel* model_      = nullptr;
    TableGridView*  tableGrid_      = nullptr;

    // Bottom bar
    QComboBox*   schemaCombo_    = nullptr;
    QLabel*      dbInfoLabel_    = nullptr;
    QPushButton* disconnectBtn_  = nullptr;
    QPushButton* newTableBtn_    = nullptr;

    // History tab
    QListWidget* historyList_    = nullptr;

    // Saved queries tab
    QTreeWidget* savedTree_      = nullptr;
};

}
