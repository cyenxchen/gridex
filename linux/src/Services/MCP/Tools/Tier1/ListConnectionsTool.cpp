#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

namespace {

class ListConnectionsTool final : public MCPTool {
public:
    std::string name() const override { return "list_connections"; }
    std::string description() const override {
        return "List all configured database connections. Returns connection IDs, names, and types "
               "(postgres/mysql/sqlite/redis/mongodb/mssql).";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {{"type", "object"}, {"properties", nlohmann::json::object()}};
    }

    MCPToolResult execute(const nlohmann::json&, const MCPToolContext& ctx) override {
        if (!ctx.connectionProvider) {
            return MCPToolResult::errorResult("No connection provider available.");
        }
        auto configs = ctx.connectionProvider->listConnections();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& c : configs) {
            auto mode = ctx.permissionEngine->getMode(c.id);
            if (mode == MCPConnectionMode::Locked) continue;
            nlohmann::json entry = {
                {"id",           c.id},
                {"name",         c.name},
                {"type",         std::string(rawValue(c.databaseType))},
                {"host",         c.displayHost()},
                {"database",     c.database.value_or("")},
                {"is_connected", false},   // pool state is opaque to MCP
                {"mcp_mode",     std::string(rawValue(mode))},
            };
            arr.push_back(std::move(entry));
        }

        if (arr.empty()) {
            return MCPToolResult::textResult(
                "No connections available for MCP access. All connections are either locked or none exist.");
        }
        return MCPToolResult::textResult(arr.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeListConnectionsTool() {
    return std::make_shared<ListConnectionsTool>();
}

}  // namespace gridex::mcp
