#include "Services/MCP/Tools/ToolRegistry.h"

// Built-in tool forward declarations. The concrete classes live in
// Tier1/, Tier2/, Tier3/ subdirectories.
namespace gridex::mcp {
std::shared_ptr<MCPTool> makeListConnectionsTool();
std::shared_ptr<MCPTool> makeListTablesTool();
std::shared_ptr<MCPTool> makeDescribeTableTool();
std::shared_ptr<MCPTool> makeListSchemasTool();
std::shared_ptr<MCPTool> makeGetSampleRowsTool();
std::shared_ptr<MCPTool> makeListRelationshipsTool();
std::shared_ptr<MCPTool> makeQueryTool();
std::shared_ptr<MCPTool> makeExplainQueryTool();
std::shared_ptr<MCPTool> makeSearchAcrossTablesTool();
std::shared_ptr<MCPTool> makeInsertRowsTool();
std::shared_ptr<MCPTool> makeUpdateRowsTool();
std::shared_ptr<MCPTool> makeDeleteRowsTool();
std::shared_ptr<MCPTool> makeExecuteWriteQueryTool();

ToolRegistry::ToolRegistry() = default;
ToolRegistry::~ToolRegistry() = default;

void ToolRegistry::registerTool(std::shared_ptr<MCPTool> t) {
    if (!t) return;
    std::lock_guard lk(mu_);
    tools_[t->name()] = std::move(t);
}

void ToolRegistry::unregister(const std::string& name) {
    std::lock_guard lk(mu_);
    tools_.erase(name);
}

std::shared_ptr<MCPTool> ToolRegistry::get(const std::string& name) const {
    std::lock_guard lk(mu_);
    auto it = tools_.find(name);
    return it != tools_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<MCPTool>> ToolRegistry::all() const {
    std::lock_guard lk(mu_);
    std::vector<std::shared_ptr<MCPTool>> out;
    out.reserve(tools_.size());
    for (const auto& [_, t] : tools_) out.push_back(t);
    return out;
}

std::vector<MCPToolDefinition> ToolRegistry::definitions() const {
    std::lock_guard lk(mu_);
    std::vector<MCPToolDefinition> out;
    out.reserve(tools_.size());
    for (const auto& [_, t] : tools_) out.push_back(t->definition());
    return out;
}

void ToolRegistry::registerBuiltins() {
    registerTool(makeListConnectionsTool());
    registerTool(makeListTablesTool());
    registerTool(makeDescribeTableTool());
    registerTool(makeListSchemasTool());
    registerTool(makeGetSampleRowsTool());
    registerTool(makeListRelationshipsTool());
    registerTool(makeQueryTool());
    registerTool(makeExplainQueryTool());
    registerTool(makeSearchAcrossTablesTool());
    registerTool(makeInsertRowsTool());
    registerTool(makeUpdateRowsTool());
    registerTool(makeDeleteRowsTool());
    registerTool(makeExecuteWriteQueryTool());
}

}  // namespace gridex::mcp
