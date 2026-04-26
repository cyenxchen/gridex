#include "Presentation/Views/Sidebar/WorkspaceSidebar.h"

#include <chrono>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTextStream>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "Core/Errors/GridexError.h"
#include "Core/Utils/SqlStatementSplitter.h"
#include "Presentation/Views/ImportSQL/SqlImportWizard.h"
#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SQLDialect.h"
#include "Core/Models/Schema/SchemaSnapshot.h"
#include "Data/Keychain/SecretStore.h"
#include "Data/Persistence/AppDatabase.h"
#include "Data/Persistence/SavedQueryRepository.h"
#include "Presentation/ViewModels/WorkspaceState.h"
#include "Presentation/Views/Backup/BackupProgressDialog.h"
#include "Presentation/Views/TableList/TableGridView.h"
#include "Services/Export/DatabaseDumpRunner.h"
#include "Services/Export/ExportService.h"

namespace gridex {

namespace {

constexpr int kRoleKind       = Qt::UserRole + 1;
constexpr int kRoleSchemaName = Qt::UserRole + 2;
constexpr int kRoleTableName  = Qt::UserRole + 3;
constexpr int kRoleLoaded     = Qt::UserRole + 4;
constexpr int kRoleRoutineName = Qt::UserRole + 5;

enum NodeKind {
    NodeSchema      = 1,
    NodeTable       = 2,
    NodePlaceholder = 3,
    NodeFunctionsGroup  = 4,
    NodeProceduresGroup = 5,
    NodeFunction    = 6,
    NodeProcedure   = 7
};

// splitSqlStatements is defined in Core/Utils/SqlStatementSplitter.h

// Parse one CSV row according to RFC 4180. Input is a single line (or block
// when the row spans newlines inside quotes). We operate on the full CSV
// blob and advance a cursor so that multi-line quoted fields work.
struct CsvParser {
    QString text;
    int pos = 0;

    bool atEnd() const { return pos >= text.size(); }

    // Returns next row as list of fields; empty vector when no more rows.
    QStringList readRow() {
        QStringList fields;
        if (atEnd()) return fields;

        QString cell;
        bool quoted = false;
        while (pos < text.size()) {
            const QChar c = text.at(pos);
            if (quoted) {
                if (c == '"') {
                    if (pos + 1 < text.size() && text.at(pos + 1) == '"') {
                        cell.append('"');
                        pos += 2;
                        continue;
                    }
                    quoted = false;
                    ++pos;
                    continue;
                }
                cell.append(c);
                ++pos;
                continue;
            }
            if (c == '"' && cell.isEmpty()) { quoted = true; ++pos; continue; }
            if (c == ',') { fields << cell; cell.clear(); ++pos; continue; }
            if (c == '\r') { ++pos; continue; }  // ignore
            if (c == '\n') { ++pos; fields << cell; return fields; }
            cell.append(c);
            ++pos;
        }
        // Last field (no trailing newline)
        fields << cell;
        return fields;
    }
};

QPushButton* makeTabButton(const QString& glyph, const QString& tooltip, QWidget* parent) {
    auto* btn = new QPushButton(glyph, parent);
    btn->setCheckable(true);
    btn->setAutoExclusive(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(28);
    btn->setToolTip(tooltip);
    btn->setFlat(true);
    btn->setProperty("tab", true);  // styled via style.qss QPushButton[tab="true"]
    return btn;
}

}

WorkspaceSidebar::~WorkspaceSidebar() = default;

WorkspaceSidebar::WorkspaceSidebar(WorkspaceState* state,
                                   std::shared_ptr<AppDatabase> appDb,
                                   QWidget* parent)
    : QWidget(parent), state_(state), appDb_(std::move(appDb)) {
    if (appDb_) savedQueryRepo_ = std::make_unique<SavedQueryRepository>(appDb_);
    buildUi();
    if (appDb_) loadHistoryFromDb();
    if (savedQueryRepo_) reloadSavedQueriesTree();
    if (state_) {
        connect(state_, &WorkspaceState::connectionOpened,
                this, &WorkspaceSidebar::onConnectionOpened);
        connect(state_, &WorkspaceState::connectionClosed,
                this, &WorkspaceSidebar::onConnectionClosed);
        if (state_->isOpen()) onConnectionOpened();
    }
}

void WorkspaceSidebar::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- Tab bar (Items / Queries / History) ----
    auto* tabBar = new QWidget(this);
    auto* tabH = new QHBoxLayout(tabBar);
    tabH->setContentsMargins(6, 4, 6, 4);
    tabH->setSpacing(1);

    itemsTabBtn_   = makeTabButton(QStringLiteral("🗂"), tr("Items"),   tabBar);
    queriesTabBtn_ = makeTabButton(QStringLiteral("📄"), tr("Queries"), tabBar);
    historyTabBtn_ = makeTabButton(QStringLiteral("🕑"), tr("History"), tabBar);
    savedTabBtn_   = makeTabButton(QStringLiteral("⭐"), tr("Saved"),   tabBar);
    itemsTabBtn_->setChecked(true);
    tabH->addWidget(itemsTabBtn_, 1);
    tabH->addWidget(queriesTabBtn_, 1);
    tabH->addWidget(historyTabBtn_, 1);
    tabH->addWidget(savedTabBtn_, 1);
    connect(itemsTabBtn_,   &QPushButton::clicked, this, [this] { onTabClicked(0); });
    connect(queriesTabBtn_, &QPushButton::clicked, this, [this] { onTabClicked(1); });
    connect(historyTabBtn_, &QPushButton::clicked, this, [this] { onTabClicked(2); });
    connect(savedTabBtn_,   &QPushButton::clicked, this, [this] { onTabClicked(3); });
    root->addWidget(tabBar);

