//
// MCPTool.cpp
//

#include "MCPTool.h"
#include "../../../Models/ConnectionStore.h"
#include "../../../Models/CredentialManager.h"
#include "../../../Models/MCP/MCPAuditEntry.h"
#include <algorithm>

namespace DBModels
{
    std::wstring MCPTool::extractConnectionId(const nlohmann::json& params)
    {
        if (!params.contains("connection_id") || !params["connection_id"].is_string())
            throw MCPToolError::invalidParameters("connection_id is required");
        auto utf8 = params["connection_id"].get<std::string>();
        return std::wstring(utf8.begin(), utf8.end()); // ASCII-only ids (UUIDs)
    }

    std::pair<std::shared_ptr<DatabaseAdapter>, ConnectionConfig>
        MCPToolContext::getAdapter(const std::wstring& connectionId) const
    {
        // Look up the config from the connection store. Linear scan
        // is fine — typical users have <50 connections.
        auto configs = ConnectionStore::Load();
        auto it = std::find_if(configs.begin(), configs.end(),
            [&](const ConnectionConfig& c) { return c.id == connectionId; });
        if (it == configs.end())
        {
            std::string narrow(connectionId.begin(), connectionId.end());
            throw MCPToolError::connectionNotFound(narrow);
        }

        // Resolve password from Windows credential vault.
        std::wstring pass = CredentialManager::Load(connectionId);

        auto& ncPool = const_cast<MCPConnectionPool&>(pool);
        auto adapter = ncPool.acquire(*it, pass);
        return { adapter, *it };
    }

    bool MCPToolContext::requestApproval(
        const std::wstring& toolName,
        const std::wstring& description,
        const std::wstring& details,
        const std::wstring& connectionId,
        const std::wstring& clientName,
        int timeoutSeconds) const
    {
        MCPApprovalRequest req;
        req.tool = toolName;
        req.description = description;
        req.details = details;
        req.connectionId = connectionId;
        req.clientName = clientName;
        req.timeoutSeconds = timeoutSeconds;
        auto& nc = const_cast<MCPApprovalGate&>(approvalGate);
        auto future = nc.requestApproval(req);
        // Block the worker thread — caller is on a stdio/http worker,
        // not the UI thread, so this is safe.
        return future.get();
    }
}
