#include "App/MCP/MCPConnectionProvider.h"

#include "Data/Adapters/AdapterFactory.h"
#include "Data/Keychain/SecretStore.h"
#include "Domain/Repositories/IConnectionRepository.h"
#include "Services/MCP/Protocol.h"

namespace gridex {

MCPConnectionProvider::MCPConnectionProvider(IConnectionRepository* repo, SecretStore* secretStore)
    : repo_(repo), secretStore_(secretStore) {}

std::pair<std::shared_ptr<IDatabaseAdapter>, ConnectionConfig>
MCPConnectionProvider::openConnection(const std::string& connectionId) {
    if (!repo_) throw mcp::MCPToolError::connectionNotFound(connectionId);
    auto cfg = repo_->fetchById(connectionId);
    if (!cfg) throw mcp::MCPToolError::connectionNotFound(connectionId);

    // Fast path: reuse cached adapter.
    {
        std::lock_guard lk(mu_);
        if (auto it = cache_.find(connectionId); it != cache_.end() && it->second && it->second->isConnected()) {
            return {it->second, *cfg};
        }
    }

    std::unique_ptr<IDatabaseAdapter> u = createAdapter(cfg->databaseType);
    if (!u) throw mcp::MCPToolError::connectionNotFound(connectionId);

    std::optional<std::string> password;
    if (secretStore_) password = secretStore_->loadPassword(connectionId);

    try {
        u->connect(*cfg, password);
    } catch (const std::exception& e) {
        throw mcp::MCPToolError::connectionNotConnected(connectionId);
    }

    std::shared_ptr<IDatabaseAdapter> s(std::move(u));
    {
        std::lock_guard lk(mu_);
        cache_[connectionId] = s;
    }
    return {s, *cfg};
}

bool MCPConnectionProvider::hasConnection(const std::string& connectionId) {
    if (!repo_) return false;
    return repo_->fetchById(connectionId).has_value();
}

std::vector<ConnectionConfig> MCPConnectionProvider::listConnections() {
    if (!repo_) return {};
    return repo_->fetchAll();
}

}  // namespace gridex