    auto* tabDiv = new QFrame(this);
    tabDiv->setFrameShape(QFrame::HLine);
    root->addWidget(tabDiv);

    // ---- Body: stacked (Items tab, empty tabs for Queries/History) ----
    body_ = new QStackedWidget(this);

    // Items tab
    auto* itemsPage = new QWidget(body_);
    auto* itemsV = new QVBoxLayout(itemsPage);
    itemsV->setContentsMargins(0, 0, 0, 0);
    itemsV->setSpacing(0);

    auto* searchRow = new QWidget(itemsPage);
    auto* searchH = new QHBoxLayout(searchRow);
    searchH->setContentsMargins(8, 6, 8, 6);
    searchH->setSpacing(4);
    searchEdit_ = new QLineEdit(searchRow);
    searchEdit_->setPlaceholderText(tr("Search tables..."));
    searchEdit_->setClearButtonEnabled(true);
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &WorkspaceSidebar::onSearchChanged);
    searchH->addWidget(searchEdit_);

    gridToggleBtn_ = new QPushButton(QStringLiteral("⊞"), searchRow);
    gridToggleBtn_->setFixedSize(26, 26);
    gridToggleBtn_->setCheckable(true);
    gridToggleBtn_->setChecked(false);
    gridToggleBtn_->setToolTip(tr("Toggle Grid View"));
    gridToggleBtn_->setCursor(Qt::PointingHandCursor);
    connect(gridToggleBtn_, &QPushButton::toggled, this, [this](bool checked) {
        itemsViewStack_->setCurrentIndex(checked ? 1 : 0);
        searchEdit_->setPlaceholderText(checked ? tr("Filter tables...") : tr("Search tables..."));
        if (checked && tableGrid_) tableGrid_->reload();
    });
    searchH->addWidget(gridToggleBtn_);

    itemsV->addWidget(searchRow);

    auto* searchDiv = new QFrame(itemsPage);
    searchDiv->setFrameShape(QFrame::HLine);
    itemsV->addWidget(searchDiv);

    // Stacked: page 0 = tree, page 1 = grid
    itemsViewStack_ = new QStackedWidget(itemsPage);

    tree_ = new QTreeView(itemsViewStack_);
    tree_->setHeaderHidden(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    model_ = new QStandardItemModel(this);
    tree_->setModel(model_);

    connect(tree_, &QTreeView::expanded,
            this, &WorkspaceSidebar::onItemExpanded);
    connect(tree_, &QTreeView::doubleClicked,
            this, &WorkspaceSidebar::onItemDoubleClicked);
    connect(tree_, &QTreeView::customContextMenuRequested,
            this, &WorkspaceSidebar::onContextMenuRequested);

    itemsViewStack_->addWidget(tree_);

    tableGrid_ = new TableGridView(state_, itemsViewStack_);
    connect(tableGrid_, &TableGridView::tableSelected,
            this, &WorkspaceSidebar::tableSelected);
    connect(tableGrid_, &TableGridView::tableDeleted,
            this, &WorkspaceSidebar::tableDeleted);
    itemsViewStack_->addWidget(tableGrid_);

    itemsViewStack_->setCurrentIndex(0);
    itemsV->addWidget(itemsViewStack_, 1);

    // Bottom bar: schema selector + DB info + disconnect
    auto* bottomDiv = new QFrame(itemsPage);
    bottomDiv->setFrameShape(QFrame::HLine);
    itemsV->addWidget(bottomDiv);

    auto* bottomBar = new QWidget(itemsPage);
    auto* bottomV = new QVBoxLayout(bottomBar);
    bottomV->setContentsMargins(8, 6, 8, 6);
    bottomV->setSpacing(4);

    // Schema selector row
    auto* schemaRow = new QWidget(bottomBar);
    auto* schemaH = new QHBoxLayout(schemaRow);
    schemaH->setContentsMargins(0, 0, 0, 0);
    schemaH->setSpacing(6);

    schemaCombo_ = new QComboBox(schemaRow);
    schemaCombo_->setToolTip(tr("Active schema"));
    schemaCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(schemaCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WorkspaceSidebar::onSchemaChanged);
    schemaH->addWidget(schemaCombo_);
    schemaH->addStretch();

    bottomV->addWidget(schemaRow);

    // DB info label
    dbInfoLabel_ = new QLabel(bottomBar);
    dbInfoLabel_->setWordWrap(false);
    bottomV->addWidget(dbInfoLabel_);

    // Bottom action row: [New Table] [Disconnect]
    auto* disconnRow = new QWidget(bottomBar);
    auto* disconnH = new QHBoxLayout(disconnRow);
    disconnH->setContentsMargins(0, 0, 0, 0);
    disconnH->setSpacing(6);

    newTableBtn_ = new QPushButton(tr("+ New Table"), disconnRow);
    connect(newTableBtn_, &QPushButton::clicked, this, [this] {
        const QString schema = schemaCombo_ ? schemaCombo_->currentText() : QString{};
        emit newTableRequested(schema);
    });

    disconnectBtn_ = new QPushButton(tr("Disconnect"), disconnRow);
    connect(disconnectBtn_, &QPushButton::clicked,
            this, &WorkspaceSidebar::disconnectRequested);

    disconnH->addWidget(newTableBtn_);
    disconnH->addStretch();
    disconnH->addWidget(disconnectBtn_);
    bottomV->addWidget(disconnRow);

    itemsV->addWidget(bottomBar);

    body_->addWidget(itemsPage);

    // Queries tab placeholder
    auto* queriesPage = new QWidget(body_);
    auto* qv = new QVBoxLayout(queriesPage);
    qv->addStretch();
    auto* noQ = new QLabel(tr("📄\nNo Queries"), queriesPage);
    noQ->setAlignment(Qt::AlignCenter);
    qv->addWidget(noQ);
    qv->addStretch();
    body_->addWidget(queriesPage);

    // History tab
    auto* historyPage = new QWidget(body_);
    auto* hv = new QVBoxLayout(historyPage);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(0);
    historyList_ = new QListWidget(historyPage);
    historyList_->setFrameShape(QFrame::NoFrame);
    historyList_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyList_->setContextMenuPolicy(Qt::CustomContextMenu);
    historyList_->setToolTip(tr("Double-click to copy SQL to clipboard"));
    connect(historyList_, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem* item) {
            if (!item) return;
            QApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
        });
    connect(historyList_, &QListWidget::customContextMenuRequested, this,
        [this](const QPoint& pos) {
            QMenu menu(this);
            auto* clearAct = menu.addAction(tr("Clear History"));
            connect(clearAct, &QAction::triggered, this, [this] {
                if (!appDb_) return;
                const auto btn = QMessageBox::question(this, tr("Clear History"),
                    tr("Remove all query history?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (btn != QMessageBox::Yes) return;
                try { appDb_->clearAllHistory(); } catch (...) {}
                historyList_->clear();
            });
            menu.exec(historyList_->viewport()->mapToGlobal(pos));
        });
    hv->addWidget(historyList_, 1);
    body_->addWidget(historyPage);

    // Saved queries tab
    auto* savedPage = new QWidget(body_);
    auto* sv = new QVBoxLayout(savedPage);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);
    savedTree_ = new QTreeWidget(savedPage);
    savedTree_->setHeaderHidden(true);
    savedTree_->setFrameShape(QFrame::NoFrame);
    savedTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    savedTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(savedTree_, &QTreeWidget::itemDoubleClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (!item || item->childCount() > 0) return;
            const QString sql = item->data(0, Qt::UserRole).toString();
            if (!sql.isEmpty()) emit loadSavedQueryRequested(sql);
        });
    connect(savedTree_, &QTreeWidget::customContextMenuRequested,
            this, &WorkspaceSidebar::onSavedQueryContextMenu);
    sv->addWidget(savedTree_, 1);
    body_->addWidget(savedPage);

