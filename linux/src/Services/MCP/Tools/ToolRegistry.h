#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Services/MCP/Tools/MCPTool.h"

namespace gridex::mcp {

class ToolRegistry {
public:
    ToolRegistry();
    ~ToolRegistry();

    void registerTool(std::shared_ptr<MCPTool> tool);
    void unregister(const std::string& name);

    std::shared_ptr<MCPTool> get(const std::string& name) const;
    std::vector<std::shared_ptr<MCPTool>> all() const;
    std::vector<MCPToolDefinition> definitions() const;

    // Registers all built-in tools (Tier 1-3).
    void registerBuiltins();

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<MCPTool>> tools_;
};

}  // namespace gridex::mcp
