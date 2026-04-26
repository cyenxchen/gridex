#include "Presentation/Views/MCP/MCPOverviewView.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Presentation/Views/MCP/MCPWindowState.h"

namespace gridex {

namespace {

int countMode(const QVector<MCPConnectionRow>& rows, MCPConnectionMode m) {
    int n = 0;
    for (const auto& r : rows) if (r.mode == m) ++n;
    return n;
}

QString modeColorHex(MCPConnectionMode m) {
    switch (m) {
        case MCPConnectionMode::Locked:    return "#e04b4a";
        case MCPConnectionMode::ReadOnly:  return "#378add";
        case MCPConnectionMode::ReadWrite: return "#3ea82f";
    }
    return "#888";
}

QString statusColorHex(mcp::MCPAuditStatus s) {
    switch (s) {
        case mcp::MCPAuditStatus::Success: return "#3ea82f";
        case mcp::MCPAuditStatus::Error:   return "#e04b4a";
        case mcp::MCPAuditStatus::Denied:  return "#ef9f27";
        case mcp::MCPAuditStatus::Timeout: return "#d4a017";
    }
    return "#888";
}

QString relativeTime(const std::string& iso) {
    auto dt = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    if (!dt.isValid()) return QString::fromStdString(iso);
    qint64 secs = dt.secsTo(QDateTime::currentDateTimeUtc());
    if (secs < 60)    return "just now";
    if (secs < 3600)  return QString("%1m ago").arg(secs / 60);
    if (secs < 86400) return QString("%1h ago").arg(secs / 3600);
    return QString("%1d ago").arg(secs / 86400);
}

QString formatUptime(int secs) {
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m);
    if (m > 0) return QString("%1m %2s").arg(m).arg(s);
    return QString("%1s").arg(s);
}

QFrame* makeCard(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setProperty("card", true);
    f->setFrameShape(QFrame::NoFrame);
    return f;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", "section-title");
    return l;
}

}  // namespace

MCPOverviewView::MCPOverviewView(MCPWindowState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    buildUi();
    connect(state_, &MCPWindowState::connectionsChanged, this, &MCPOverviewView::refresh);
    connect(state_, &MCPWindowState::recentActivityChanged, this, &MCPOverviewView::refresh);
    connect(state_, &MCPWindowState::serverRunningChanged, this, &MCPOverviewView::refresh);
    connect(state_, &MCPWindowState::uptimeChanged, this, &MCPOverviewView::refresh);
    refresh();
}

