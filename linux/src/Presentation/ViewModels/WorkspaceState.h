#pragma once

#include <QObject>
#include <memory>
#include <optional>
#include <string>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

class SecretStore;
class SSHTunnelManager;

// Holds the currently-active database connection. Mirrors the macOS
// AppState.activeAdapter + activeConnection pair. UI listens to signals to
// switch between HomeView and WorkspaceView.
// When sshConfig is present, an SSH tunnel is established first and the adapter
// connects via localhost:<tunnelPort>.
class WorkspaceState : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceState(QObject* parent = nullptr);
    ~WorkspaceState() override;

    void open(const ConnectionConfig& config,
              const std::optional<std::string>& password = std::nullopt,
              SecretStore* secretStore = nullptr);

    // Switch database without emitting connectionClosed/Opened (stays in workspace).
    void switchDatabase(const std::string& dbName, SecretStore* secretStore = nullptr);

    void close();

    [[nodiscard]] bool isOpen() const noexcept { return adapter_ != nullptr; }
    [[nodiscard]] IDatabaseAdapter* adapter() const noexcept { return adapter_.get(); }
    [[nodiscard]] const ConnectionConfig& config() const noexcept { return config_; }

signals:
    void connectionOpened();
    void connectionClosed();
    void connectionFailed(const QString& message);

private:
    std::unique_ptr<IDatabaseAdapter> adapter_;
    std::unique_ptr<SSHTunnelManager> tunnelMgr_;
    ConnectionConfig config_;
};

}
