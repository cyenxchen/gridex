#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace gridex {

class IDatabaseAdapter;

// Dialog for creating a new database table.
// Mirrors macOS CreateTableView: table name + columns editor with
// Name, Type, Nullable, Default, Primary Key columns.
// Emits tableCreated(tableName) on success.
class CreateTableDialog : public QDialog {
    Q_OBJECT

public:
    explicit CreateTableDialog(IDatabaseAdapter* adapter,
                               const QString& schema,
                               QWidget* parent = nullptr);

signals:
    void tableCreated(const QString& tableName);

private slots:
    void onAddColumn();
    void onRemoveColumn();
    void onCreate();

private:
    void buildUi();
    QString buildCreateSql() const;

    IDatabaseAdapter* adapter_;
    QString           schema_;

    QLineEdit*    tableNameEdit_  = nullptr;
    QTableWidget* columnsTable_   = nullptr;
    QPushButton*  addColBtn_      = nullptr;
    QPushButton*  removeColBtn_   = nullptr;
    QPushButton*  createBtn_      = nullptr;
    QPushButton*  cancelBtn_      = nullptr;

    // Column table columns indices
    static constexpr int kColName       = 0;
    static constexpr int kColType       = 1;
    static constexpr int kColNullable   = 2;
    static constexpr int kColDefault    = 3;
    static constexpr int kColPrimaryKey = 4;
};

} // namespace gridex