void MCPOverviewView::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("mcpScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    rootLayout->addWidget(scroll);

    auto* host = new QWidget(scroll);
    auto* hostH = new QHBoxLayout(host);
    hostH->setContentsMargins(24, 20, 24, 24);
    hostH->setSpacing(0);
    hostH->addStretch();

    auto* content = new QWidget(host);
    content->setMaximumWidth(900);
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(20);

    // ---- Running card ----
    auto* runningCard = makeCard(content);
    auto* runH = new QHBoxLayout(runningCard);
    runH->setContentsMargins(18, 16, 18, 16);
    runH->setSpacing(14);

    statusIcon_ = new QLabel(runningCard);
    statusIcon_->setFixedSize(42, 42);
    statusIcon_->setAlignment(Qt::AlignCenter);

    auto* titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    statusTitle_ = new QLabel(runningCard);
    QFont tf = statusTitle_->font(); tf.setBold(true); tf.setPointSizeF(tf.pointSizeF() + 1.0);
    statusTitle_->setFont(tf);
    statusDetail_ = new QLabel(runningCard);
    statusDetail_->setProperty("role", "muted");
    statusDetail_->setWordWrap(true);
    titleBox->addWidget(statusTitle_);
    titleBox->addWidget(statusDetail_);

    runH->addWidget(statusIcon_);
    runH->addLayout(titleBox, 1);
    outer->addWidget(runningCard);

    // ---- Access summary ----
    outer->addWidget(makeSectionTitle(tr("Access"), content));
    auto* accessCard = makeCard(content);
    auto* accessV = new QVBoxLayout(accessCard);
    accessV->setContentsMargins(18, 14, 18, 14);
    accessV->setSpacing(10);

    auto addAccessRow = [&](const QString& label, QLabel** out, const QString& color) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        row->addWidget(new QLabel(label, accessCard), 1);
        auto* dot = new QLabel(accessCard);
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background:%1; border-radius:4px;").arg(color));
        auto* count = new QLabel("0", accessCard);
        QFont cf = count->font(); cf.setFamily("monospace"); count->setFont(cf);
        row->addWidget(dot);
        row->addWidget(count);
        *out = count;
        accessV->addLayout(row);
    };
    addAccessRow(tr("Locked"),     &lockedCount_,    modeColorHex(MCPConnectionMode::Locked));
    addAccessRow(tr("Read-only"),  &readOnlyCount_,  modeColorHex(MCPConnectionMode::ReadOnly));
    addAccessRow(tr("Read-write"), &readWriteCount_, modeColorHex(MCPConnectionMode::ReadWrite));

    auto* manageBtn = new QPushButton(tr("Manage Connections…"), accessCard);
    manageBtn->setProperty("link", true);
    manageBtn->setCursor(Qt::PointingHandCursor);
    connect(manageBtn, &QPushButton::clicked, this, [this] { emit switchToTab(Tab::Connections); });
    accessV->addWidget(manageBtn, 0, Qt::AlignLeft);
    outer->addWidget(accessCard);

    // ---- Recent activity ----
    outer->addWidget(makeSectionTitle(tr("Recent Activity"), content));
    activityBox_ = makeCard(content);
    activityLayout_ = new QVBoxLayout(activityBox_);
    activityLayout_->setContentsMargins(18, 14, 18, 14);
    activityLayout_->setSpacing(10);
    outer->addWidget(activityBox_);

    // ---- Connections preview ----
    connectionsTitle_ = makeSectionTitle(tr("Connections"), content);
    outer->addWidget(connectionsTitle_);
    connectionsBox_ = makeCard(content);
    connectionsLayout_ = new QVBoxLayout(connectionsBox_);
    connectionsLayout_->setContentsMargins(18, 14, 18, 14);
    connectionsLayout_->setSpacing(10);
    outer->addWidget(connectionsBox_);

    outer->addStretch();
    hostH->addWidget(content, 0, Qt::AlignTop);
    hostH->addStretch();
    scroll->setWidget(host);
}

