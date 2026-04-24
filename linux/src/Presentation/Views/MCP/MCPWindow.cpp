#include "Presentation/Views/MCP/MCPWindow.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "Presentation/Views/MCP/MCPActivityView.h"
#include "Presentation/Views/MCP/MCPAdvancedView.h"
#include "Presentation/Views/MCP/MCPConnectionsView.h"
#include "Presentation/Views/MCP/MCPOverviewView.h"
#include "Presentation/Views/MCP/MCPSetupView.h"
#include "Presentation/Views/MCP/MCPStyle.h"
#include "Presentation/Views/MCP/MCPWindowState.h"
#include "Services/MCP/MCPServer.h"

namespace gridex {

MCPWindow::MCPWindow(mcp::MCPServer* server,
                     IConnectionRepository* repo,
                     QWidget* parent)
    : QDialog(parent), server_(server) {
    setObjectName(QStringLiteral("mcpRoot"));
    setWindowTitle(tr("MCP Server"));
    setModal(false);
    resize(1000, 700);
    setMinimumSize(860, 580);

    // Apply the MCP stylesheet to the whole subtree.
    setStyleSheet(kMCPStyleSheet);

    state_ = new MCPWindowState(server_, repo, this);
    buildUi();
    state_->start();
    refreshStatus();
}

MCPWindow::~MCPWindow() { state_->stop(); }

void MCPWindow::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---------------- Header ----------------
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("mcpHeader"));
    auto* hL = new QHBoxLayout(header);
    hL->setContentsMargins(24, 14, 24, 14);
    hL->setSpacing(12);

    statusDot_ = new QLabel(header);
    statusDot_->setObjectName(QStringLiteral("mcpStatusDot"));
    statusDot_->setFixedSize(24, 24);
    statusDot_->setAlignment(Qt::AlignCenter);

    auto* titles = new QVBoxLayout();
    titles->setSpacing(0);
    titles->setContentsMargins(0, 0, 0, 0);
    statusTitle_ = new QLabel(tr("MCP Server"), header);
    statusTitle_->setObjectName(QStringLiteral("mcpHeaderTitle"));
    statusDetail_ = new QLabel(header);
    statusDetail_->setObjectName(QStringLiteral("mcpHeaderDetail"));
    titles->addWidget(statusTitle_);
    titles->addWidget(statusDetail_);

    toggleBtn_ = new QPushButton(tr("Start Server"), header);
    toggleBtn_->setMinimumWidth(120);
    toggleBtn_->setCursor(Qt::PointingHandCursor);
    toggleBtn_->setProperty("accent", true);
    connect(toggleBtn_, &QPushButton::clicked, this, &MCPWindow::onToggleServerClicked);

    hL->addWidget(statusDot_);
    hL->addLayout(titles, 1);
    hL->addStretch();
    hL->addWidget(toggleBtn_);
    outer->addWidget(header);

    // ---------------- Tabs ----------------
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("mcpTabs"));
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setExpanding(false);

    auto* overview    = new MCPOverviewView(state_, tabs_);
    auto* connections = new MCPConnectionsView(state_, tabs_);
    auto* activity    = new MCPActivityView(state_, tabs_);
    auto* setup       = new MCPSetupView(tabs_);
    auto* advanced    = new MCPAdvancedView(state_, tabs_);
    tabs_->addTab(overview,    tr("Overview"));
    tabs_->addTab(connections, tr("Connections"));
    tabs_->addTab(activity,    tr("Activity"));
    tabs_->addTab(setup,       tr("Setup"));
    tabs_->addTab(advanced,    tr("Config"));
    connect(overview, &MCPOverviewView::switchToTab, this, [this](MCPOverviewView::Tab t) {
        tabs_->setCurrentIndex(static_cast<int>(t));
    });
    outer->addWidget(tabs_, 1);

    connect(state_, &MCPWindowState::serverRunningChanged, this, &MCPWindow::refreshStatus);
    connect(state_, &MCPWindowState::uptimeChanged,        this, &MCPWindow::refreshStatus);
}

void MCPWindow::onToggleServerClicked() { state_->toggleServer(); }

void MCPWindow::selectTab(int index) { if (tabs_) tabs_->setCurrentIndex(index); }

void MCPWindow::refreshStatus() {
    const bool running = state_->isServerRunning();

    // The status dot uses a dynamic property so QSS can swap the background.
    statusDot_->setProperty("running", running);
    statusDot_->style()->unpolish(statusDot_);
    statusDot_->style()->polish(statusDot_);

    // Small inner pip painted via text (no pixmap needed).
    statusDot_->setText(QString("<span style='color:%1; font-size:14px;'>●</span>")
                            .arg(running ? "#3ea82f" : "#888"));

    if (!running) {
        statusDetail_->setText(tr("Server is stopped"));
    } else {
        int up = state_->uptimeSeconds();
        if (up > 0) {
            int h = up / 3600, m = (up % 3600) / 60, s = up % 60;
            QString uptime = h > 0 ? QString("%1h %2m").arg(h).arg(m)
                            : m > 0 ? QString("%1m %2s").arg(m).arg(s)
                                    : QString("%1s").arg(s);
            statusDetail_->setText(tr("Running · %1").arg(uptime));
        } else {
            statusDetail_->setText(tr("Running"));
        }
    }

    toggleBtn_->setText(running ? tr("Stop Server") : tr("Start Server"));
    toggleBtn_->setProperty("accent", !running);
    toggleBtn_->setProperty("danger", running);
    toggleBtn_->style()->unpolish(toggleBtn_);
    toggleBtn_->style()->polish(toggleBtn_);
}

}  // namespace gridex
