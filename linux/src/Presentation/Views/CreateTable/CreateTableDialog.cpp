#include "Presentation/Views/CreateTable/CreateTableDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SQLDialect.h"
#include "Core/Errors/GridexError.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

namespace {

QStringList sqlTypesForAdapter(IDatabaseAdapter* adapter) {
    if (!adapter) {
        return {"INTEGER", "TEXT", "REAL", "BLOB"};
    }
    switch (adapter->databaseType()) {
        case DatabaseType::PostgreSQL:
            return {"INTEGER", "BIGINT", "SMALLINT", "SERIAL", "BIGSERIAL",
                    "TEXT", "VARCHAR(255)", "CHAR(1)",
                    "BOOLEAN",
                    "FLOAT", "DOUBLE PRECISION", "DECIMAL", "NUMERIC",
                    "TIMESTAMP", "TIMESTAMPTZ", "DATE", "TIME",
                    "UUID", "JSON", "JSONB", "BYTEA"};
        case DatabaseType::MySQL:
            return {"INT", "BIGINT", "SMALLINT", "TINYINT",
                    "TEXT", "VARCHAR(255)", "CHAR(1)", "LONGTEXT",
                    "BOOLEAN", "TINYINT(1)",
                    "FLOAT", "DOUBLE", "DECIMAL",
                    "DATETIME", "TIMESTAMP", "DATE", "TIME",
                    "JSON", "BLOB", "LONGBLOB"};
        case DatabaseType::SQLite:
            return {"INTEGER", "TEXT", "REAL", "NUMERIC", "BLOB"};
        case DatabaseType::MSSQL:
            return {"INT", "BIGINT", "SMALLINT", "TINYINT",
                    "NVARCHAR(255)", "VARCHAR(255)", "NTEXT", "TEXT",
                    "BIT",
                    "FLOAT", "REAL", "DECIMAL", "NUMERIC",
                    "DATETIME", "DATETIME2", "DATE", "TIME",
                    "UNIQUEIDENTIFIER", "XML", "JSON", "VARBINARY(MAX)"};
        default:
            return {"INTEGER", "TEXT", "REAL", "BLOB"};
    }
}

} // namespace

CreateTableDialog::CreateTableDialog(IDatabaseAdapter* adapter,
                                     const QString& schema,
                                     QWidget* parent)
    : QDialog(parent)
    , adapter_(adapter)
    , schema_(schema)
{
    buildUi();
}