    body_->setCurrentIndex(0);
    root->addWidget(body_, 1);
}

void WorkspaceSidebar::refreshTree() {
    onConnectionOpened();
}

void WorkspaceSidebar::onTabClicked(int tab) {
    activeTab_ = tab;
    itemsTabBtn_->setChecked(tab == 0);
    queriesTabBtn_->setChecked(tab == 1);
    historyTabBtn_->setChecked(tab == 2);
    savedTabBtn_->setChecked(tab == 3);
    body_->setCurrentIndex(tab);
}

void WorkspaceSidebar::onConnectionOpened() {
    loadSchemas();

    // Populate schema combo from adapter.
    if (schemaCombo_ && state_ && state_->adapter()) {
        // Block signals while rebuilding to avoid triggering reload mid-populate.
        QSignalBlocker blocker(schemaCombo_);
        schemaCombo_->clear();
        try {
            const auto schemas = state_->adapter()->listSchemas(std::nullopt);
            for (const auto& s : schemas) {
                schemaCombo_->addItem(QString::fromUtf8(s.c_str()));
            }
        } catch (const GridexError&) {}
        // Default to "public" if present, else index 0.
        const int pubIdx = schemaCombo_->findText(QStringLiteral("public"));
        schemaCombo_->setCurrentIndex(pubIdx >= 0 ? pubIdx : 0);
    }

    // DB info: "name · dbtype · host"
    if (dbInfoLabel_ && state_) {
        const auto& cfg = state_->config();
        const QString dbType = QString::fromUtf8(
            displayName(cfg.databaseType).data(),
            static_cast<qsizetype>(displayName(cfg.databaseType).size()));
        const QString host = QString::fromUtf8(cfg.displayHost().c_str());
        dbInfoLabel_->setText(QStringLiteral("%1 · %2").arg(dbType, host));
    }
}

void WorkspaceSidebar::onConnectionClosed() {
    model_->clear();
    if (schemaCombo_) schemaCombo_->clear();
    if (dbInfoLabel_) dbInfoLabel_->clear();
}

void WorkspaceSidebar::loadSchemas() {
    model_->clear();
    if (!state_ || !state_->adapter()) return;

    try {
        const auto schemas = state_->adapter()->listSchemas(std::nullopt);
        for (const auto& s : schemas) {
            auto* schemaItem = new QStandardItem(QString::fromUtf8(s.c_str()));
            schemaItem->setData(NodeSchema, kRoleKind);
            schemaItem->setData(QString::fromUtf8(s.c_str()), kRoleSchemaName);
            schemaItem->setData(false, kRoleLoaded);

            // Insert placeholder child so the expand arrow shows up.
            auto* placeholder = new QStandardItem(tr("(loading...)"));
            placeholder->setData(NodePlaceholder, kRoleKind);
            schemaItem->appendRow(placeholder);

            model_->appendRow(schemaItem);
        }
        // If only one schema, auto-expand it for convenience.
        if (schemas.size() == 1 && model_->rowCount() > 0) {
            tree_->expand(model_->index(0, 0));
        }
    } catch (const GridexError&) {
        auto* err = new QStandardItem(tr("(schema listing failed)"));
        err->setEnabled(false);
        model_->appendRow(err);
    }
}

