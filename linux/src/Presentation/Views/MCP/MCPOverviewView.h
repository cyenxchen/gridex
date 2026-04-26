#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace gridex {

class MCPWindowState;

class MCPOverviewView : public QWidget {
    Q_OBJECT
public:
    enum class Tab { Overview, Connections, Activity, Setup, Config };

    explicit MCPOverviewView(MCPWindowState* state, QWidget* parent = nullptr);

signals:
    void switchToTab(Tab tab);

private slots:
    void refresh();

private:
    void buildUi();

    MCPWindowState* state_;
    QLabel* statusIcon_ = nullptr;
    QLabel* statusTitle_ = nullptr;
    QLabel* statusDetail_ = nullptr;
    QLabel* lockedCount_ = nullptr;
    QLabel* readOnlyCount_ = nullptr;
    QLabel* readWriteCount_ = nullptr;
    QFrame* activityBox_ = nullptr;
    QVBoxLayout* activityLayout_ = nullptr;
    QFrame* connectionsBox_ = nullptr;
    QVBoxLayout* connectionsLayout_ = nullptr;
    QLabel* connectionsTitle_ = nullptr;
};

}  // namespace gridex