void CreateTableDialog::buildUi() {
    setWindowTitle(tr("Create Table"));
    setMinimumWidth(640);
    setMinimumHeight(420);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(16, 16, 16, 12);

    // Table name row
    auto* nameRow = new QWidget(this);
    auto* nameH = new QHBoxLayout(nameRow);
    nameH->setContentsMargins(0, 0, 0, 0);
    nameH->setSpacing(8);
    auto* nameLabel = new QLabel(tr("Table name:"), nameRow);
    tableNameEdit_ = new QLineEdit(nameRow);
    tableNameEdit_->setPlaceholderText(tr("new_table"));
    tableNameEdit_->setText(QStringLiteral("new_table"));
    connect(tableNameEdit_, &QLineEdit::textChanged, this,
            [this](const QString& t) { createBtn_->setEnabled(!t.trimmed().isEmpty()); });
    nameH->addWidget(nameLabel);
    nameH->addWidget(tableNameEdit_, 1);
    if (!schema_.isEmpty()) {
        auto* schemaLabel = new QLabel(tr("Schema: %1").arg(schema_), nameRow);
        nameH->addWidget(schemaLabel);
    }
    root->addWidget(nameRow);

    // Column header label
    auto* colHeader = new QLabel(tr("Columns"), this);
    root->addWidget(colHeader);

    // Columns table
    columnsTable_ = new QTableWidget(0, 5, this);
    columnsTable_->setHorizontalHeaderLabels(
        {tr("Name"), tr("Type"), tr("Nullable"), tr("Default"), tr("Primary Key")});
    columnsTable_->horizontalHeader()->setSectionResizeMode(kColName,       QHeaderView::Stretch);
    columnsTable_->horizontalHeader()->setSectionResizeMode(kColType,       QHeaderView::ResizeToContents);
    columnsTable_->horizontalHeader()->setSectionResizeMode(kColNullable,   QHeaderView::ResizeToContents);
    columnsTable_->horizontalHeader()->setSectionResizeMode(kColDefault,    QHeaderView::Stretch);
    columnsTable_->horizontalHeader()->setSectionResizeMode(kColPrimaryKey, QHeaderView::ResizeToContents);
    columnsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    columnsTable_->setAlternatingRowColors(true);
    root->addWidget(columnsTable_, 1);

    // Add a default "id" primary key column
    onAddColumn();
    // Set it up as PK with type appropriate for the adapter
    if (columnsTable_->rowCount() > 0) {
        auto* nameItem = columnsTable_->item(0, kColName);
        if (nameItem) nameItem->setText(QStringLiteral("id"));
        auto* typeCombo = qobject_cast<QComboBox*>(columnsTable_->cellWidget(0, kColType));
        if (typeCombo) {
            const DatabaseType dt = adapter_ ? adapter_->databaseType() : DatabaseType::SQLite;
            QString idType;
            switch (dt) {
                case DatabaseType::PostgreSQL: idType = QStringLiteral("SERIAL");  break;
                case DatabaseType::MySQL:      idType = QStringLiteral("INT");     break;
                case DatabaseType::MSSQL:      idType = QStringLiteral("INT");     break;
                default:                       idType = QStringLiteral("INTEGER"); break;
            }
            const int idx = typeCombo->findText(idType);
            if (idx >= 0) typeCombo->setCurrentIndex(idx);
        }
        auto* nullableCheck = qobject_cast<QCheckBox*>(columnsTable_->cellWidget(0, kColNullable));
        if (nullableCheck) nullableCheck->setChecked(false);
        auto* pkCheck = qobject_cast<QCheckBox*>(columnsTable_->cellWidget(0, kColPrimaryKey));
        if (pkCheck) pkCheck->setChecked(true);
    }

    // [+ Column] / [- Column] buttons
    auto* colBtnRow = new QWidget(this);
    auto* colBtnH = new QHBoxLayout(colBtnRow);
    colBtnH->setContentsMargins(0, 0, 0, 0);
    colBtnH->setSpacing(6);

    addColBtn_ = new QPushButton(tr("+ Column"), colBtnRow);
    connect(addColBtn_, &QPushButton::clicked, this, &CreateTableDialog::onAddColumn);

    removeColBtn_ = new QPushButton(tr("- Column"), colBtnRow);
    connect(removeColBtn_, &QPushButton::clicked, this, &CreateTableDialog::onRemoveColumn);

    colBtnH->addWidget(addColBtn_);
    colBtnH->addWidget(removeColBtn_);
    colBtnH->addStretch();
    root->addWidget(colBtnRow);

    // Divider
    auto* div = new QFrame(this);
    div->setFrameShape(QFrame::HLine);
    root->addWidget(div);

    // OK / Cancel row
    auto* btnRow = new QWidget(this);
    auto* btnH = new QHBoxLayout(btnRow);
    btnH->setContentsMargins(0, 0, 0, 0);

    cancelBtn_ = new QPushButton(tr("Cancel"), btnRow);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);

    createBtn_ = new QPushButton(tr("Create"), btnRow);
    createBtn_->setDefault(true);
    connect(createBtn_, &QPushButton::clicked, this, &CreateTableDialog::onCreate);

    btnH->addWidget(cancelBtn_);
    btnH->addStretch();
    btnH->addWidget(createBtn_);
    root->addWidget(btnRow);
}

void CreateTableDialog::onAddColumn() {
    const int row = columnsTable_->rowCount();
    columnsTable_->insertRow(row);

    // Name cell
    const QString defaultName = QStringLiteral("column_%1").arg(row + 1);
    columnsTable_->setItem(row, kColName, new QTableWidgetItem(defaultName));

    // Type combo
    auto* typeCombo = new QComboBox(columnsTable_);
    typeCombo->addItems(sqlTypesForAdapter(adapter_));
    columnsTable_->setCellWidget(row, kColType, typeCombo);

    // Nullable checkbox (centred)
    auto* nullableCheck = new QCheckBox(columnsTable_);
    nullableCheck->setChecked(true);
    auto* nullableWrapper = new QWidget(columnsTable_);
    auto* nullableLayout = new QHBoxLayout(nullableWrapper);
    nullableLayout->setContentsMargins(0, 0, 0, 0);
    nullableLayout->setAlignment(Qt::AlignCenter);
    nullableLayout->addWidget(nullableCheck);
    columnsTable_->setCellWidget(row, kColNullable, nullableWrapper);

    // Default cell
    columnsTable_->setItem(row, kColDefault, new QTableWidgetItem(QString{}));

    // PK checkbox (centred)
    auto* pkCheck = new QCheckBox(columnsTable_);
    pkCheck->setChecked(false);
    auto* pkWrapper = new QWidget(columnsTable_);
    auto* pkLayout = new QHBoxLayout(pkWrapper);
    pkLayout->setContentsMargins(0, 0, 0, 0);
    pkLayout->setAlignment(Qt::AlignCenter);
    pkLayout->addWidget(pkCheck);
    columnsTable_->setCellWidget(row, kColPrimaryKey, pkWrapper);

    columnsTable_->selectRow(row);
}

