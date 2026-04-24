#include "Presentation/Views/TableList/TableGridView.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "Core/Enums/SQLDialect.h"
#include "Core/Errors/GridexError.h"
#include "Presentation/ViewModels/WorkspaceState.h"
#include "Services/Export/ExportService.h"

namespace gridex {

namespace {
constexpr int kRoleTableName  = Qt::UserRole + 1;
constexpr int kRoleSchemaName = Qt::UserRole + 2;
}

TableGridView::TableGridView(WorkspaceState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    buildUi();
    if (state_) {
        connect(state_, &WorkspaceState::connectionOpened,
                this, &TableGridView::onConnectionOpened);
        connect(state_, &WorkspaceState::connectionClosed,
                this, &TableGridView::onConnectionClosed);
        if (state_->isOpen()) onConnectionOpened();
    }
}

void TableGridView::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar: schema combo + search + refresh
    auto* toolbar = new QWidget(this);
    auto* tbH = new QHBoxLayout(toolbar);
    tbH->setContentsMargins(6, 6, 6, 6);
    tbH->setSpacing(6);

    schemaCb_ = new QComboBox(toolbar);
    schemaCb_->setToolTip(tr("Schema"));
    schemaCb_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(schemaCb_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TableGridView::onSchemaChanged);
    tbH->addWidget(schemaCb_);

    searchEd_ = new QLineEdit(toolbar);
    searchEd_->setPlaceholderText(tr("Filter tables..."));
    searchEd_->setClearButtonEnabled(true);
    connect(searchEd_, &QLineEdit::textChanged,
            this, [this](const QString& t) { onSearchChanged(t); });
    tbH->addWidget(searchEd_, 1);

    refreshBtn_ = new QPushButton(QStringLiteral("↻"), toolbar);
    refreshBtn_->setToolTip(tr("Refresh"));
    refreshBtn_->setFixedSize(28, 28);
    refreshBtn_->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn_, &QPushButton::clicked, this, [this] { reload(); });
    tbH->addWidget(refreshBtn_);

    root->addWidget(toolbar);

    // Grid list view
    listView_ = new QListView(this);
    listView_->setViewMode(QListView::IconMode);
    listView_->setGridSize(QSize(110, 80));
    listView_->setIconSize(QSize(32, 32));
    listView_->setResizeMode(QListView::Adjust);
    listView_->setMovement(QListView::Static);
    listView_->setWordWrap(true);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listView_->setSelectionMode(QAbstractItemView::SingleSelection);
    listView_->setContextMenuPolicy(Qt::CustomContextMenu);
    listView_->setFrameShape(QFrame::NoFrame);
    listView_->setUniformItemSizes(true);

    model_ = new QStandardItemModel(this);
    listView_->setModel(model_);

    connect(listView_, &QListView::doubleClicked,
            this, &TableGridView::onItemDoubleClicked);
    connect(listView_, &QListView::customContextMenuRequested,
            this, &TableGridView::onContextMenuRequested);

    root->addWidget(listView_, 1);
}

void TableGridView::reload() {
    const QString schema = schemaCb_ ? schemaCb_->currentText() : QString{};
    loadSchemas();
    if (!schema.isEmpty()) {
        const int idx = schemaCb_->findText(schema);
        if (idx >= 0) {
            QSignalBlocker blk(schemaCb_);
            schemaCb_->setCurrentIndex(idx);
        }
    }
    loadTables(schemaCb_ ? schemaCb_->currentText() : QString{});
}

void TableGridView::onConnectionOpened() {
    loadSchemas();
    loadTables(schemaCb_ ? schemaCb_->currentText() : QString{});
}

void TableGridView::onConnectionClosed() {
    if (schemaCb_) { QSignalBlocker blk(schemaCb_); schemaCb_->clear(); }
    model_->clear();
    allTableNames_.clear();
}

void TableGridView::loadSchemas() {
    if (!schemaCb_ || !state_ || !state_->adapter()) return;
    QSignalBlocker blk(schemaCb_);
    schemaCb_->clear();
    try {
        const auto schemas = state_->adapter()->listSchemas(std::nullopt);
        for (const auto& s : schemas)
            schemaCb_->addItem(QString::fromUtf8(s.c_str()));
        const int pubIdx = schemaCb_->findText(QStringLiteral("public"));
        schemaCb_->setCurrentIndex(pubIdx >= 0 ? pubIdx : 0);
    } catch (const GridexError&) {}
}

void TableGridView::loadTables(const QString& schema) {
    model_->clear();
    allTableNames_.clear();
    activeSchema_ = schema;
    if (!state_ || !state_->adapter()) return;

    try {
        const auto tables = state_->adapter()->listTables(schema.toStdString());
        for (const auto& t : tables) {
            const QString name = QString::fromUtf8(t.name.c_str());
            allTableNames_ << name;

            auto* item = new QStandardItem(name);
            item->setData(name, kRoleTableName);
            item->setData(schema, kRoleSchemaName);
            item->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView));
            item->setToolTip(schema.isEmpty() ? name : (schema + "." + name));
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            model_->appendRow(item);
        }
    } catch (const GridexError& e) {
        auto* err = new QStandardItem(tr("(error)"));
        err->setEnabled(false);
        err->setToolTip(QString::fromUtf8(e.what()));
        model_->appendRow(err);
    }

    applyFilter(searchEd_ ? searchEd_->text() : QString{});
}

