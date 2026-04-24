#include <algorithm>
#include <chrono>

#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"

namespace gridex::mcp {

namespace {

class QueryTool final : public MCPTool {
public:
    std::string name() const override { return "query"; }
    std::string description() const override {
        return "Execute a SQL query. In read-only mode, only SELECT statements are allowed. Returns "
               "rows with metadata (column types, row count, execution time).";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Read; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"sql",           {{"type", "string"}, {"description", "SQL query"}}},
                {"params",        {{"type", "array"},  {"description", "Parameters for placeholders"}}},
                {"row_limit",     {{"type", "integer"},
                                   {"description", "Maximum rows (default 1000, max 10000)"},
                                   {"default", 1000}, {"maximum", 10000}}},
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
        int rowLimit = 1000;
        if (params.contains("row_limit") && params["row_limit"].is_number_integer()) {
            rowLimit = std::clamp(params["row_limit"].get<int>(), 1, 10000);
        }

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto mode = ctx.permissionEngine->getMode(connectionId);
        if (mode == MCPConnectionMode::ReadOnly) {
            auto r = ctx.permissionEngine->validateReadOnlyQuery(sql);
            if (r.kind() == PermissionResult::Kind::Denied) {
                throw MCPToolError::permissionDenied(r.errorMessage());
            }
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);

        std::string finalSql = sql;
        std::string upper = toUpperAscii(sql);
        if (upper.find("LIMIT") == std::string::npos && isSQL(config.databaseType)) {
            std::string trimmed = trimSpaces(sql);
            while (!trimmed.empty() && trimmed.back() == ';') trimmed.pop_back();
            finalSql = trimmed + " LIMIT " + std::to_string(rowLimit);
        }

        std::vector<RowValue> queryParams;
        if (params.contains("params") && params["params"].is_array()) {
            for (const auto& p : params["params"]) queryParams.push_back(jsonToRowValue(p));
        }

        auto start = std::chrono::steady_clock::now();
        QueryResult result = queryParams.empty()
            ? adapter->executeRaw(finalSql)
            : adapter->executeWithRowValues(finalSql, queryParams);
        int durationMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - start).count());

        nlohmann::json cols = nlohmann::json::array();
        for (const auto& c : result.columns) {
            nlohmann::json j = {{"name", c.name}, {"type", c.dataType}};
            if (c.isNullable) j["nullable"] = true;
            cols.push_back(std::move(j));
        }

        nlohmann::json rows = nlohmann::json::array();
        std::size_t count = std::min<std::size_t>(result.rows.size(), static_cast<std::size_t>(rowLimit));
        for (std::size_t i = 0; i < count; ++i) {
            const auto& row = result.rows[i];
            nlohmann::json r = nlohmann::json::object();
            for (std::size_t k = 0; k < result.columns.size() && k < row.size(); ++k) {
                r[result.columns[k].name] = row[k].displayString();
            }
            rows.push_back(std::move(r));
        }

        nlohmann::json resp = {
            {"success", true},
            {"row_count", result.rowCount()},
            {"execution_time_ms", durationMs},
            {"columns", cols},
            {"rows", rows},
        };
        if (result.rowsAffected > 0) resp["rows_affected"] = result.rowsAffected;
        if (result.rows.size() > static_cast<std::size_t>(rowLimit)) {
            resp["truncated"] = true;
            resp["message"] = "Results truncated to " + std::to_string(rowLimit) + " rows";
        }
        return MCPToolResult::textResult(resp.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeQueryTool() {
    return std::make_shared<QueryTool>();
}

}  // namespace gridex::mcp
