#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;

namespace gridex {

class IDatabaseAdapter;

// Dialog to list and switch databases on the current connection.
// Mirrors macOS DatabaseSwitcherDialog.
// Emits databaseSelected(dbName) when the user confirms a choice.
class DatabaseSwitcherDialog : public QDialog {
    Q_OBJECT

public:
    explicit DatabaseSwitcherDialog(IDatabaseAdapter* adapter,
                                    const QString& currentDatabase,
                                    QWidget* parent = nullptr);

signals:
    void databaseSelected(const QString& dbName);

private slots:
    void onSearchChanged(const QString& text);
    void onAccepted();
    void onItemDoubleClicked();
    void onCreateClicked();

private:
    void buildUi();
    void populate();

    IDatabaseAdapter* adapter_;
    QString           currentDatabase_;

    QLineEdit*   searchEdit_  = nullptr;
    QListWidget* listWidget_  = nullptr;
    QPushButton* openBtn_     = nullptr;
};

} // namespace gridex
