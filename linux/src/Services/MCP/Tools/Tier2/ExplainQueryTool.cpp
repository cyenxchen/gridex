#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

namespace {

class ExplainQueryTool final : public MCPTool {
public:
    std::string name() const override { return "explain_query"; }
    std::string description() const override {
        return "Get EXPLAIN plan for a query without executing it. Helps AI understand query performance.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Read; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"sql",           {{"type", "string"}, {"description", "SQL query to explain"}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "sql"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        if (!params.contains("sql") || !params["sql"].is_string()) {
            throw MCPToolError::invalidParameters("sql is required");
        }
        const std::string sql = params["sql"].get<std::string>();

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);

        std::string explainSql;
        switch (config.databaseType) {
            case DatabaseType::PostgreSQL:
                explainSql = "EXPLAIN (ANALYZE false, COSTS true, FORMAT TEXT) " + sql;
                break;
            case DatabaseType::MySQL:
                explainSql = "EXPLAIN " + sql;
                break;
            case DatabaseType::SQLite:
                explainSql = "EXPLAIN QUERY PLAN " + sql;
                break;
            case DatabaseType::MSSQL:
                explainSql = "SET SHOWPLAN_TEXT ON; " + sql + "; SET SHOWPLAN_TEXT OFF";
                break;
            case DatabaseType::MongoDB:
            case DatabaseType::Redis:
                return MCPToolResult::textResult(
                    std::string("EXPLAIN is not supported for ")
                    + std::string(displayName(config.databaseType)) + " connections.",
                    true);
        }

        auto result = adapter->executeRaw(explainSql);
        std::string out = "Query Plan for: " + sql + "\n\n";
        if (result.rows.empty()) {
            out += "No plan information available.";
        } else {
            for (const auto& row : result.rows) {
                for (std::size_t i = 0; i < row.size(); ++i) {
                    if (i > 0) out += " | ";
                    out += row[i].displayString();
                }
                out += "\n";
            }
        }
        return MCPToolResult::textResult(out);
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeExplainQueryTool() {
    return std::make_shared<ExplainQueryTool>();
}

}  // namespace gridex::mcp
