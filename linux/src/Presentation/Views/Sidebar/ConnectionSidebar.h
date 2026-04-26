#pragma once

#include <memory>
#include <QWidget>
#include <string>

class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace gridex {

class AppDatabase;
class ConnectionListViewModel;

class ConnectionSidebar : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionSidebar(ConnectionListViewModel* viewModel,
                               std::shared_ptr<AppDatabase> appDb,
                               QWidget* parent = nullptr);

signals:
    void addConnectionRequested();
    void newGroupRequested();
    void editConnectionRequested(const QString& connectionId);
    void removeConnectionRequested(const QString& connectionId);
    void connectionSelected(const QString& connectionId);

private slots:
    void onViewModelChanged();
    void onContextMenuRequested(const QPoint& pos);
    void onSearchChanged(const QString& text);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void buildUi();
    QString idOfItem(QTreeWidgetItem* item) const;

    ConnectionListViewModel* viewModel_;
    std::shared_ptr<AppDatabase> appDb_;

    QLineEdit*      searchEdit_ = nullptr;
    QStackedWidget* body_       = nullptr;
    QTreeWidget*    tree_       = nullptr;
};

}
