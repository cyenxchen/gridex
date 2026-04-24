#include "Services/MCP/Security/SQLSanitizer.h"
#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"

namespace gridex::mcp {

namespace {

bool containsWordUpper(const std::string& s, std::string_view word) {
    auto isWord = [](unsigned char c) { return std::isalnum(c) || c == '_'; };
    const std::size_t n = s.size(), m = word.size();
    for (std::size_t i = 0; i + m <= n; ++i) {
        if (i > 0 && isWord(static_cast<unsigned char>(s[i - 1]))) continue;
        if (i + m < n && isWord(static_cast<unsigned char>(s[i + m]))) continue;
        bool eq = true;
        for (std::size_t k = 0; k < m; ++k) {
            if (s[i + k] != word[k]) { eq = false; break; }
        }
        if (eq) return true;
    }
    return false;
}

class ExecuteWriteQueryTool final : public MCPTool {
public:
    std::string name() const override { return "execute_write_query"; }
    std::string description() const override {
        return "Execute one write SQL statement (INSERT/UPDATE/DELETE). Requires user approval. "
               "Only available in read-write mode. Multi-statement input is rejected.";
    }
    // Tier::Ddl because this is the broadest write tool and hits rate-limit bucket accordingly.
    MCPPermissionTier tier() const override { return MCPPermissionTier::Ddl; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"sql",           {{"type", "string"}, {"description", "A single SQL statement"}}},
                {"params",        {{"type", "array"},  {"description", "Parameters for placeholders"}}},
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

        const std::string codeOnly = stripCommentsAndStrings(sql);
        std::string upper = toUpperAscii(trimSpaces(codeOnly));

        std::string noSemi = upper;
        if (!noSemi.empty() && noSemi.back() == ';') noSemi.pop_back();
        noSemi = trimSpaces(noSemi);
        if (noSemi.find(';') != std::string::npos) {
            throw MCPToolError::invalidParameters(
                "Multiple statements are not allowed. Send one statement at a time.");
        }

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (perm.kind() == PermissionResult::Kind::Denied) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        if (startsWith(upper, "SELECT") || startsWith(upper, "WITH")) {
            throw MCPToolError::invalidParameters(
                "Use the 'query' tool for SELECT / WITH statements. This tool is for write operations only.");
        }

        if (startsWith(upper, "UPDATE") || startsWith(upper, "DELETE")) {
            if (!containsWordUpper(upper, "WHERE")) {
                throw MCPToolError::permissionDenied(
                    "UPDATE/DELETE without WHERE clause is not allowed.");
            }
            auto pos = upper.find("WHERE");
            if (pos != std::string::npos) {
                std::string whereClause = trimSpaces(codeOnly.substr(pos + 5));
                auto wr = ctx.permissionEngine->validateWhereClause(whereClause);
                if (wr.kind() == PermissionResult::Kind::Denied) {
                    throw MCPToolError::permissionDenied(wr.errorMessage());
                }
            }
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);

        if (perm.requiresUserApproval()) {
            bool approved = ctx.requestApproval(
                name(), "Execute write query", "SQL:\n" + sql, connectionId);
            if (!approved) throw MCPToolError::permissionDenied("User denied the operation.");
        }

        std::vector<RowValue> rowParams;
        if (params.contains("params") && params["params"].is_array()) {
            for (const auto& p : params["params"]) rowParams.push_back(jsonToRowValue(p));
        }

        QueryResult result = rowParams.empty()
            ? adapter->executeRaw(sql)
            : adapter->executeWithRowValues(sql, rowParams);

        ctx.recordUsage(tier(), connectionId);

        std::string msg = "Query executed successfully.";
        if (result.rowsAffected > 0) {
            msg += " " + std::to_string(result.rowsAffected) + " row(s) affected.";
        }
        return MCPToolResult::textResult(msg);
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeExecuteWriteQueryTool() {
    return std::make_shared<ExecuteWriteQueryTool>();
}

}  // namespace gridex::mcp
