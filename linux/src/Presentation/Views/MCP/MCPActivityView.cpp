#include "Presentation/Views/MCP/MCPActivityView.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <fstream>

#include "Presentation/Views/MCP/MCPWindowState.h"
#include "Services/MCP/MCPServer.h"

namespace gridex {

namespace {

QString statusColor(mcp::MCPAuditStatus s) {
    switch (s) {
        case mcp::MCPAuditStatus::Success: return "#639922";
        case mcp::MCPAuditStatus::Error:   return "#e04b4a";
        case mcp::MCPAuditStatus::Denied:  return "#ef9f27";
        case mcp::MCPAuditStatus::Timeout: return "#d4a017";
    }
    return "#888";
}

QString tierColor(int tier) {
    switch (tier) {
        case 1: return "#378add";
        case 2: return "#639922";
        case 3: return "#ef9f27";
        case 4: return "#e04b4a";
        case 5: return "#534ab7";
    }
    return "#888";
}

QString formatTimeShort(const std::string& iso) {
    auto dt = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    if (!dt.isValid()) return QString::fromStdString(iso);
    return dt.toLocalTime().toString("HH:mm:ss");
}

QString formatTimeFull(const std::string& iso) {
    auto dt = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    if (!dt.isValid()) return QString::fromStdString(iso);
    return dt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
}

}  // namespace

MCPActivityView::MCPActivityView(MCPWindowState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    buildUi();
    connect(state_, &MCPWindowState::fullActivityChanged, this, &MCPActivityView::refresh);
    state_->reloadFullActivity();
}

void MCPActivityView::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 16, 24, 16);
    outer->setSpacing(10);

    // Filter bar
    auto* filterBar = new QHBoxLayout();
    filterBar->setSpacing(10);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search tool, SQL, or client"));
    search_->setClearButtonEnabled(true);
    search_->setObjectName(QStringLiteral("mcpSearch")); search_->setMaximumWidth(280);
    connect(search_, &QLineEdit::textChanged, this, &MCPActivityView::onFilterChanged);

    toolFilter_ = new QComboBox(this);
    toolFilter_->setFixedWidth(160);
    toolFilter_->addItem(tr("All Tools"), QString());
    connect(toolFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MCPActivityView::onFilterChanged);

    statusFilter_ = new QComboBox(this);
    statusFilter_->setFixedWidth(120);
    statusFilter_->addItem(tr("All Status"), QString());
    statusFilter_->addItem(tr("Success"), QStringLiteral("success"));
    statusFilter_->addItem(tr("Error"),   QStringLiteral("error"));
    statusFilter_->addItem(tr("Denied"),  QStringLiteral("denied"));
    statusFilter_->addItem(tr("Timeout"), QStringLiteral("timeout"));
    connect(statusFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MCPActivityView::onFilterChanged);

    auto* refreshBtn = new QToolButton(this);
    refreshBtn->setText(QStringLiteral("⟳"));
    refreshBtn->setToolTip(tr("Refresh"));
    connect(refreshBtn, &QToolButton::clicked, this, &MCPActivityView::onRefreshClicked);

    auto* menuBtn = new QToolButton(this);
    menuBtn->setText(QStringLiteral("⋯"));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    auto* m = new QMenu(menuBtn);
    m->addAction(tr("Export as JSON…"), this, &MCPActivityView::onExportClicked);
    m->addSeparator();
    m->addAction(tr("Clear Log…"), this, &MCPActivityView::onClearClicked);
    menuBtn->setMenu(m);

    filterBar->addWidget(search_);
    filterBar->addWidget(toolFilter_);
    filterBar->addWidget(statusFilter_);
    filterBar->addStretch();
    filterBar->addWidget(refreshBtn);
    filterBar->addWidget(menuBtn);
    outer->addLayout(filterBar);

    // Splitter: table | detail
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    table_ = new QTableWidget(splitter);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({tr("Time"), tr("Tool"), tr("Client"), tr("Status"), tr("Duration")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(32);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MCPActivityView::onSelectionChanged);

    detailPanel_ = new QWidget(splitter);
    detailLayout_ = new QVBoxLayout(detailPanel_);
    detailLayout_->setContentsMargins(12, 8, 12, 8);
    detailPlaceholder_ = new QLabel(tr("Select an entry to see details."), detailPanel_);
    detailPlaceholder_->setAlignment(Qt::AlignCenter);
    detailPlaceholder_->setStyleSheet("color: palette(mid);");
    detailLayout_->addWidget(detailPlaceholder_);
    detailLayout_->addStretch();

    splitter->addWidget(table_);
    splitter->addWidget(detailPanel_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    outer->addWidget(splitter, 1);
}

void MCPActivityView::onFilterChanged() { refresh(); }
void MCPActivityView::onRefreshClicked() { state_->reloadFullActivity(); }

void MCPActivityView::onExportClicked() {
    auto path = QFileDialog::getSaveFileName(this, tr("Export audit log"),
                                             QString(), tr("JSON Lines (*.jsonl);;All Files (*)"));
    if (path.isEmpty()) return;
    std::ofstream out(path.toStdString());
    if (!out) {
        QMessageBox::warning(this, tr("Export"), tr("Could not open file for writing."));
        return;
    }
    for (const auto& e : state_->fullActivity()) out << e.toJson().dump() << '\n';
}

void MCPActivityView::onClearClicked() {
    if (QMessageBox::question(this, tr("Clear audit log"),
            tr("Permanently delete the entire MCP audit log?"),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (state_->server()) state_->server()->auditLogger().clearAll();
        state_->reloadFullActivity();
    }
}

void MCPActivityView::onSelectionChanged() {
    selectedRow_ = table_->currentRow();
    rebuildDetail();
}

void MCPActivityView::refresh() {
    // Populate tool filter from unique tools
    QString keepTool = toolFilter_->currentData().toString();
    QStringList tools;
    for (const auto& e : state_->fullActivity()) {
        QString t = QString::fromStdString(e.tool);
        if (!tools.contains(t)) tools << t;
    }
    tools.sort();
    toolFilter_->blockSignals(true);
    toolFilter_->clear();
    toolFilter_->addItem(tr("All Tools"), QString());
    for (const auto& t : tools) toolFilter_->addItem(t, t);
    int idx = toolFilter_->findData(keepTool);
    if (idx >= 0) toolFilter_->setCurrentIndex(idx);
    toolFilter_->blockSignals(false);

    // Filter
    const QString needle = search_->text().trimmed().toLower();
    const QString toolF  = toolFilter_->currentData().toString();
    const QString statF  = statusFilter_->currentData().toString();

    std::vector<const mcp::MCPAuditEntry*> rows;
    for (const auto& e : state_->fullActivity()) {
        if (!toolF.isEmpty() && QString::fromStdString(e.tool) != toolF) continue;
        if (!statF.isEmpty() && QString::fromStdString(mcp::toString(e.result.status)) != statF) continue;
        if (!needle.isEmpty()) {
            bool hit = QString::fromStdString(e.tool).toLower().contains(needle)
                    || QString::fromStdString(e.client.name).toLower().contains(needle)
                    || (e.input.sqlPreview && QString::fromStdString(*e.input.sqlPreview).toLower().contains(needle));
            if (!hit) continue;
        }
        rows.push_back(&e);
    }

    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto* e = rows[i];
        auto* timeItem = new QTableWidgetItem(formatTimeShort(e->timestampIso8601));
        auto* toolItem = new QTableWidgetItem(QString::fromStdString(e->tool));
        toolItem->setForeground(QColor(tierColor(e->tier)));
        auto* clientItem = new QTableWidgetItem(QString::fromStdString(e->client.name));
        auto* statusItem = new QTableWidgetItem(QString::fromStdString(mcp::toString(e->result.status)));
        statusItem->setForeground(QColor(statusColor(e->result.status)));
        auto* durItem = new QTableWidgetItem(QString("%1ms").arg(e->result.durationMs));

        table_->setItem(i, 0, timeItem);
        table_->setItem(i, 1, toolItem);
        table_->setItem(i, 2, clientItem);
        table_->setItem(i, 3, statusItem);
        table_->setItem(i, 4, durItem);
    }
    selectedRow_ = -1;
    rebuildDetail();
}

void MCPActivityView::rebuildDetail() {
    while (auto* item = detailLayout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(state_->fullActivity().size())) {
        detailPlaceholder_ = new QLabel(tr("Select an entry to see details."), detailPanel_);
        detailPlaceholder_->setAlignment(Qt::AlignCenter);
        detailPlaceholder_->setStyleSheet("color: palette(mid);");
        detailLayout_->addWidget(detailPlaceholder_);
        detailLayout_->addStretch();
        return;
    }

    // We built table_ rows in the same order as filtered entries. Re-run filter
    // quickly to find the selected one.
    int k = 0;
    const mcp::MCPAuditEntry* selected = nullptr;
    const QString needle = search_->text().trimmed().toLower();
    const QString toolF  = toolFilter_->currentData().toString();
    const QString statF  = statusFilter_->currentData().toString();
    for (const auto& e : state_->fullActivity()) {
        if (!toolF.isEmpty() && QString::fromStdString(e.tool) != toolF) continue;
        if (!statF.isEmpty() && QString::fromStdString(mcp::toString(e.result.status)) != statF) continue;
        if (!needle.isEmpty()) {
            bool hit = QString::fromStdString(e.tool).toLower().contains(needle)
                    || QString::fromStdString(e.client.name).toLower().contains(needle)
                    || (e.input.sqlPreview && QString::fromStdString(*e.input.sqlPreview).toLower().contains(needle));
            if (!hit) continue;
        }
        if (k == selectedRow_) { selected = &e; break; }
        ++k;
    }
    if (!selected) return;

    auto makeBox = [&](const QString& title) {
        auto* gb = new QGroupBox(title, detailPanel_);
        gb->setFlat(true);
        return gb;
    };

    auto addRow = [&](QFormLayout* form, const QString& k, const QString& v, bool mono = false) {
        auto* val = new QLabel(v);
        val->setWordWrap(true);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (mono) {
            QFont f = val->font(); f.setFamily("monospace"); val->setFont(f);
        }
        form->addRow(k, val);
    };

    // Event
    {
        auto* gb = makeBox(tr("Event"));
        auto* f = new QFormLayout(gb);
        addRow(f, tr("Tool"), QString::fromStdString(selected->tool), true);
        addRow(f, tr("Tier"), QString("Tier %1").arg(selected->tier));
        addRow(f, tr("Time"), formatTimeFull(selected->timestampIso8601));
        addRow(f, tr("Event ID"), QString::fromStdString(selected->eventId.substr(0, 8)) + "…", true);
        detailLayout_->addWidget(gb);
    }
    // Client
    {
        auto* gb = makeBox(tr("Client"));
        auto* f = new QFormLayout(gb);
        addRow(f, tr("Name"),      QString::fromStdString(selected->client.name));
        addRow(f, tr("Version"),   QString::fromStdString(selected->client.version));
        addRow(f, tr("Transport"), QString::fromStdString(selected->client.transport));
        detailLayout_->addWidget(gb);
    }
    // Connection
    if (selected->connectionId || selected->connectionType) {
        auto* gb = makeBox(tr("Connection"));
        auto* f = new QFormLayout(gb);
        if (selected->connectionType) addRow(f, tr("Database"), QString::fromStdString(*selected->connectionType));
        if (selected->connectionId)   addRow(f, tr("ID"), QString::fromStdString(selected->connectionId->substr(0, 8)) + "…", true);
        detailLayout_->addWidget(gb);
    }
    // Result
    {
        auto* gb = makeBox(tr("Result"));
        auto* f = new QFormLayout(gb);
        QString st = QString::fromStdString(mcp::toString(selected->result.status));
        st[0] = st[0].toUpper();
        auto* statusLbl = new QLabel(st);
        statusLbl->setStyleSheet(QString("color: %1;").arg(statusColor(selected->result.status)));
        f->addRow(tr("Status"), statusLbl);
        addRow(f, tr("Duration"), QString("%1ms").arg(selected->result.durationMs));
        if (selected->result.rowsReturned) addRow(f, tr("Rows returned"), QString::number(*selected->result.rowsReturned));
        if (selected->result.rowsAffected) addRow(f, tr("Rows affected"), QString::number(*selected->result.rowsAffected));
        detailLayout_->addWidget(gb);
    }
    // SQL
    if (selected->input.sqlPreview) {
        auto* gb = makeBox(tr("SQL"));
        auto* v = new QVBoxLayout(gb);
        auto* tb = new QTextBrowser(gb);
        tb->setPlainText(QString::fromStdString(*selected->input.sqlPreview));
        tb->setMaximumHeight(160);
        QFont f = tb->font(); f.setFamily("monospace"); tb->setFont(f);
        v->addWidget(tb);
        detailLayout_->addWidget(gb);
    }
    // Error
    if (selected->error) {
        auto* gb = makeBox(tr("Error"));
        auto* v = new QVBoxLayout(gb);
        auto* lbl = new QLabel(QString::fromStdString(*selected->error), gb);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("color: #e04b4a;");
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(lbl);
        detailLayout_->addWidget(gb);
    }

    detailLayout_->addStretch();
}

}  // namespace gridex