void WorkspaceSidebar::onItemExpanded(const QModelIndex& index) {
    auto* item = model_->itemFromIndex(index);
    if (!item) return;
    if (item->data(kRoleLoaded).toBool()) return;

    const int kind = item->data(kRoleKind).toInt();
    const QString schema = item->data(kRoleSchemaName).toString();

    if (kind == NodeSchema) {
        item->removeRows(0, item->rowCount());
        loadTablesForSchema(item, schema);
        item->setData(true, kRoleLoaded);
    } else if (kind == NodeFunctionsGroup) {
        item->removeRows(0, item->rowCount());
        loadFunctionsForSchema(item, schema);
        item->setData(true, kRoleLoaded);
    } else if (kind == NodeProceduresGroup) {
        item->removeRows(0, item->rowCount());
        loadProceduresForSchema(item, schema);
        item->setData(true, kRoleLoaded);
    }
}

void WorkspaceSidebar::loadTablesForSchema(QStandardItem* schemaItem, const QString& schemaName) {
    if (!state_ || !state_->adapter()) return;
    try {
        const auto tables = state_->adapter()->listTables(schemaName.toStdString());
        if (tables.empty()) {
            auto* empty = new QStandardItem(tr("(no tables)"));
            empty->setEnabled(false);
            schemaItem->appendRow(empty);
        } else {
            for (const auto& t : tables) {
                auto* tableItem = new QStandardItem(QString::fromUtf8(t.name.c_str()));
                tableItem->setData(NodeTable, kRoleKind);
                tableItem->setData(schemaName, kRoleSchemaName);
                tableItem->setData(QString::fromUtf8(t.name.c_str()), kRoleTableName);
                schemaItem->appendRow(tableItem);
            }
        }
    } catch (const GridexError& e) {
        auto* err = new QStandardItem(QString::fromUtf8(e.what()));
        err->setEnabled(false);
        schemaItem->appendRow(err);
    }

    // Functions group node
    auto* fnGroup = new QStandardItem(tr("Functions"));
    fnGroup->setData(NodeFunctionsGroup, kRoleKind);
    fnGroup->setData(schemaName, kRoleSchemaName);
    fnGroup->setData(false, kRoleLoaded);
    auto* fnPlaceholder = new QStandardItem(tr("(loading...)"));
    fnPlaceholder->setData(NodePlaceholder, kRoleKind);
    fnGroup->appendRow(fnPlaceholder);
    schemaItem->appendRow(fnGroup);

    // Procedures group node
    auto* procGroup = new QStandardItem(tr("Procedures"));
    procGroup->setData(NodeProceduresGroup, kRoleKind);
    procGroup->setData(schemaName, kRoleSchemaName);
    procGroup->setData(false, kRoleLoaded);
    auto* procPlaceholder = new QStandardItem(tr("(loading...)"));
    procPlaceholder->setData(NodePlaceholder, kRoleKind);
    procGroup->appendRow(procPlaceholder);
    schemaItem->appendRow(procGroup);
}

void WorkspaceSidebar::loadFunctionsForSchema(QStandardItem* parent, const QString& schemaName) {
    if (!state_ || !state_->adapter()) return;
    try {
        const std::optional<std::string> schemaOpt =
            schemaName.isEmpty() ? std::nullopt
                                 : std::make_optional(schemaName.toStdString());
        const auto fns = state_->adapter()->listFunctions(schemaOpt);
        if (fns.empty()) {
            auto* empty = new QStandardItem(tr("(none)"));
            empty->setEnabled(false);
            parent->appendRow(empty);
            return;
        }
        for (const auto& fn : fns) {
            auto* item = new QStandardItem(QString::fromUtf8(fn.c_str()));
            item->setData(NodeFunction, kRoleKind);
            item->setData(schemaName, kRoleSchemaName);
            item->setData(QString::fromUtf8(fn.c_str()), kRoleRoutineName);
            parent->appendRow(item);
        }
    } catch (const GridexError&) {
        auto* empty = new QStandardItem(tr("(none)"));
        empty->setEnabled(false);
        parent->appendRow(empty);
    }
}

void WorkspaceSidebar::loadProceduresForSchema(QStandardItem* parent, const QString& schemaName) {
    if (!state_ || !state_->adapter()) return;
    try {
        const std::optional<std::string> schemaOpt =
            schemaName.isEmpty() ? std::nullopt
                                 : std::make_optional(schemaName.toStdString());
        const auto procs = state_->adapter()->listProcedures(schemaOpt);
        if (procs.empty()) {
            auto* empty = new QStandardItem(tr("(none)"));
            empty->setEnabled(false);
            parent->appendRow(empty);
            return;
        }
        for (const auto& p : procs) {
            auto* item = new QStandardItem(QString::fromUtf8(p.c_str()));
            item->setData(NodeProcedure, kRoleKind);
            item->setData(schemaName, kRoleSchemaName);
            item->setData(QString::fromUtf8(p.c_str()), kRoleRoutineName);
            parent->appendRow(item);
        }
    } catch (const GridexError&) {
        auto* empty = new QStandardItem(tr("(none)"));
        empty->setEnabled(false);
        parent->appendRow(empty);
    }
}

void WorkspaceSidebar::onItemDoubleClicked(const QModelIndex& index) {
    auto* item = model_->itemFromIndex(index);
    if (!item) return;
    const int kind = item->data(kRoleKind).toInt();
    const QString schema = item->data(kRoleSchemaName).toString();
    if (kind == NodeTable) {
        emit tableSelected(schema, item->data(kRoleTableName).toString());
    } else if (kind == NodeFunction) {
        emit functionSelected(schema, item->data(kRoleRoutineName).toString());
    } else if (kind == NodeProcedure) {
        emit procedureSelected(schema, item->data(kRoleRoutineName).toString());
    }
}

void WorkspaceSidebar::onSchemaChanged(int /*index*/) {
    reloadActiveSchema();
}

