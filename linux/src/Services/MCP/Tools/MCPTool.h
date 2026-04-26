#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "Core/Enums/MCPPermissionTier.h"
#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Services/MCP/Protocol.h"
#include "Services/MCP/Security/ApprovalGate.h"
#include "Services/MCP/Security/PermissionEngine.h"
#include "Services/MCP/Security/RateLimiter.h"

namespace gridex::mcp {

class AuditLogger;

// Provides on-demand database adapters for a given connection ID. MCP tools use
// this rather than taking the live app's single-adapter WorkspaceState, so tools
// can operate on any configured connection.
class IMCPConnectionProvider {
public:
    virtual ~IMCPConnectionProvider() = default;

    // Returns (adapter, config) for the given connection ID. Throws MCPToolError
    // if the connection does not exist or cannot be opened.
    virtual std::pair<std::shared_ptr<IDatabaseAdapter>, ConnectionConfig>
        openConnection(const std::string& connectionId) = 0;

    // Returns true if the connection is known (listed in the repository).
    virtual bool hasConnection(const std::string& connectionId) = 0;

    // Lists all known connections.
    virtual std::vector<ConnectionConfig> listConnections() = 0;
};

// Context bag passed to every tool invocation.
struct MCPToolContext {
    IMCPConnectionProvider* connectionProvider = nullptr;
    PermissionEngine*       permissionEngine   = nullptr;
    AuditLogger*            auditLogger        = nullptr;
    RateLimiter*            rateLimiter        = nullptr;
    ApprovalGate*           approvalGate       = nullptr;
    MCPAuditClient          client;

    std::pair<std::shared_ptr<IDatabaseAdapter>, ConnectionConfig>
        getAdapter(const std::string& connectionId) const {
        if (!connectionProvider) {
            throw MCPToolError::connectionNotFound(connectionId);
        }
        return connectionProvider->openConnection(connectionId);
    }

    PermissionResult checkPermission(MCPPermissionTier tier, const std::string& connectionId) const {
        return permissionEngine->checkPermission(tier, connectionId);
    }

    void checkRateLimit(MCPPermissionTier tier, const std::string& connectionId) const {
        auto r = rateLimiter->checkLimit(tier, connectionId);
        if (auto retry = r.retryAfterSeconds()) {
            throw MCPToolError::rateLimitExceeded(*retry);
        }
    }

    void recordUsage(MCPPermissionTier tier, const std::string& connectionId) const {
        rateLimiter->recordUsage(tier, connectionId);
    }

    bool requestApproval(const std::string& tool,
                         const std::string& description,
                         const std::string& details,
                         const std::string& connectionId) const {
        return approvalGate->requestApproval(tool, description, details, connectionId, client);
    }
};

// Tool interface. Implementations must be thread-safe — the registry hands out
// shared instances that may be invoked concurrently.
class MCPTool {
public:
    virtual ~MCPTool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual MCPPermissionTier tier() const = 0;
    virtual nlohmann::json inputSchema() const = 0;

    virtual MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) = 0;

    MCPToolDefinition definition() const {
        return {name(), description(), inputSchema()};
    }

    // Helper: extracts and UUID-validates `connection_id` from params.
    static std::string extractConnectionId(const nlohmann::json& params) {
        if (!params.contains("connection_id") || !params["connection_id"].is_string()) {
            throw MCPToolError::invalidParameters("connection_id is required");
        }
        return params["connection_id"].get<std::string>();
    }
};

}  // namespace gridex::mcp
