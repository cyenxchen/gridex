#pragma once

// State holder for the MCP window. Lives for the lifetime of the window,
// drives the 5 tab views via Qt signals.

#include <QObject>
#include <QTimer>
#include <QVector>
#include <chrono>
#include <vector>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Enums/MCPConnectionMode.h"
#include "Services/MCP/Protocol.h"

namespace gridex {
class IConnectionRepository;
namespace mcp { class MCPServer; }

struct MCPConnectionRow {
    ConnectionConfig config;
    MCPConnectionMode mode = MCPConnectionMode::Locked;
};

class MCPWindowState : public QObject {
    Q_OBJECT
public:
    MCPWindowState(mcp::MCPServer* server,
                   IConnectionRepository* connectionRepo,
                   QObject* parent = nullptr);

    void start();
    void stop();

    const QVector<MCPConnectionRow>& connections() const noexcept { return connections_; }
    const std::vector<mcp::MCPAuditEntry>& recentActivity() const noexcept { return recent_; }
    const std::vector<mcp::MCPAuditEntry>& fullActivity() const noexcept { return full_; }

    int uptimeSeconds() const noexcept;
    bool isServerRunning() const noexcept;

    mcp::MCPServer* server() const noexcept { return server_; }

public slots:
    void reloadConnections();
    void reloadRecentActivity();
    void reloadFullActivity();
    void updateConnectionMode(const QString& connectionId, MCPConnectionMode mode);
    void toggleServer();

signals:
    void connectionsChanged();
    void recentActivityChanged();
    void fullActivityChanged();
    void uptimeChanged();
    void serverRunningChanged();

private:
    void loadModeFromSettings(const QString& id, MCPConnectionMode fallback);
    void persistMode(const QString& id, MCPConnectionMode mode);

    mcp::MCPServer* server_;
    IConnectionRepository* connectionRepo_;
    QVector<MCPConnectionRow> connections_;
    std::vector<mcp::MCPAuditEntry> recent_;
    std::vector<mcp::MCPAuditEntry> full_;
    QTimer* uptimeTimer_ = nullptr;
};

}  // namespace gridex
