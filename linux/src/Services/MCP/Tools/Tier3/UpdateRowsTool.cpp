#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"
#include "Services/MCP/Security/IdentifierValidator.h"
#include "Services/MCP/Security/RowCountEstimator.h"

namespace gridex::mcp {

namespace {

class UpdateRowsTool final : public MCPTool {
public:
    std::string name() const override { return "update_rows"; }
    std::string description() const override {
        return "Update rows matching WHERE clause. Requires user approval. WHERE clause is "
               "MANDATORY (no bare UPDATE).";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Write; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table"}}},
                {"schema",        {{"type", "string"}, {"description", "Optional schema name"}}},
                {"set",           {{"type", "object"}, {"description", "Column-value pairs to update"}}},
                {"where",         {{"type", "string"}, {"description", "WHERE clause (required)"}}},
                {"where_params",  {{"type", "array"},  {"description", "Parameters for WHERE clause"}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "table_name", "set", "where"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        auto [tableName, schemaName] = IdentifierValidator::extractTableAndSchema(params);

        if (!params.contains("set") || !params["set"].is_object() || params["set"].empty()) {
            throw MCPToolError::invalidParameters(
                "set must be a non-empty object with column-value pairs");
        }
        const auto& setObj = params["set"];
        for (auto it = setObj.begin(); it != setObj.end(); ++it) {
            IdentifierValidator::validate(it.key(), "column name");
        }

        if (!params.contains("where") || !params["where"].is_string()) {
            throw MCPToolError::invalidParameters(
                "where clause is required. Bare UPDATE without WHERE is not allowed.");
        }
        const std::string whereClause = params["where"].get<std::string>();

        auto whereCheck = ctx.permissionEngine->validateWhereClause(whereClause);
        if (whereCheck.kind() == PermissionResult::Kind::Denied) {
            throw MCPToolError::permissionDenied(whereCheck.errorMessage());
        }

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (perm.kind() == PermissionResult::Kind::Denied) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);
        SQLDialect dialect = sqlDialect(config.databaseType);
        std::string qualifiedTable = qualifiedIdentifier(dialect, tableName, schemaName);

        std::string setClause;
        bool first = true;
        for (auto it = setObj.begin(); it != setObj.end(); ++it) {
            if (!first) setClause += ", ";
            first = false;
            setClause += quoteIdentifier(dialect, it.key()) + " = "
                       + formatValueForSQL(it.value(), dialect);
        }
        std::string updateSql = "UPDATE " + qualifiedTable + " SET " + setClause
                              + " WHERE " + whereClause;

        int estimated = RowCountEstimator::estimate(*adapter, qualifiedTable, whereClause, config);

        if (perm.requiresUserApproval()) {
            std::string details = "SQL: " + updateSql + "\n\nEstimated rows affected: "
                                + std::to_string(estimated);
            bool approved = ctx.requestApproval(
                name(),
                "Update rows in '" + tableName + "' where " + whereClause,
                details,
                connectionId);
            if (!approved) throw MCPToolError::permissionDenied("User denied the operation.");
        }

        auto result = adapter->executeRaw(updateSql);
        ctx.recordUsage(tier(), connectionId);
        return MCPToolResult::textResult(
            "Successfully updated " + std::to_string(result.rowsAffected)
            + " row(s) in '" + tableName + "'.");
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeUpdateRowsTool() {
    return std::make_shared<UpdateRowsTool>();
}

}  // namespace gridex::mcp
