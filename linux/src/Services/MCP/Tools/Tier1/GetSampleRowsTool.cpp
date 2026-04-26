#include <algorithm>

#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

namespace {

class GetSampleRowsTool final : public MCPTool {
public:
    std::string name() const override { return "get_sample_rows"; }
    std::string description() const override {
        return "Get sample rows from a table to help understand data shape. Default limit 10, max 100.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Schema; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"table_name",    {{"type", "string"}, {"description", "Name of the table"}}},
                {"limit",         {{"type", "integer"},
                                   {"description", "Number of rows (default 10, max 100)"},
                                   {"default", 10}, {"minimum", 1}, {"maximum", 100}}},
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
        int limit = 10;
        if (params.contains("limit") && params["limit"].is_number_integer()) {
            limit = std::clamp(params["limit"].get<int>(), 1, 100);
        }

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);
        auto result = adapter->fetchRows(tableName, std::nullopt, std::nullopt,
                                         std::nullopt, std::nullopt, limit, 0);

        if (result.rows.empty()) {
            return MCPToolResult::textResult("Table '" + tableName + "' is empty.");
        }

        nlohmann::json rows = nlohmann::json::array();
        for (const auto& row : result.rows) {
            nlohmann::json r = nlohmann::json::object();
            for (std::size_t i = 0; i < result.columns.size() && i < row.size(); ++i) {
                r[result.columns[i].name] = row[i].displayString();
            }
            rows.push_back(std::move(r));
        }

        std::string header = "Sample " + std::to_string(rows.size()) + " row(s) from '" + tableName + "':\n";
        std::string cols = "Columns: ";
        for (std::size_t i = 0; i < result.columns.size(); ++i) {
            if (i > 0) cols += ", ";
            cols += result.columns[i].name + " (" + result.columns[i].dataType + ")";
        }
        cols += "\n\n";
        return MCPToolResult::textResult(header + cols + rows.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeGetSampleRowsTool() {
    return std::make_shared<GetSampleRowsTool>();
}

}  // namespace gridex::mcp
