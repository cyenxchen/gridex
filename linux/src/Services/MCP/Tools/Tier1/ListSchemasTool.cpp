#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

namespace {

class ListSchemasTool final : public MCPTool {
public:
    std::string name() const override { return "list_schemas"; }
    std::string description() const override {
        return "List schemas/databases available in a connection.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
            }},
            {"required", nlohmann::json::array({"connection_id"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);

        if (supportsSchemas(config.databaseType)) {
            auto schemas = adapter->listSchemas(std::nullopt);
            if (schemas.empty()) return MCPToolResult::textResult("No schemas found.");
            return MCPToolResult::textResult("Schemas: " + nlohmann::json(schemas).dump());
        }

        auto dbs = adapter->listDatabases();
        if (dbs.empty()) return MCPToolResult::textResult("No databases found.");
        return MCPToolResult::textResult("Databases: " + nlohmann::json(dbs).dump());
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeListSchemasTool() {
    return std::make_shared<ListSchemasTool>();
}

}  // namespace gridex::mcp
