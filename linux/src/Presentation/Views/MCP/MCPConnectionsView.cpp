#include "Presentation/Views/MCP/MCPConnectionsView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Presentation/Views/MCP/MCPWindowState.h"

namespace gridex {

namespace {

QString displayNameQ(MCPConnectionMode m) {
    return QString::fromStdString(std::string(displayName(m)));
}

QString descriptionQ(MCPConnectionMode m) {
    return QString::fromStdString(std::string(description(m)));
}

}  // namespace

MCPConnectionsView::MCPConnectionsView(MCPWindowState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    buildUi();
    connect(state_, &MCPWindowState::connectionsChanged, this, &MCPConnectionsView::refresh);
    refresh();
}

void MCPConnectionsView::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 16, 24, 16);
    outer->setSpacing(10);

    // Filter bar
    auto* filterBar = new QHBoxLayout();
    filterBar->setSpacing(10);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("mcpSearch"));
    search_->setPlaceholderText(tr("Search connections"));
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(300);
    connect(search_, &QLineEdit::textChanged, this, &MCPConnectionsView::onFilterChanged);

    modeFilter_ = new QComboBox(this);
    modeFilter_->addItem(tr("All Access"), QString());
    for (auto m : kAllMCPConnectionModes) {
        modeFilter_->addItem(displayNameQ(m),
                             QString::fromStdString(std::string(rawValue(m))));
    }
    modeFilter_->setFixedWidth(140);
    connect(modeFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MCPConnectionsView::onFilterChanged);

    countLabel_ = new QLabel(this);
    countLabel_->setStyleSheet("color: palette(mid);");

    filterBar->addWidget(search_);
    filterBar->addWidget(modeFilter_);
    filterBar->addStretch();
    filterBar->addWidget(countLabel_);
    outer->addLayout(filterBar);

    // Table
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Host"), tr("Access"), tr("Description")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    outer->addWidget(table_, 1);
}

void MCPConnectionsView::onFilterChanged() { refresh(); }

void MCPConnectionsView::onRowModeChanged(int row, int) {
    auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, 3));
    if (!combo) return;
    QString id = combo->property("connId").toString();
    QString modeRaw = combo->currentData().toString();
    auto parsed = mcpConnectionModeFromRaw(modeRaw.toStdString());
    if (parsed) state_->updateConnectionMode(id, *parsed);
}

void MCPConnectionsView::refresh() {
    const QString needle = search_->text().trimmed().toLower();
    const QString filterRaw = modeFilter_->currentData().toString();

    // Build filtered list
    QVector<MCPConnectionRow> rows;
    for (const auto& r : state_->connections()) {
        if (!needle.isEmpty()) {
            bool hit = QString::fromStdString(r.config.name).toLower().contains(needle)
                    || QString::fromStdString(r.config.displayHost()).toLower().contains(needle);
            if (!hit) continue;
        }
        if (!filterRaw.isEmpty()) {
            if (QString::fromStdString(std::string(rawValue(r.mode))) != filterRaw) continue;
        }
        rows.push_back(r);
    }

    countLabel_->setText(tr("%1 of %2 connections").arg(rows.size()).arg(state_->connections().size()));

    table_->blockSignals(true);
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(r.config.name));
        auto* typeItem = new QTableWidgetItem(QString::fromStdString(std::string(displayName(r.config.databaseType))));
        auto* hostItem = new QTableWidgetItem(QString::fromStdString(r.config.displayHost()));
        auto* descItem = new QTableWidgetItem(descriptionQ(r.mode));

        table_->setItem(i, 0, nameItem);
        table_->setItem(i, 1, typeItem);
        table_->setItem(i, 2, hostItem);
        table_->setItem(i, 4, descItem);

        auto* combo = new QComboBox(table_);
        combo->setProperty("connId", QString::fromStdString(r.config.id));
        for (auto m : kAllMCPConnectionModes) {
            combo->addItem(displayNameQ(m),
                           QString::fromStdString(std::string(rawValue(m))));
            if (m == r.mode) combo->setCurrentIndex(combo->count() - 1);
        }
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int idx) { onRowModeChanged(i, idx); });
        table_->setCellWidget(i, 3, combo);
    }
    table_->blockSignals(false);
}

}  // namespace gridex
