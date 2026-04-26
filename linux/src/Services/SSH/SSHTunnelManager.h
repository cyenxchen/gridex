#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex {

// SSH local port forwarding using libssh.
// establish() opens an SSH session, binds a free local TCP port, and forwards
// traffic through the SSH tunnel to remoteHost:remotePort. The returned local
// port can be passed to the database adapter as "localhost:<port>".
//
// Each tunnel runs a dedicated background thread (select loop). A single TCP
// client is served at a time — sufficient for one-adapter-per-connection.
class SSHTunnelManager {
public:
    enum class Status { Connecting, Connected, Disconnected, Error };

    SSHTunnelManager();
    ~SSHTunnelManager();

    SSHTunnelManager(const SSHTunnelManager&) = delete;
    SSHTunnelManager& operator=(const SSHTunnelManager&) = delete;

    // Opens the tunnel and returns the local port the adapter should connect to.
    // Throws gridex::ConnectionError on failure.
    std::uint16_t establish(const std::string& connectionId,
                            const SSHTunnelConfig& sshConfig,
                            const std::string& remoteHost,
                            int remotePort,
                            const std::optional<std::string>& sshPassword);

    void disconnect(const std::string& connectionId);
    void disconnectAll();

    [[nodiscard]] Status status(const std::string& connectionId) const;

    struct Tunnel;  // defined in .cpp
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Tunnel>> tunnels_;
};

}
