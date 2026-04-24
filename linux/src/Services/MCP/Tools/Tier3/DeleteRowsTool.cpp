#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"
#include "Services/MCP/Security/IdentifierValidator.h"
#include "Services/MCP/Security/RowCountEstimator.h"

namespace gridex::mcp {

namespace {

class DeleteRowsTool final : public MCPTool {
public:
    std::string name() const override { return "delete_rows"; }
    std::string description() const override {
        return "Delete rows matching WHERE clause. Requires user approval. WHERE clause is MANDATORY.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Write; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table"}}},
                {"schema",        {{"type", "string"}, {"description", "Optional schema name"}}},
                {"where",         {{"type", "string"}, {"description", "WHERE clause (required)"}}},
                {"where_params",  {{"type", "array"},  {"description", "Parameters for WHERE clause"}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "table_name", "where"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        auto [tableName, schemaName] = IdentifierValidator::extractTableAndSchema(params);

        if (!params.contains("where") || !params["where"].is_string()) {
            throw MCPToolError::invalidParameters(
                "where clause is required. Bare DELETE without WHERE is not allowed.");
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
        std::string deleteSql = "DELETE FROM " + qualifiedTable + " WHERE " + whereClause;

        int estimated = RowCountEstimator::estimate(*adapter, qualifiedTable, whereClause, config);

        if (perm.requiresUserApproval()) {
            std::string details = "SQL: " + deleteSql + "\n\nEstimated rows to delete: "
                                + std::to_string(estimated)
                                + "\n\nThis operation cannot be undone!";
            bool approved = ctx.requestApproval(
                name(),
                "Delete rows from '" + tableName + "' where " + whereClause,
                details,
                connectionId);
            if (!approved) throw MCPToolError::permissionDenied("User denied the operation.");
        }

        auto result = adapter->executeRaw(deleteSql);
        ctx.recordUsage(tier(), connectionId);
        return MCPToolResult::textResult(
            "Successfully deleted " + std::to_string(result.rowsAffected)
            + " row(s) from '" + tableName + "'.");
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeDeleteRowsTool() {
    return std::make_shared<DeleteRowsTool>();
}

}  // namespace gridex::mcp
