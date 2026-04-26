#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"
#include "Services/MCP/Security/IdentifierValidator.h"

namespace gridex::mcp {

namespace {

class InsertRowsTool final : public MCPTool {
public:
    std::string name() const override { return "insert_rows"; }
    std::string description() const override {
        return "Insert one or more rows into a table. Requires user approval. Returns affected count.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Write; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table to insert into"}}},
                {"schema",        {{"type", "string"}, {"description", "Optional schema name"}}},
                {"rows",          {{"type", "array"},
                                    {"description", "Array of row objects to insert"},
                                    {"items", {{"type", "object"}}}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "table_name", "rows"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        auto [tableName, schemaName] = IdentifierValidator::extractTableAndSchema(params);

        if (!params.contains("rows") || !params["rows"].is_array() || params["rows"].empty()) {
            throw MCPToolError::invalidParameters("rows must be a non-empty array");
        }
        const auto& rowsArr = params["rows"];

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (perm.kind() == PermissionResult::Kind::Denied) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        if (perm.requiresUserApproval()) {
            std::string preview;
            {
                nlohmann::json prev = nlohmann::json::array();
                std::size_t n = std::min<std::size_t>(rowsArr.size(), 5);
                for (std::size_t i = 0; i < n; ++i) prev.push_back(rowsArr[i]);
                preview = prev.dump(2);
                if (rowsArr.size() > 5) {
                    preview += "\n... and " + std::to_string(rowsArr.size() - 5) + " more rows";
                }
            }
            bool approved = ctx.requestApproval(
                name(),
                "Insert " + std::to_string(rowsArr.size()) + " row(s) into '" + tableName + "'",
                preview,
                connectionId);
            if (!approved) throw MCPToolError::permissionDenied("User denied the operation.");
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);
        int inserted = 0;
        for (const auto& rowVal : rowsArr) {
            if (!rowVal.is_object()) continue;
            std::unordered_map<std::string, RowValue> values;
            for (auto it = rowVal.begin(); it != rowVal.end(); ++it) {
                values[it.key()] = jsonToRowValue(it.value());
            }
            (void)adapter->insertRow(tableName, schemaName, values);
            ++inserted;
        }
        ctx.recordUsage(tier(), connectionId);
        return MCPToolResult::textResult(
            "Successfully inserted " + std::to_string(inserted) + " row(s) into '" + tableName + "'.");
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeInsertRowsTool() {
    return std::make_shared<InsertRowsTool>();
}

}  // namespace gridex::mcp
