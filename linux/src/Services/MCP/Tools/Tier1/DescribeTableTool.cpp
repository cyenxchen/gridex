#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"

namespace gridex::mcp {

namespace {

class DescribeTableTool final : public MCPTool {
public:
    std::string name() const override { return "describe_table"; }
    std::string description() const override {
        return "Get detailed structure of a table including columns, data types, indexes, "
               "primary keys, foreign keys, and constraints.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table to describe"}}},
                {"schema",        {{"type", "string"}, {"description", "Optional schema name"}}},
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
        std::optional<std::string> schemaName;
        if (params.contains("schema") && params["schema"].is_string()) {
            schemaName = params["schema"].get<std::string>();
        }

        auto desc = adapter->describeTable(tableName, schemaName);
        auto indexes = adapter->listIndexes(tableName, schemaName);
        auto foreignKeys = adapter->listForeignKeys(tableName, schemaName);

        nlohmann::json result = {
            {"name", desc.name},
            {"database_type", std::string(displayName(config.databaseType))},
        };
        if (desc.schema)            result["schema"]         = *desc.schema;
        if (desc.comment)           result["comment"]        = *desc.comment;
        if (desc.estimatedRowCount) result["estimated_rows"] = *desc.estimatedRowCount;

        nlohmann::json cols = nlohmann::json::array();
        nlohmann::json pk   = nlohmann::json::array();
        for (const auto& c : desc.columns) {
            nlohmann::json col = {
                {"name", c.name},
                {"type", c.dataType},
                {"nullable", c.isNullable},
            };
            if (c.isPrimaryKey) { col["primary_key"] = true; pk.push_back(c.name); }
            if (c.defaultValue) col["default"] = *c.defaultValue;
            if (c.comment)      col["comment"] = *c.comment;
            cols.push_back(std::move(col));
        }
        result["columns"] = cols;
        if (!pk.empty()) result["primary_key"] = pk;

        if (!indexes.empty()) {
            nlohmann::json idx = nlohmann::json::array();
            for (const auto& i : indexes) {
                idx.push_back({
                    {"name", i.name},
                    {"columns", i.columns},
                    {"unique", i.isUnique},
                    {"type", i.type.value_or("btree")},
                });
            }
            result["indexes"] = idx;
        }

        if (!foreignKeys.empty()) {
            nlohmann::json fks = nlohmann::json::array();
            for (const auto& fk : foreignKeys) {
                fks.push_back({
                    {"name", fk.name.value_or("")},
                    {"columns", fk.columns},
                    {"references_table", fk.referencedTable},
                    {"references_columns", fk.referencedColumns},
                });
            }
            result["foreign_keys"] = fks;
        }

        if (!desc.constraints.empty()) {
            nlohmann::json cs = nlohmann::json::array();
            for (const auto& c : desc.constraints) {
                cs.push_back({
                    {"name", c.name},
                    {"type", std::string(rawValue(c.type))},
                    {"definition", c.definition.value_or("")},
                });
            }
            result["constraints"] = cs;
        }

        result["ddl"] = desc.toDDL(sqlDialect(config.databaseType));
        return MCPToolResult::textResult(result.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeDescribeTableTool() {
    return std::make_shared<DescribeTableTool>();
}

}  // namespace gridex::mcp