void WorkspaceSidebar::reloadActiveSchema() {
    if (!schemaCombo_ || schemaCombo_->count() == 0) return;
    const QString schema = schemaCombo_->currentText();
    // Reload tree with only the selected schema's tables visible.
    model_->clear();
    if (!state_ || !state_->adapter()) return;
    auto* schemaItem = new QStandardItem(schema);
    schemaItem->setData(NodeSchema, kRoleKind);
    schemaItem->setData(schema, kRoleSchemaName);
    schemaItem->setData(false, kRoleLoaded);
    auto* placeholder = new QStandardItem(tr("(loading...)"));
    placeholder->setData(NodePlaceholder, kRoleKind);
    schemaItem->appendRow(placeholder);
    model_->appendRow(schemaItem);
    tree_->expand(model_->index(0, 0));
}

void WorkspaceSidebar::onSearchChanged(const QString& text) {
    // In grid mode, delegate filtering to TableGridView.
    if (gridToggleBtn_ && gridToggleBtn_->isChecked()) {
        if (tableGrid_) tableGrid_->onSearchChanged(text);
        return;
    }

    // Simple hide/show filter across schemas+tables. For v1 we do a flat walk.
    const auto lower = text.trimmed().toLower();
    for (int i = 0; i < model_->rowCount(); ++i) {
        auto* schemaItem = model_->item(i);
        bool schemaMatches = lower.isEmpty() ||
            schemaItem->text().toLower().contains(lower);
        bool anyChildMatches = false;
        for (int j = 0; j < schemaItem->rowCount(); ++j) {
            auto* child = schemaItem->child(j);
            const bool ok = lower.isEmpty() || child->text().toLower().contains(lower);
            tree_->setRowHidden(j, schemaItem->index(), !ok);
            anyChildMatches = anyChildMatches || ok;
        }
        tree_->setRowHidden(i, QModelIndex(), !(schemaMatches || anyChildMatches));
    }
}

void WorkspaceSidebar::onContextMenuRequested(const QPoint& pos) {
    const QModelIndex index = tree_->indexAt(pos);
    auto* item = index.isValid() ? model_->itemFromIndex(index) : nullptr;
    const int kind = item ? item->data(kRoleKind).toInt() : 0;

    QMenu menu(this);

    // Refresh is always first — works on blank area, schema nodes, and
    // table nodes. Full reload is cheap and keeps schemas + tables in sync.
    auto* refreshAction = menu.addAction(tr("Refresh"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    connect(refreshAction, &QAction::triggered, this, [this] { loadSchemas(); });

    // Blank area or schema node: Refresh + import/backup/restore actions.
    if (!item || kind != NodeTable) {
        menu.addSeparator();

        auto* runSqlAct  = menu.addAction(tr("Run SQL File..."));
        auto* backupAct  = menu.addAction(tr("Backup Database..."));
        auto* restoreAct = menu.addAction(tr("Restore Database..."));
        const bool hasAdapter = state_ && state_->adapter();
        runSqlAct->setEnabled(hasAdapter);
        backupAct->setEnabled(hasAdapter);
        restoreAct->setEnabled(hasAdapter);
        connect(runSqlAct,  &QAction::triggered, this, &WorkspaceSidebar::runSqlFile);
        connect(backupAct,  &QAction::triggered, this, &WorkspaceSidebar::backupDatabase);
        connect(restoreAct, &QAction::triggered, this, &WorkspaceSidebar::restoreDatabase);

        menu.exec(tree_->viewport()->mapToGlobal(pos));
        return;
    }

    menu.addSeparator();

    const QString schema = item->data(kRoleSchemaName).toString();
    const QString table  = item->data(kRoleTableName).toString();

    // Open / view actions
    auto* openAction = menu.addAction(tr("Open in New Tab"));
    connect(openAction, &QAction::triggered, this, [this, schema, table] {
        emit tableSelected(schema, table);
    });

    auto* structureAction = menu.addAction(tr("View Structure"));
    connect(structureAction, &QAction::triggered, this, [this, schema, table] {
        emit tableSelected(schema, table);
    });

    menu.addSeparator();

    // Copy actions
    auto* copyNameAction = menu.addAction(tr("Copy Table Name"));
    connect(copyNameAction, &QAction::triggered, this, [table] {
        QApplication::clipboard()->setText(table);
    });

    const QString selectSql = QStringLiteral("SELECT * FROM %1 LIMIT 100;").arg(table);
    auto* copySelectAction = menu.addAction(tr("Copy SELECT * FROM..."));
    connect(copySelectAction, &QAction::triggered, this, [selectSql] {
        QApplication::clipboard()->setText(selectSql);
    });

    auto* copyDdlAction = menu.addAction(tr("Copy CREATE TABLE..."));
    connect(copyDdlAction, &QAction::triggered, this, [this, schema, table] {
        if (!state_ || !state_->adapter()) return;
        try {
            const auto desc = state_->adapter()->describeTable(
                table.toStdString(),
                schema.isEmpty() ? std::nullopt : std::make_optional(schema.toStdString()));
            const SQLDialect dialect = sqlDialect(state_->adapter()->databaseType());
            const QString ddl = QString::fromStdString(desc.toDDL(dialect));
            QApplication::clipboard()->setText(ddl);
        } catch (const GridexError& e) {
            QMessageBox::warning(this, tr("Copy DDL Failed"), QString::fromUtf8(e.what()));
        }
    });

    menu.addSeparator();

    // Truncate
    auto* truncateAction = menu.addAction(tr("Truncate Table..."));
    connect(truncateAction, &QAction::triggered, this, [this, schema, table] {
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
                    + "."
                    + quoteIdentifier(sqlDialect(dt), table.toStdString());
            } else {
                sql = "TRUNCATE TABLE " + quoteIdentifier(sqlDialect(dt), table.toStdString());
            }
            state_->adapter()->executeRaw(sql);
        } catch (const GridexError& e) {
            QMessageBox::critical(this, tr("Truncate Failed"), QString::fromUtf8(e.what()));
        }
    });

    // Drop table
    auto* dropAction = menu.addAction(tr("Delete Table..."));
    connect(dropAction, &QAction::triggered, this, [this, schema, table] {
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
                    + "."
                    + quoteIdentifier(sqlDialect(dt), table.toStdString());
            } else {
                sql = "DROP TABLE " + quoteIdentifier(sqlDialect(dt), table.toStdString());
            }
            state_->adapter()->executeRaw(sql);
            // Refresh sidebar and notify WorkspaceView to close tabs
            loadSchemas();
            emit tableDeleted(schema, table);
        } catch (const GridexError& e) {
            QMessageBox::critical(this, tr("Delete Table Failed"), QString::fromUtf8(e.what()));
        }
    });

    menu.addSeparator();

    // Export
    auto* exportAction = menu.addAction(tr("Export Table..."));
    connect(exportAction, &QAction::triggered, this, [this, schema, table] {
        if (!state_ || !state_->adapter()) return;

        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export Table — %1").arg(table),
            QStringLiteral("%1.csv").arg(table),
            tr("CSV (*.csv);;JSON (*.json);;SQL (*.sql)"));
        if (path.isEmpty()) return;

        try {
            const std::optional<std::string> schemaOpt =
                schema.isEmpty() ? std::nullopt
                                 : std::make_optional(schema.toStdString());
            const QueryResult result = state_->adapter()->fetchRows(
                table.toStdString(), schemaOpt,
                std::nullopt, std::nullopt, std::nullopt,
                10000, 0);

            const std::string filePath = path.toStdString();
            if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
                ExportService::exportToJson(result, filePath);
            } else if (path.endsWith(QStringLiteral(".sql"), Qt::CaseInsensitive)) {
                ExportService::exportToSql(result, table.toStdString(), filePath);
            } else {
                ExportService::exportToCsv(result, filePath);
            }
        } catch (const std::exception& e) {
            QMessageBox::critical(this, tr("Export Failed"), QString::fromUtf8(e.what()));
        }
    });

    // Import CSV (table-specific)
    auto* importCsvAct = menu.addAction(tr("Import CSV..."));
    connect(importCsvAct, &QAction::triggered, this, [this, schema, table] {
        importCsv(schema, table);
    });

    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

