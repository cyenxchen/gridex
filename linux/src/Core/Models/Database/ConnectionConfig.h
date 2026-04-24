#pragma once

#include <optional>
#include <string>
#include <utility>

#include "Core/Enums/ColorTag.h"
#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SSHAuthMethod.h"

namespace gridex {

struct SSHTunnelConfig {
    std::string host;
    int port = 22;
    std::string username;
    SSHAuthMethod authMethod = SSHAuthMethod::Password;
    std::optional<std::string> keyPath;

    bool operator==(const SSHTunnelConfig&) const noexcept = default;
};

struct ConnectionConfig {
    std::string id;                              // UUID string
    std::string name;
    DatabaseType databaseType = DatabaseType::PostgreSQL;
    std::optional<std::string> host;
    std::optional<int> port;
    std::optional<std::string> database;
    std::optional<std::string> username;
    bool sslEnabled = false;
    std::optional<ColorTag> colorTag;
    std::optional<std::string> group;

    // SQLite-specific
    std::optional<std::string> filePath;

    // SSH tunnel
    std::optional<SSHTunnelConfig> sshConfig;

    bool operator==(const ConnectionConfig&) const noexcept = default;

    [[nodiscard]] std::string displayHost() const {
        if (databaseType == DatabaseType::SQLite) {
            return filePath.value_or("Unknown");
        }
        const auto h = host.value_or(std::string{"localhost"});
        const int p = port.value_or(defaultPort(databaseType));
        return h + ":" + std::to_string(p);
    }
};

}
