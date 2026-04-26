#include "Presentation/Views/MCP/MCPWindowState.h"

#include <QSettings>
#include <chrono>

#include "Domain/Repositories/IConnectionRepository.h"
#include "Services/MCP/MCPServer.h"

namespace gridex {

MCPWindowState::MCPWindowState(mcp::MCPServer* server,
                               IConnectionRepository* connectionRepo,
                               QObject* parent)
    : QObject(parent), server_(server), connectionRepo_(connectionRepo) {
    uptimeTimer_ = new QTimer(this);
    uptimeTimer_->setInterval(1000);
    connect(uptimeTimer_, &QTimer::timeout, this, [this] { emit uptimeChanged(); });
}

void MCPWindowState::start() {
    reloadConnections();
    reloadRecentActivity();
    uptimeTimer_->start();
}

void MCPWindowState::stop() {
    uptimeTimer_->stop();
}

int MCPWindowState::uptimeSeconds() const noexcept {
    if (!server_ || !server_->isRunning()) return 0;
    auto start = server_->startTime();
    auto now = std::chrono::system_clock::now();
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
}

bool MCPWindowState::isServerRunning() const noexcept {
    return server_ && server_->isRunning();
}

void MCPWindowState::loadModeFromSettings(const QString& id, MCPConnectionMode fallback) {
    QSettings s;
    QString key = QStringLiteral("mcp.connectionMode.") + id;
    QString raw = s.value(key).toString();
    auto parsed = mcpConnectionModeFromRaw(raw.toStdString());
    auto mode = parsed.value_or(fallback);
    if (server_) server_->setConnectionMode(id.toStdString(), mode);
}

void MCPWindowState::persistMode(const QString& id, MCPConnectionMode mode) {
    QSettings s;
    QString key = QStringLiteral("mcp.connectionMode.") + id;
    s.setValue(key, QString::fromStdString(std::string(rawValue(mode))));
}

void MCPWindowState::reloadConnections() {
    connections_.clear();
    if (!connectionRepo_) { emit connectionsChanged(); return; }
    auto configs = connectionRepo_->fetchAll();
    for (auto& c : configs) {
        QString id = QString::fromStdString(c.id);
        // Seed permission engine from QSettings (default: Locked).
        loadModeFromSettings(id, MCPConnectionMode::Locked);
        MCPConnectionRow row;
        row.config = std::move(c);
        row.mode   = server_ ? server_->getConnectionMode(row.config.id)
                              : MCPConnectionMode::Locked;
        connections_.push_back(std::move(row));
    }
    emit connectionsChanged();
}

void MCPWindowState::reloadRecentActivity() {
    if (!server_) return;
    recent_ = server_->auditLogger().recentEntries(10);
    emit recentActivityChanged();
}

void MCPWindowState::reloadFullActivity() {
    if (!server_) return;
    full_ = server_->auditLogger().recentEntries(500);
    emit fullActivityChanged();
}

void MCPWindowState::updateConnectionMode(const QString& connectionId, MCPConnectionMode mode) {
    if (!server_) return;
    server_->setConnectionMode(connectionId.toStdString(), mode);
    persistMode(connectionId, mode);
    for (auto& row : connections_) {
        if (QString::fromStdString(row.config.id) == connectionId) {
            row.mode = mode;
            break;
        }
    }
    emit connectionsChanged();
}

void MCPWindowState::toggleServer() {
    if (!server_) return;
    if (server_->isRunning()) server_->stop();
    else server_->start();
    QSettings s;
    s.setValue(QStringLiteral("mcp.enabled"), server_->isRunning());
    emit serverRunningChanged();
    emit uptimeChanged();
}

}  // namespace gridex