// --------------------------------------------------------------------
// Run SQL File (script importer)
// --------------------------------------------------------------------

void WorkspaceSidebar::runSqlFile() {
    if (!state_ || !state_->adapter()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Run SQL File"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("SQL (*.sql);;All files (*)"));
    if (path.isEmpty()) return;

    auto* wizard = new SqlImportWizard(state_->adapter(), path, this);
    wizard->setWindowModality(Qt::WindowModal);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    connect(wizard, &QDialog::finished, this, [this] { loadSchemas(); });
    wizard->show();
}

// --------------------------------------------------------------------
// Import CSV (single table)
// --------------------------------------------------------------------

void WorkspaceSidebar::importCsv(const QString& schema, const QString& table) {
    if (!state_ || !state_->adapter()) return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import CSV into %1").arg(table),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import CSV"),
                             tr("Cannot open file: %1").arg(path));
        return;
    }
    QTextStream ts(&f);
    const QString body = ts.readAll();
    f.close();

    CsvParser parser{body, 0};
    const QStringList header = parser.readRow();
    if (header.isEmpty()) {
        QMessageBox::warning(this, tr("Import CSV"), tr("Empty CSV — no header row."));
        return;
    }

    // Fetch table columns to validate header ↔ table alignment.
    std::vector<ColumnInfo> tableCols;
    try {
        const auto schemaOpt = schema.isEmpty() ? std::nullopt
                                                 : std::make_optional(schema.toStdString());
        const auto desc = state_->adapter()->describeTable(table.toStdString(), schemaOpt);
        tableCols = desc.columns;
    } catch (const GridexError& e) {
        QMessageBox::critical(this, tr("Import CSV"), QString::fromUtf8(e.what()));
        return;
    }

    // Column indexes in target table for each CSV header, by name match.
    // Missing columns → skip that CSV column entirely. Fewer columns in CSV
    // than table is fine (unset cols use default/NULL).
    std::vector<int> colMap;  // size = header.size(); -1 if unmapped
    QStringList mappedNames;
    for (const auto& h : header) {
        int idx = -1;
        for (std::size_t i = 0; i < tableCols.size(); ++i) {
            if (QString::fromStdString(tableCols[i].name).compare(h, Qt::CaseInsensitive) == 0) {
                idx = static_cast<int>(i);
                break;
            }
        }
        colMap.push_back(idx);
        if (idx >= 0) mappedNames << h;
    }
    if (mappedNames.isEmpty()) {
        QMessageBox::warning(this, tr("Import CSV"),
            tr("No CSV columns match table columns. Check header names."));
        return;
    }

    const auto ok = QMessageBox::question(
        this, tr("Import CSV"),
        tr("Mapped %1 of %2 CSV columns. Continue?").arg(mappedNames.size()).arg(header.size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (ok != QMessageBox::Yes) return;

    const DatabaseType dt = state_->adapter()->databaseType();
    const SQLDialect dialect = sqlDialect(dt);
    const std::string qTable = (!schema.isEmpty() && dt != DatabaseType::SQLite && dt != DatabaseType::MySQL)
        ? quoteIdentifier(dialect, schema.toStdString()) + "." + quoteIdentifier(dialect, table.toStdString())
        : quoteIdentifier(dialect, table.toStdString());

    // Build column list once (only mapped cols, in CSV order).
    std::string colList;
    for (std::size_t i = 0; i < colMap.size(); ++i) {
        if (colMap[i] < 0) continue;
        if (!colList.empty()) colList += ", ";
        colList += quoteIdentifier(dialect, header[i].toStdString());
    }

    int inserted = 0;
    QStringList errors;
    while (!parser.atEnd()) {
        const QStringList fields = parser.readRow();
        if (fields.isEmpty() || (fields.size() == 1 && fields[0].isEmpty())) continue;

        std::string values;
        for (std::size_t i = 0; i < colMap.size(); ++i) {
            if (colMap[i] < 0) continue;
            if (!values.empty()) values += ", ";
            if (i >= static_cast<std::size_t>(fields.size()) || fields[i].isNull()) {
                values += "NULL";
            } else {
                // Quote everything as string; DB coerces. Escape '.
                QString v = fields[i];
                v.replace('\'', QStringLiteral("''"));
                values += '\'';
                values += v.toStdString();
                values += '\'';
            }
        }
        const std::string sql = "INSERT INTO " + qTable + " (" + colList + ") VALUES (" + values + ")";
        try {
            state_->adapter()->executeRaw(sql);
            ++inserted;
        } catch (const GridexError& e) {
            errors << QString::fromUtf8(e.what());
            if (errors.size() > 10) { errors << "... (truncated)"; break; }
        } catch (const std::exception& e) {
            errors << QString::fromUtf8(e.what());
            if (errors.size() > 10) { errors << "... (truncated)"; break; }
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Import CSV"),
                                 tr("Inserted %1 rows into %2.").arg(inserted).arg(table));
    } else {
        QMessageBox::warning(this, tr("Import CSV"),
            tr("Inserted %1 rows with errors:\n\n%2").arg(inserted).arg(errors.join("\n")));
    }
}

// --------------------------------------------------------------------
// Backup / Restore — delegates to DatabaseDumpRunner service
// --------------------------------------------------------------------

void WorkspaceSidebar::backupDatabase() {
    if (!state_ || !state_->adapter()) return;
    const ConnectionConfig& cfg = state_->config();

    QString defName = QString::fromStdString(cfg.database.value_or(cfg.name));
    if (defName.isEmpty()) defName = QStringLiteral("backup");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Backup Database"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/") + defName + QStringLiteral(".sql"),
        tr("SQL Dump (*.sql);;All files (*)"));
    if (path.isEmpty()) return;

    std::optional<std::string> pw;
    SecretStore store;
    if (store.isAvailable()) pw = store.loadPassword(cfg.id);

    auto* dlg = new BackupProgressDialog(
        BackupProgressDialog::Mode::Backup, cfg, pw, path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->show();
    dlg->start();
}

