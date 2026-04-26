#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QListView;
class QMenu;
class QPushButton;
class QStandardItemModel;

namespace gridex {

class WorkspaceState;

// Grid-view alternative to the tree sidebar: shows tables for the selected
// schema as icon tiles. Double-click emits tableSelected, right-click shows
// context menu matching the sidebar's table actions.
class TableGridView : public QWidget {
    Q_OBJECT

public:
    explicit TableGridView(WorkspaceState* state, QWidget* parent = nullptr);

    void reload();
    void onSearchChanged(const QString& text);

signals:
    void tableSelected(const QString& schema, const QString& table);
    void tableDeleted(const QString& schema, const QString& table);

private slots:
    void onSchemaChanged(int index);
    void onItemDoubleClicked(const QModelIndex& index);
    void onContextMenuRequested(const QPoint& pos);
    void onConnectionOpened();
    void onConnectionClosed();

private:
    void buildUi();
    void loadSchemas();
    void loadTables(const QString& schema);
    void applyFilter(const QString& text);
    void buildContextMenu(QMenu* menu, const QString& schema, const QString& table);

    WorkspaceState*     state_    = nullptr;
    QComboBox*          schemaCb_ = nullptr;
    QLineEdit*          searchEd_ = nullptr;
    QPushButton*        refreshBtn_ = nullptr;
    QListView*          listView_ = nullptr;
    QStandardItemModel* model_    = nullptr;

    QStringList allTableNames_;
    QString     activeSchema_;
};

}
