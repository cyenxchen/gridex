#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"

namespace gridex::mcp {

namespace {

class ListTablesTool final : public MCPTool {
public:
    std::string name() const override { return "list_tables"; }
    std::string description() const override {
        return "List all tables in a database connection. Returns table names, schemas, and "
               "approximate row counts.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"schema",        {{"type", "string"}, {"description", "Optional schema/database filter"}}},
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
        std::optional<std::string> schemaFilter;
        if (params.contains("schema") && params["schema"].is_string()) {
            schemaFilter = params["schema"].get<std::string>();
        }

        auto tables = adapter->listTables(schemaFilter);

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tables) {
            nlohmann::json e = {
                {"name", t.name},
                {"type", std::string(rawValue(t.type))},
            };
            if (t.schema) e["schema"] = *t.schema;
            if (t.estimatedRowCount) e["estimated_rows"] = *t.estimatedRowCount;
            arr.push_back(std::move(e));
        }

        if (arr.empty()) {
            std::string msg = "No tables found";
            if (schemaFilter) msg += " in schema '" + *schemaFilter + "'";
            msg += ".";
            return MCPToolResult::textResult(msg);
        }
        return MCPToolResult::textResult(
            "Found " + std::to_string(arr.size()) + " table(s):\n" + arr.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeListTablesTool() {
    return std::make_shared<ListTablesTool>();
}

}  // namespace gridex::mcp