void CreateTableDialog::onRemoveColumn() {
    const int row = columnsTable_->currentRow();
    if (row >= 0) columnsTable_->removeRow(row);
}

QString CreateTableDialog::buildCreateSql() const {
    const QString tableName = tableNameEdit_->text().trimmed();
    if (tableName.isEmpty()) return {};

    const SQLDialect dialect = adapter_
        ? sqlDialect(adapter_->databaseType())
        : SQLDialect::SQLite;

    // Qualified table name
    QString qualifiedTable;
    if (!schema_.isEmpty() &&
        adapter_ &&
        adapter_->databaseType() != DatabaseType::SQLite &&
        adapter_->databaseType() != DatabaseType::MySQL)
    {
        qualifiedTable = QString::fromStdString(quoteIdentifier(dialect, schema_.toStdString()))
            + QStringLiteral(".")
            + QString::fromStdString(quoteIdentifier(dialect, tableName.toStdString()));
    } else {
        qualifiedTable = QString::fromStdString(quoteIdentifier(dialect, tableName.toStdString()));
    }

    QStringList colDefs;
    QStringList pkCols;

    for (int row = 0; row < columnsTable_->rowCount(); ++row) {
        auto* nameItem = columnsTable_->item(row, kColName);
        auto* typeCombo = qobject_cast<QComboBox*>(columnsTable_->cellWidget(row, kColType));
        auto* defaultItem = columnsTable_->item(row, kColDefault);

        const QString colName = nameItem ? nameItem->text().trimmed() : QString{};
        const QString colType = typeCombo ? typeCombo->currentText() : QStringLiteral("TEXT");

        // Nullable: find QCheckBox inside wrapper widget
        bool isNullable = true;
        if (auto* wrapper = columnsTable_->cellWidget(row, kColNullable)) {
            if (auto* cb = wrapper->findChild<QCheckBox*>()) {
                isNullable = cb->isChecked();
            }
        }

        // PK
        bool isPK = false;
        if (auto* wrapper = columnsTable_->cellWidget(row, kColPrimaryKey)) {
            if (auto* cb = wrapper->findChild<QCheckBox*>()) {
                isPK = cb->isChecked();
            }
        }

        if (colName.isEmpty()) continue;

        QString def = QString::fromStdString(quoteIdentifier(dialect, colName.toStdString()))
                    + QStringLiteral(" ") + colType;
        if (!isNullable) def += QStringLiteral(" NOT NULL");

        const QString defaultVal = defaultItem ? defaultItem->text().trimmed() : QString{};
        if (!defaultVal.isEmpty()) {
            def += QStringLiteral(" DEFAULT ") + defaultVal;
        }

        colDefs << def;
        if (isPK) {
            pkCols << QString::fromStdString(quoteIdentifier(dialect, colName.toStdString()));
        }
    }

    if (colDefs.isEmpty()) return {};

    if (!pkCols.isEmpty()) {
        colDefs << QStringLiteral("PRIMARY KEY (%1)").arg(pkCols.join(QStringLiteral(", ")));
    }

    return QStringLiteral("CREATE TABLE %1 (\n  %2\n)")
        .arg(qualifiedTable, colDefs.join(QStringLiteral(",\n  ")));
}

void CreateTableDialog::onCreate() {
    const QString sql = buildCreateSql();
    if (sql.isEmpty()) {
        QMessageBox::warning(this, tr("Create Table"),
                             tr("Please provide a table name and at least one column."));
        return;
    }

    if (!adapter_) {
        QMessageBox::critical(this, tr("Create Table"), tr("No database connection."));
        return;
    }

    try {
        adapter_->executeRaw(sql.toStdString());
    } catch (const GridexError& e) {
        QMessageBox::critical(this, tr("Create Table Failed"),
                              QString::fromUtf8(e.what()));
        return;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Create Table Failed"),
                              QString::fromUtf8(e.what()));
        return;
    }

    const QString name = tableNameEdit_->text().trimmed();
    emit tableCreated(name);
    accept();
}

} // namespace gridex
