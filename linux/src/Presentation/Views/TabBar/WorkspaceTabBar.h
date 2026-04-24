#pragma once

#include <QWidget>
#include <QString>
#include <vector>

class QHBoxLayout;
class QPushButton;
class QScrollArea;

namespace gridex {

// Multi-tab bar matching macOS TabBarSwiftUIView:
//   [Tab1 ×] [Tab2 ×] [+]  ...spacer...
// Active tab highlighted with bottom accent bar. Each tab has a close button.
class WorkspaceTabBar : public QWidget {
    Q_OBJECT

public:
    struct TabInfo {
        QString id;
        QString label;
    };

    explicit WorkspaceTabBar(QWidget* parent = nullptr);

    // Returns the new tab's id.
    QString addTab(const QString& label);
    void removeTab(const QString& id);
    void setActiveTab(const QString& id);
    void renameTab(const QString& id, const QString& label);
    [[nodiscard]] QString activeTabId() const { return activeId_; }
    [[nodiscard]] int tabCount() const { return static_cast<int>(tabs_.size()); }

signals:
    void newTabRequested();
    void tabSelected(const QString& id);
    void tabCloseRequested(const QString& id);

private:
    void buildUi();
    void rebuildTabs();

    std::vector<TabInfo> tabs_;
    QString activeId_;
    QHBoxLayout* tabsLayout_ = nullptr;
    QPushButton* plusBtn_     = nullptr;
    int nextSeq_ = 1;
};

}