void WorkspaceSidebar::restoreDatabase() {
    if (!state_ || !state_->adapter()) return;
    const ConnectionConfig& cfg = state_->config();

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Restore Database"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("SQL Dump (*.sql);;All files (*)"));
    if (path.isEmpty()) return;

    const auto confirm = QMessageBox::warning(
        this, tr("Restore Database"),
        tr("This will execute SQL from:\n%1\n\nExisting data may be overwritten. Continue?").arg(path),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (confirm != QMessageBox::Yes) return;

    std::optional<std::string> pw;
    SecretStore store;
    if (store.isAvailable()) pw = store.loadPassword(cfg.id);

    auto* dlg = new BackupProgressDialog(
        BackupProgressDialog::Mode::Restore, cfg, pw, path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowModality(Qt::WindowModal);
    connect(dlg, &QDialog::finished, this, [this] { loadSchemas(); });
    dlg->show();
    dlg->start();
}

void WorkspaceSidebar::promptSaveQuery(const QString& sql) {
    if (!savedQueryRepo_) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Query"), tr("Query name:"),
        QLineEdit::Normal, QString{}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString group = QInputDialog::getText(
        this, tr("Save Query"), tr("Group (leave blank for Default):"),
        QLineEdit::Normal, QString{}, &ok);
    if (!ok) return;

    AppDatabase::SavedQueryRecord rec;
    // Generate a simple UUID-like id from timestamp
    const auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    rec.id         = std::to_string(ts);
    rec.name       = name.trimmed().toStdString();
    rec.groupName  = group.trimmed().toStdString();
    rec.sql        = sql.toStdString();
    rec.createdAt  = std::chrono::system_clock::now();
    rec.updatedAt  = rec.createdAt;

    try {
        savedQueryRepo_->save(rec);
        reloadSavedQueriesTree();
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Save Failed"), QString::fromUtf8(e.what()));
    }
}

void WorkspaceSidebar::logQuery(const QString& sql, int rowCount, int elapsedMs) {
    if (appDb_) {
        AppDatabase::HistoryEntry entry;
        entry.connectionId = state_ ? state_->config().id : std::string{};
        entry.sql          = sql.toStdString();
        entry.executedAt   = std::chrono::system_clock::now();
        entry.durationMs   = elapsedMs;
        entry.rowCount     = rowCount;
        entry.succeeded    = true;
        try { appDb_->appendHistory(entry); } catch (...) {}
    }

    if (!historyList_) return;
    const QString preview = sql.simplified().left(80);
    const QString label = QStringLiteral("%1 rows · %2 ms  │  %3")
                              .arg(rowCount).arg(elapsedMs).arg(preview);
    auto* item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, sql);
    item->setToolTip(sql);
    historyList_->insertItem(0, item);
    while (historyList_->count() > 200) {
        delete historyList_->takeItem(historyList_->count() - 1);
    }
}

void WorkspaceSidebar::loadHistoryFromDb() {
    if (!appDb_ || !historyList_) return;
    historyList_->clear();
    try {
        const auto entries = appDb_->listAllHistory(200);
        for (const auto& h : entries) {
            const QString sql = QString::fromStdString(h.sql);
            const QString preview = sql.simplified().left(80);
            const QString label = QStringLiteral("%1 rows · %2 ms  │  %3")
                                      .arg(h.rowCount).arg(h.durationMs).arg(preview);
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, sql);
            item->setToolTip(sql);
            historyList_->addItem(item);
        }
    } catch (...) {}
}

