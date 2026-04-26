#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "Services/MCP/Audit/AuditLogger.h"
#include "Services/MCP/Protocol.h"
#include "Services/MCP/Security/ApprovalGate.h"
#include "Services/MCP/Security/PermissionEngine.h"
#include "Services/MCP/Security/RateLimiter.h"
#include "Services/MCP/Tools/MCPTool.h"
#include "Services/MCP/Tools/ToolRegistry.h"
#include "Services/MCP/Transport/StdioTransport.h"

namespace gridex::mcp {

enum class MCPTransportMode {
    Stdio,      // CLI mode — read stdin, write stdout
    HttpOnly,   // GUI mode — HTTP only (not yet implemented)
    InProcess,  // GUI mode — no transport; used for exposing state to MCP window
};

// Central MCP server. Holds registries (tools, security, audit) and routes
// JSON-RPC requests from whichever transport is configured.
class MCPServer {
public:
    MCPServer(std::shared_ptr<IMCPConnectionProvider> connectionProvider,
              std::string serverVersion = "1.0.0",
              MCPTransportMode mode = MCPTransportMode::InProcess);
    ~MCPServer();

    void start();
    void stop();
    bool isRunning() const noexcept { return running_.load(); }
    std::chrono::system_clock::time_point startTime() const noexcept { return startTime_; }

    // Accessors for UI layer.
    PermissionEngine& permissionEngine() noexcept { return *permissionEngine_; }
    RateLimiter&      rateLimiter()      noexcept { return *rateLimiter_; }
    ApprovalGate&     approvalGate()     noexcept { return *approvalGate_; }
    AuditLogger&      auditLogger()      noexcept { return *auditLogger_; }
    ToolRegistry&     toolRegistry()     noexcept { return *toolRegistry_; }
    IMCPConnectionProvider& connectionProvider() noexcept { return *connectionProvider_; }

    // Per-connection mode.
    void setConnectionMode(const std::string& id, MCPConnectionMode mode);
    MCPConnectionMode getConnectionMode(const std::string& id) const;

    const MCPClientInfo& clientInfo() const noexcept { return clientInfo_; }

    // Manual request routing — for HTTP transport or in-process tests.
    JSONRPCResponse handleRequest(const JSONRPCRequest& request);

private:
    JSONRPCResponse handleInitialize(const JSONRPCRequest&);
    JSONRPCResponse handleToolsList(const JSONRPCRequest&);
    JSONRPCResponse handleToolCall(const JSONRPCRequest&);

    static std::string sanitizeError(const std::string& msg);

    std::shared_ptr<IMCPConnectionProvider> connectionProvider_;
    std::string serverVersion_;
    MCPTransportMode mode_;

    std::unique_ptr<StdioTransport> transport_;
    std::unique_ptr<ToolRegistry>   toolRegistry_;
    std::unique_ptr<PermissionEngine> permissionEngine_;
    std::unique_ptr<AuditLogger>    auditLogger_;
    std::unique_ptr<RateLimiter>    rateLimiter_;
    std::unique_ptr<ApprovalGate>   approvalGate_;

    std::atomic<bool> running_{false};
    std::chrono::system_clock::time_point startTime_{};
    mutable std::mutex stateMu_;
    MCPClientInfo clientInfo_;
};

}  // namespace gridex::mcp