void TableGridView::applyFilter(const QString& text) {
    const QString lower = text.trimmed().toLower();
    for (int i = 0; i < model_->rowCount(); ++i) {
        auto* item = model_->item(i);
        const bool visible = lower.isEmpty() ||
            item->text().toLower().contains(lower);
        listView_->setRowHidden(i, !visible);
    }
}

void TableGridView::onSchemaChanged(int /*index*/) {
    loadTables(schemaCb_->currentText());
}

void TableGridView::onSearchChanged(const QString& text) {
    applyFilter(text);
}

void TableGridView::onItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    auto* item = model_->itemFromIndex(index);
    if (!item) return;
    emit tableSelected(item->data(kRoleSchemaName).toString(),
                       item->data(kRoleTableName).toString());
}

void TableGridView::onContextMenuRequested(const QPoint& pos) {
    const QModelIndex index = listView_->indexAt(pos);
    if (!index.isValid()) return;
    auto* item = model_->itemFromIndex(index);
    if (!item) return;

    const QString schema = item->data(kRoleSchemaName).toString();
    const QString table  = item->data(kRoleTableName).toString();

    QMenu menu(this);
    buildContextMenu(&menu, schema, table);
    menu.exec(listView_->viewport()->mapToGlobal(pos));
}

void TableGridView::buildContextMenu(QMenu* menu, const QString& schema, const QString& table) {
    auto* openAct = menu->addAction(tr("Open in New Tab"));
    connect(openAct, &QAction::triggered, this, [this, schema, table] {
        emit tableSelected(schema, table);
    });

    menu->addSeparator();

    auto* copyNameAct = menu->addAction(tr("Copy Table Name"));
    connect(copyNameAct, &QAction::triggered, this, [table] {
        QApplication::clipboard()->setText(table);
    });

    const QString selectSql = QStringLiteral("SELECT * FROM %1 LIMIT 100;").arg(table);
    auto* copySelectAct = menu->addAction(tr("Copy SELECT * FROM..."));
    connect(copySelectAct, &QAction::triggered, this, [selectSql] {
        QApplication::clipboard()->setText(selectSql);
    });

    menu->addSeparator();

    auto* truncateAct = menu->addAction(tr("Truncate Table..."));
    connect(truncateAct, &QAction::triggered, this, [this, schema, table] {
        if (!state_ || !state_->adapter()) return;
        const auto answer = QMessageBox::warning(
            this, tr("Truncate Table"),
            tr("Are you sure you want to truncate \"%1\"?\nAll rows will be deleted.").arg(table),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
        try {
            const DatabaseType dt = state_->adapter()->databaseType();
            std::string sql;
            if (dt == DatabaseType::SQLite) {
                sql = "DELETE FROM " + quoteIdentifier(sqlDialect(dt), table.toStdString());
            } else if (!schema.isEmpty()) {
                sql = "TRUNCATE TABLE "
                    + quoteIdentifier(sqlDialect(dt), schema.toStdString())
                    + "." + quoteIdentifier(sqlDialect(dt), table.toStdString());
            } else {
                sql = "TRUNCATE TABLE " + quoteIdentifier(sqlDialect(dt), table.toStdString());
            }
            state_->adapter()->executeRaw(sql);
        } catch (const GridexError& e) {
            QMessageBox::critical(this, tr("Truncate Failed"), QString::fromUtf8(e.what()));
        }
    });

    auto* dropAct = menu->addAction(tr("Delete Table..."));
    connect(dropAct, &QAction::triggered, this, [this, schema, table] {
        if (!state_ || !state_->adapter()) return;
        const auto answer = QMessageBox::warning(
            this, tr("Delete Table"),
            tr("Are you sure you want to drop \"%1\"?\nThis action cannot be undone.").arg(table),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
        try {
            const DatabaseType dt = state_->adapter()->databaseType();
            std::string sql;
            if (!schema.isEmpty() &&
                dt != DatabaseType::SQLite &&
                dt != DatabaseType::MySQL)
            {
                sql = "DROP TABLE "
                    + quoteIdentifier(sqlDialect(dt), schema.toStdString())
                    + "." + quoteIdentifier(sqlDialect(dt), table.toStdString());
            } else {
                sql = "DROP TABLE " + quoteIdentifier(sqlDialect(dt), table.toStdString());
            }
            state_->adapter()->executeRaw(sql);
            loadTables(schema);
            emit tableDeleted(schema, table);
        } catch (const GridexError& e) {
            QMessageBox::critical(this, tr("Delete Table Failed"), QString::fromUtf8(e.what()));
        }
    });

    menu->addSeparator();

    auto* exportAct = menu->addAction(tr("Export Table..."));
    connect(exportAct, &QAction::triggered, this, [this, schema, table] {
        if (!state_ || !state_->adapter()) return;
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Table — %1").arg(table),
            QStringLiteral("%1.csv").arg(table),
            tr("CSV (*.csv);;JSON (*.json);;SQL (*.sql)"));
        if (path.isEmpty()) return;
        try {
            const std::optional<std::string> schemaOpt =
                schema.isEmpty() ? std::nullopt : std::make_optional(schema.toStdString());
            const QueryResult result = state_->adapter()->fetchRows(
                table.toStdString(), schemaOpt, std::nullopt, std::nullopt, std::nullopt,
                10000, 0);
            const std::string fp = path.toStdString();
            if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
                ExportService::exportToJson(result, fp);
            else if (path.endsWith(QStringLiteral(".sql"), Qt::CaseInsensitive))
                ExportService::exportToSql(result, table.toStdString(), fp);
            else
                ExportService::exportToCsv(result, fp);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Export Failed"), QString::fromUtf8(e.what()));
        }
    });
}

}
