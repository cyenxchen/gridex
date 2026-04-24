#include "Presentation/Views/TabBar/WorkspaceTabBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUuid>

namespace gridex {

WorkspaceTabBar::WorkspaceTabBar(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void WorkspaceTabBar::buildUi() {
    setFixedHeight(38);
    setAutoFillBackground(true);
    setObjectName(QStringLiteral("WorkspaceTabBar"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    tabsLayout_ = new QHBoxLayout();
    tabsLayout_->setContentsMargins(0, 0, 0, 0);
    tabsLayout_->setSpacing(0);
    root->addLayout(tabsLayout_);

    plusBtn_ = new QPushButton(QStringLiteral("+"), this);
    plusBtn_->setObjectName(QStringLiteral("newTabButton"));
    plusBtn_->setFixedSize(36, 38);
    plusBtn_->setCursor(Qt::PointingHandCursor);
    plusBtn_->setToolTip(tr("New Query Tab (Ctrl+Shift+N)"));
    connect(plusBtn_, &QPushButton::clicked, this, &WorkspaceTabBar::newTabRequested);
    root->addWidget(plusBtn_);

    root->addStretch();
}

QString WorkspaceTabBar::addTab(const QString& label) {
    TabInfo t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.label = label;
    tabs_.push_back(t);
    setActiveTab(t.id);
    return t.id;
}

void WorkspaceTabBar::removeTab(const QString& id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(),
                           [&](const TabInfo& t) { return t.id == id; });
    if (it == tabs_.end()) return;
    const bool wasActive = activeId_ == id;
    const auto idx = std::distance(tabs_.begin(), it);
    tabs_.erase(it);

    if (wasActive && !tabs_.empty()) {
        const int next = std::min(static_cast<int>(idx), static_cast<int>(tabs_.size()) - 1);
        setActiveTab(tabs_[next].id);
    } else if (tabs_.empty()) {
        activeId_.clear();
        rebuildTabs();
    } else {
        rebuildTabs();
    }
}

void WorkspaceTabBar::setActiveTab(const QString& id) {
    activeId_ = id;
    rebuildTabs();
    emit tabSelected(id);
}

void WorkspaceTabBar::renameTab(const QString& id, const QString& label) {
    for (auto& t : tabs_) {
        if (t.id == id) { t.label = label; break; }
    }
    rebuildTabs();
}

void WorkspaceTabBar::rebuildTabs() {
    // Remove existing tab buttons from layout (delete old widgets).
    while (tabsLayout_->count() > 0) {
        auto* item = tabsLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const auto& tab : tabs_) {
        const bool active = tab.id == activeId_;

        auto* btn = new QWidget(this);
        btn->setProperty("queryTab", true);
        btn->setProperty("active", active);
        btn->setFixedHeight(38);
        auto* h = new QHBoxLayout(btn);
        h->setContentsMargins(10, 0, 4, 0);
        h->setSpacing(5);

        // Colored connection-status dot (6×6, red when active, grey otherwise).
        auto* dot = new QLabel(btn);
        dot->setFixedSize(6, 6);
        dot->setObjectName(QStringLiteral("statusDot"));
        dot->setProperty("active", active);
        h->addWidget(dot);

        // Tab label — always visible text, bold when active.
        auto* label = new QPushButton(tab.label, btn);
        label->setFlat(true);
        label->setCursor(Qt::PointingHandCursor);
        label->setProperty("queryTabLabel", true);
        label->setProperty("active", active);
        const QString tabId = tab.id;
        connect(label, &QPushButton::clicked, this, [this, tabId] {
            setActiveTab(tabId);
        });
        h->addWidget(label);

        // Close button (×) — 18×18, flat.
        auto* closeBtn = new QPushButton(QStringLiteral("×"), btn);
        closeBtn->setFixedSize(18, 18);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setObjectName(QStringLiteral("queryTabClose"));
        connect(closeBtn, &QPushButton::clicked, this, [this, tabId] {
            emit tabCloseRequested(tabId);
        });
        h->addWidget(closeBtn);

        tabsLayout_->addWidget(btn);
    }
}

}
