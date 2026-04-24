#pragma once

// Concrete IMCPConnectionProvider wired into the App layer. Lazily opens
// database adapters for any connection ID by looking up ConnectionConfig in
// the connection repository, reading the password from the system keychain,
// and calling IDatabaseAdapter::connect().

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Services/MCP/Tools/MCPTool.h"

namespace gridex {

class IConnectionRepository;
class SecretStore;

class MCPConnectionProvider : public mcp::IMCPConnectionProvider {
public:
    MCPConnectionProvider(IConnectionRepository* repository, SecretStore* secretStore);

    std::pair<std::shared_ptr<IDatabaseAdapter>, ConnectionConfig>
        openConnection(const std::string& connectionId) override;

    bool hasConnection(const std::string& connectionId) override;
    std::vector<ConnectionConfig> listConnections() override;

private:
    IConnectionRepository* repo_;
    SecretStore*           secretStore_;
    std::mutex             mu_;
    std::unordered_map<std::string, std::shared_ptr<IDatabaseAdapter>> cache_;
};

}  // namespace gridex
