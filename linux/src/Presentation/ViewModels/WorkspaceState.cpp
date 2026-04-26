#include "Presentation/ViewModels/WorkspaceState.h"

#include <chrono>
#include <thread>
#include <utility>

#include "Core/Errors/GridexError.h"
#include "Data/Adapters/AdapterFactory.h"
#include "Data/Keychain/SecretStore.h"
#include "Services/SSH/SSHTunnelManager.h"

namespace gridex {

WorkspaceState::WorkspaceState(QObject* parent)
    : QObject(parent), tunnelMgr_(std::make_unique<SSHTunnelManager>()) {}

WorkspaceState::~WorkspaceState() { close(); }

void WorkspaceState::open(const ConnectionConfig& config,
                          const std::optional<std::string>& password,
                          SecretStore* secretStore) {
    if (adapter_) close();

    std::optional<std::string> pw = password;
    if (!pw && secretStore) {
        try { pw = secretStore->loadPassword(config.id); }
        catch (...) { /* SecretStore daemon absent — degrade */ }
    }

    // Load SSH password separately if needed.
    std::optional<std::string> sshPw;
    if (config.sshConfig && secretStore) {
        try { sshPw = secretStore->loadSSHPassword(config.id); }
        catch (...) {}
    }

    try {
        ConnectionConfig effective = config;

        // If SSH tunnel configured, establish tunnel first and rewrite host/port.
        if (config.sshConfig) {
            const auto& ssh = *config.sshConfig;
            const auto remoteHost = config.host.value_or("localhost");
            const int remotePort = config.port.value_or(defaultPort(config.databaseType));

            const auto localPort = tunnelMgr_->establish(
                config.id, ssh, remoteHost, remotePort, sshPw);

            effective.host = "127.0.0.1";
            effective.port = static_cast<int>(localPort);

            // Allow relay thread to reach accept() before the adapter connects.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        auto adapter = createAdapter(config.databaseType);
        adapter->connect(effective, pw);
        adapter_ = std::move(adapter);
        config_ = config;  // store original config (with ssh info), not effective
        emit connectionOpened();
    } catch (const GridexError& e) {
        tunnelMgr_->disconnect(config.id);
        emit connectionFailed(QString::fromUtf8(e.what()));
        throw;
    }
}

void WorkspaceState::switchDatabase(const std::string& dbName, SecretStore* secretStore) {
    if (!adapter_) return;

    // Build modified config with new database name.
    auto cfg = config_;
    cfg.database = dbName;

    // Disconnect current adapter silently (no signals).
    try { adapter_->disconnect(); } catch (...) {}
    adapter_.reset();
    // NOTE: keep tunnel alive — same SSH tunnel, just different DB.

    // Load password.
    std::optional<std::string> pw;
    if (secretStore) {
        try { pw = secretStore->loadPassword(cfg.id); } catch (...) {}
    }

    // Reconnect with new database.
    ConnectionConfig effective = cfg;
    if (cfg.sshConfig && tunnelMgr_) {
        // Tunnel already running — reuse its local port.
        auto st = tunnelMgr_->status(cfg.id);
        if (st == SSHTunnelManager::Status::Connected ||
            st == SSHTunnelManager::Status::Connecting) {
            // Tunnel still alive; adapter needs to reconnect to same local port.
            // Re-establish tunnel to get the port (idempotent — establish() disconnects old first).
            std::optional<std::string> sshPw;
            if (secretStore) {
                try { sshPw = secretStore->loadSSHPassword(cfg.id); } catch (...) {}
            }
            try {
                const auto localPort = tunnelMgr_->establish(
                    cfg.id, *cfg.sshConfig,
                    cfg.host.value_or("localhost"),
                    cfg.port.value_or(defaultPort(cfg.databaseType)),
                    sshPw);
                effective.host = "127.0.0.1";
                effective.port = static_cast<int>(localPort);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } catch (...) {
                // Tunnel re-establish failed; try direct connect.
            }
        }
    }

    auto adapter = createAdapter(cfg.databaseType);
    adapter->connect(effective, pw);
    adapter_ = std::move(adapter);
    config_ = cfg;
    // Emit databaseSwitched — NOT connectionOpened (avoids HomeView flip).
}

void WorkspaceState::close() {
    if (!adapter_) return;
    try { adapter_->disconnect(); } catch (...) {}
    adapter_.reset();
    tunnelMgr_->disconnectAll();
    emit connectionClosed();
}

}
