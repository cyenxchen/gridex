#pragma once

#include <QDialog>
#include <memory>

class QLabel;
class QPushButton;
class QTabWidget;

namespace gridex {

class IConnectionRepository;
class MCPWindowState;
namespace mcp { class MCPServer; }

// Top-level MCP Server management window. Non-modal: call show().
class MCPWindow : public QDialog {
    Q_OBJECT
public:
    explicit MCPWindow(mcp::MCPServer* server,
                       IConnectionRepository* connectionRepo,
                       QWidget* parent = nullptr);
    ~MCPWindow() override;

public slots:
    void selectTab(int index);

private slots:
    void refreshStatus();
    void onToggleServerClicked();

private:
    void buildUi();

    mcp::MCPServer* server_;
    MCPWindowState* state_;  // QObject child
    QLabel*      statusTitle_ = nullptr;
    QLabel*      statusDetail_ = nullptr;
    QLabel*      statusDot_ = nullptr;
    QPushButton* toggleBtn_ = nullptr;
    QTabWidget*  tabs_ = nullptr;
};

}  // namespace gridex
