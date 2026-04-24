#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolHelpers.h"

namespace gridex::mcp {

namespace {

class SearchAcrossTablesTool final : public MCPTool {
public:
    std::string name() const override { return "search_across_tables"; }
    std::string description() const override {
        return "Search for a keyword across table names, column names, and column comments. "
               "Useful for discovering relevant data.";
    }
    MCPPermissionTier tier() const override { return MCPPermissionTier::Read; }

    nlohmann::json inputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"connection_id", {{"type", "string"}, {"description", "Connection identifier"}}},
                {"keyword",       {{"type", "string"}, {"description", "Keyword to search for"}}},
            }},
            {"required", nlohmann::json::array({"connection_id", "keyword"})},
        };
    }

    MCPToolResult execute(const nlohmann::json& params, const MCPToolContext& ctx) override {
        const std::string connectionId = MCPTool::extractConnectionId(params);
        if (!params.contains("keyword") || !params["keyword"].is_string()) {
            throw MCPToolError::invalidParameters("keyword is required");
        }
        const std::string keyword = params["keyword"].get<std::string>();

        auto perm = ctx.checkPermission(tier(), connectionId);
        if (!perm.isAllowed() && !perm.requiresUserApproval()) {
            throw MCPToolError::permissionDenied(perm.errorMessage());
        }

        auto [adapter, config] = ctx.getAdapter(connectionId);
        const std::string needle = toLowerAscii(keyword);

        auto contains = [&](const std::string& hay) {
            return toLowerAscii(hay).find(needle) != std::string::npos;
        };

        nlohmann::json matches = nlohmann::json::array();
        auto tables = adapter->listTables(std::nullopt);
        for (const auto& t : tables) {
            if (contains(t.name)) {
                matches.push_back({{"type", "table"}, {"table", t.name}, {"match", t.name}});
            }
            auto desc = adapter->describeTable(t.name, std::nullopt);
            for (const auto& col : desc.columns) {
                if (contains(col.name)) {
                    matches.push_back({
                        {"type", "column"},
                        {"table", t.name},
                        {"column", col.name},
                        {"data_type", col.dataType},
                    });
                }
                if (col.comment && contains(*col.comment)) {
                    matches.push_back({
                        {"type", "column_comment"},
                        {"table", t.name},
                        {"column", col.name},
                        {"comment", *col.comment},
                    });
                }
            }
            if (desc.comment && contains(*desc.comment)) {
                matches.push_back({
                    {"type", "table_comment"},
                    {"table", t.name},
                    {"comment", *desc.comment},
                });
            }
        }

        if (matches.empty()) {
            return MCPToolResult::textResult("No matches found for '" + keyword + "'.");
        }
        return MCPToolResult::textResult(
            "Found " + std::to_string(matches.size()) + " match(es) for '" + keyword + "':\n"
            + matches.dump(2));
    }
};

}  // namespace

std::shared_ptr<MCPTool> makeSearchAcrossTablesTool() {
    return std::make_shared<SearchAcrossTablesTool>();
}

}  // namespace gridex::mcp
