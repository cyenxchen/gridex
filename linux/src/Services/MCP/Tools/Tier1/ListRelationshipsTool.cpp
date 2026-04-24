#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

namespace {

class ListRelationshipsTool final : public MCPTool {
public:
    std::string name() const override { return "list_relationships"; }
    std::string description() const override {
        return "List foreign key relationships for a table. Returns both incoming and outgoing references.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table"}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "table_name"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        if (!params.contains("table_name") || !params["table_name"].is_string()) {
            throw MCPToolError::invalidParameters("table_name is required");
        }
        const std::string tableName = params["table_name"].get<std::string>();

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);
        auto outgoing = adapter->listForeignKeys(tableName, std::nullopt);

        // Incoming: scan other tables for FKs pointing back at us.
        std::vector<std::pair<std::string, ForeignKeyInfo>> incoming;  // (from_table, fk)
        try {
            auto allTables = adapter->listTables(std::nullopt);
            for (const auto& t : allTables) {
                if (t.name == tableName) continue;
                auto fks = adapter->listForeignKeys(t.name, std::nullopt);
                for (const auto& fk : fks) {
                    if (fk.referencedTable == tableName) incoming.emplace_back(t.name, fk);
                }
            }
        } catch (...) {
            // Expensive scan may fail on huge DBs; treat as no incoming.
        }

        nlohmann::json result = {{"table", tableName}};

        if (!outgoing.empty()) {
            nlohmann::json out = nlohmann::json::array();
            for (const auto& fk : outgoing) {
                out.push_back({
                    {"name", fk.name.value_or("")},
                    {"columns", fk.columns},
                    {"references_table", fk.referencedTable},
                    {"references_columns", fk.referencedColumns},
                });
            }
            result["outgoing"] = out;
        } else {
            result["outgoing"] = "None";
        }

        if (!incoming.empty()) {
            nlohmann::json in = nlohmann::json::array();
            for (const auto& [fromTable, fk] : incoming) {
                in.push_back({
                    {"from_table", fromTable},
                    {"from_columns", fk.columns},
                    {"to_columns", fk.referencedColumns},
                });
            }
            result["incoming"] = in;
        } else {
            result["incoming"] = "None";
        }

        return MCPToolResult::textResult(result.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeListRelationshipsTool() {
    return std::make_shared<ListRelationshipsTool>();
}

}  // namespace gridex::mcp