void WorkspaceSidebar::reloadSavedQueriesTree() {
    if (!savedTree_ || !savedQueryRepo_) return;
    savedTree_->clear();
    try {
        const auto records = savedQueryRepo_->fetchAll();
        QMap<QString, QTreeWidgetItem*> groups;
        for (const auto& r : records) {
            const QString group = r.groupName.empty()
                ? tr("Default") : QString::fromStdString(r.groupName);
            if (!groups.contains(group)) {
                auto* gi = new QTreeWidgetItem(savedTree_);
                gi->setText(0, group);
                gi->setData(0, Qt::UserRole + 1, QStringLiteral("group"));
                gi->setData(0, Qt::UserRole + 2, group);
                groups[group] = gi;
            }
            auto* qi = new QTreeWidgetItem(groups[group]);
            qi->setText(0, QString::fromStdString(r.name));
            qi->setData(0, Qt::UserRole, QString::fromStdString(r.sql));
            qi->setData(0, Qt::UserRole + 1, QStringLiteral("query"));
            qi->setData(0, Qt::UserRole + 3, QString::fromStdString(r.id));
        }
        savedTree_->expandAll();
    } catch (...) {}
}

void WorkspaceSidebar::onSavedQueryContextMenu(const QPoint& pos) {
    auto* item = savedTree_->itemAt(pos);
    QMenu menu(this);
    const QString role = item ? item->data(0, Qt::UserRole + 1).toString() : QString{};

    if (role == QStringLiteral("query")) {
        const QString sql    = item->data(0, Qt::UserRole).toString();
        const QString qid    = item->data(0, Qt::UserRole + 3).toString();
        const QString qname  = item->text(0);

        auto* runAct = menu.addAction(tr("Run"));
        connect(runAct, &QAction::triggered, this, [this, sql] {
            emit loadSavedQueryRequested(sql);
        });

        auto* editAct = menu.addAction(tr("Edit SQL..."));
        connect(editAct, &QAction::triggered, this, [this, qid, qname, sql] {
            if (!savedQueryRepo_) return;
            bool ok = false;
            const QString newSql = QInputDialog::getMultiLineText(
                this, tr("Edit Saved Query"), tr("SQL:"), sql, &ok);
            if (!ok || newSql.trimmed().isEmpty()) return;
            try {
                auto records = savedQueryRepo_->fetchAll();
                for (auto& r : records) {
                    if (QString::fromStdString(r.id) == qid) {
                        r.sql       = newSql.toStdString();
                        r.updatedAt = std::chrono::system_clock::now();
                        savedQueryRepo_->save(r);
                        break;
                    }
                }
                reloadSavedQueriesTree();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Edit Failed"), QString::fromUtf8(e.what()));
            }
        });

        auto* delAct = menu.addAction(tr("Delete"));
        connect(delAct, &QAction::triggered, this, [this, qid, qname] {
            if (!savedQueryRepo_) return;
            const auto btn = QMessageBox::question(this, tr("Delete Saved Query"),
                tr("Delete \"%1\"?").arg(qname),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
            try {
                savedQueryRepo_->remove(qid.toStdString());
                reloadSavedQueriesTree();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Delete Failed"), QString::fromUtf8(e.what()));
            }
        });

    } else if (role == QStringLiteral("group")) {
        const QString groupName = item->data(0, Qt::UserRole + 2).toString();

        auto* renameAct = menu.addAction(tr("Rename Group..."));
        connect(renameAct, &QAction::triggered, this, [this, groupName] {
            if (!savedQueryRepo_) return;
            bool ok = false;
            const QString newName = QInputDialog::getText(
                this, tr("Rename Group"), tr("New name:"),
                QLineEdit::Normal, groupName, &ok);
            if (!ok || newName.trimmed().isEmpty()) return;
            try {
                auto records = savedQueryRepo_->fetchAll();
                for (auto& r : records) {
                    if (QString::fromStdString(r.groupName) == groupName) {
                        r.groupName = newName.toStdString();
                        r.updatedAt = std::chrono::system_clock::now();
                        savedQueryRepo_->save(r);
                    }
                }
                reloadSavedQueriesTree();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Rename Failed"), QString::fromUtf8(e.what()));
            }
        });

        auto* deleteGroupAct = menu.addAction(tr("Delete Group"));
        connect(deleteGroupAct, &QAction::triggered, this, [this, groupName] {
            if (!savedQueryRepo_) return;
            const auto btn = QMessageBox::question(this, tr("Delete Group"),
                tr("Delete group \"%1\" and move queries to Default?").arg(groupName),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (btn != QMessageBox::Yes) return;
            try {
                auto records = savedQueryRepo_->fetchAll();
                for (auto& r : records) {
                    if (QString::fromStdString(r.groupName) == groupName) {
                        r.groupName = {};
                        r.updatedAt = std::chrono::system_clock::now();
                        savedQueryRepo_->save(r);
                    }
                }
                reloadSavedQueriesTree();
            } catch (const std::exception& e) {
                QMessageBox::warning(this, tr("Delete Group Failed"), QString::fromUtf8(e.what()));
            }
        });
    }

    if (!menu.isEmpty()) menu.exec(savedTree_->viewport()->mapToGlobal(pos));
}

}