void MCPOverviewView::refresh() {
    const bool running = state_->isServerRunning();
    QString bg = running ? "rgba(62, 168, 47, 55)" : "rgba(128, 128, 128, 40)";
    QString fg = running ? "#3ea82f" : "#888";
    statusIcon_->setStyleSheet(QString(
        "background-color: %1; border-radius: 21px; color: %2; font-size: 22px; font-weight: bold;")
        .arg(bg).arg(fg));
    statusIcon_->setText(running ? QStringLiteral("✓") : QStringLiteral("⏸"));
    statusTitle_->setText(running ? tr("MCP Server is running") : tr("MCP Server is stopped"));

    if (!running) {
        statusDetail_->setText(tr("Start the server from the header to allow AI clients to access your databases."));
    } else {
        QStringList parts;
        int up = state_->uptimeSeconds();
        if (up > 0) parts << tr("Running for %1").arg(formatUptime(up));
        int active = 0;
        for (const auto& r : state_->connections()) if (r.mode != MCPConnectionMode::Locked) ++active;
        parts << tr("%1 of %2 connection%3 exposed")
                    .arg(active).arg(state_->connections().size())
                    .arg(state_->connections().size() == 1 ? "" : "s");
        statusDetail_->setText(parts.join(" · "));
    }

    lockedCount_   ->setText(QString::number(countMode(state_->connections(), MCPConnectionMode::Locked)));
    readOnlyCount_ ->setText(QString::number(countMode(state_->connections(), MCPConnectionMode::ReadOnly)));
    readWriteCount_->setText(QString::number(countMode(state_->connections(), MCPConnectionMode::ReadWrite)));

    // ---- Rebuild activity rows ----
    while (auto* item = activityLayout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    const auto& recent = state_->recentActivity();
    if (recent.empty()) {
        auto* lbl = new QLabel(tr("No activity yet"), activityBox_);
        lbl->setProperty("role", "muted");
        activityLayout_->addWidget(lbl);
    } else {
        std::size_t n = std::min<std::size_t>(recent.size(), 5);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& e = recent[i];
            auto* row = new QWidget(activityBox_);
            auto* rowH = new QHBoxLayout(row);
            rowH->setContentsMargins(0, 0, 0, 0);
            rowH->setSpacing(10);

            auto* dot = new QLabel(row);
            dot->setFixedSize(6, 6);
            dot->setStyleSheet(QString("background:%1; border-radius:3px;").arg(statusColorHex(e.result.status)));
            rowH->addWidget(dot, 0, Qt::AlignVCenter);

            auto* textCol = new QVBoxLayout();
            textCol->setSpacing(0);
            auto* primary = new QLabel(row);
            primary->setTextFormat(Qt::RichText);
            primary->setText(QString(
                "<span style='font-family:monospace;font-weight:600;'>%1</span>"
                "  <span style='color:rgba(128,128,128,200);'>· %2</span>")
                .arg(QString::fromStdString(e.tool))
                .arg(QString::fromStdString(e.client.name)));
            auto* secondary = new QLabel(row);
            secondary->setProperty("role", "hint");
            secondary->setText(QString("%1 · %2ms")
                .arg(relativeTime(e.timestampIso8601))
                .arg(e.result.durationMs));
            textCol->addWidget(primary);
            textCol->addWidget(secondary);
            rowH->addLayout(textCol, 1);

            activityLayout_->addWidget(row);
        }
        if (recent.size() > 5) {
            auto* btn = new QPushButton(tr("View All Activity…"), activityBox_);
            btn->setProperty("link", true);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this] { emit switchToTab(Tab::Activity); });
            activityLayout_->addWidget(btn, 0, Qt::AlignLeft);
        }
    }

    // ---- Rebuild connection rows ----
    while (auto* item = connectionsLayout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    const auto& conns = state_->connections();
    if (conns.isEmpty()) {
        connectionsBox_->hide();
        if (connectionsTitle_) connectionsTitle_->hide();
    } else {
        connectionsBox_->show();
        if (connectionsTitle_) connectionsTitle_->show();
        int n = std::min<int>(conns.size(), 5);
        for (int i = 0; i < n; ++i) {
            const auto& r = conns[i];
            auto* row = new QWidget(connectionsBox_);
            auto* rowH = new QHBoxLayout(row);
            rowH->setContentsMargins(0, 0, 0, 0);
            rowH->setSpacing(10);

            auto* dot = new QLabel(row);
            dot->setFixedSize(8, 8);
            dot->setStyleSheet(QString("background:%1; border-radius:4px;").arg(modeColorHex(r.mode)));
            rowH->addWidget(dot, 0, Qt::AlignVCenter);

            auto* name = new QLabel(QString::fromStdString(r.config.name), row);
            rowH->addWidget(name);
            auto* host = new QLabel(QString::fromStdString(r.config.displayHost()), row);
            host->setProperty("role", "muted");
            rowH->addWidget(host);
            rowH->addStretch();

            auto* badge = new QLabel(QString::fromStdString(std::string(displayName(r.mode))), row);
            badge->setStyleSheet(QString(
                "color:%1; font-size:11px; padding:2px 8px;"
                "background: rgba(128,128,128,25); border-radius: 4px;")
                .arg(modeColorHex(r.mode)));
            rowH->addWidget(badge);

            connectionsLayout_->addWidget(row);
        }
        if (conns.size() > 5) {
            auto* btn = new QPushButton(tr("Show all %1 connections…").arg(conns.size()), connectionsBox_);
            btn->setProperty("link", true);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this] { emit switchToTab(Tab::Connections); });
            connectionsLayout_->addWidget(btn, 0, Qt::AlignLeft);
        }
    }
}

}  // namespace gridex
