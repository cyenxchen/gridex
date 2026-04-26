#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Services/MCP/Protocol.h"

namespace gridex::mcp {

enum class ApprovalResult { Approved, ApprovedForSession, Denied };

// Approval gate: asks the user to approve a write/DDL operation.
// In GUI mode a dialog callback is invoked on the Qt main thread.
// In stdio CLI mode (no GUI), the callback may default to auto-deny or auto-approve
// depending on config.
class ApprovalGate {
public:
    // dialogCallback is invoked with (tool, description, details, connectionId, client).
    // It must return an ApprovalResult. Safe to call from a background thread — the
    // implementation should marshal to the GUI thread internally if needed.
    using DialogCallback = std::function<ApprovalResult(
        const std::string& tool,
        const std::string& description,
        const std::string& details,
        const std::string& connectionId,
        const MCPAuditClient& client)>;

    void setDialogCallback(DialogCallback cb);

    // Blocks the calling thread up to `timeout` waiting for the user to respond.
    // Returns true if approved, false if denied or timed out.
    bool requestApproval(const std::string& tool,
                         const std::string& description,
                         const std::string& details,
                         const std::string& connectionId,
                         const MCPAuditClient& client,
                         std::chrono::seconds timeout = std::chrono::seconds{60});

    void revokeSessionApproval(const std::string& connectionId);
    void revokeAllSessionApprovals();

private:
    struct SessionKey {
        std::string connectionId;
        std::string tool;
        bool operator==(const SessionKey& o) const noexcept {
            return connectionId == o.connectionId && tool == o.tool;
        }
    };
    struct SessionKeyHash {
        std::size_t operator()(const SessionKey& k) const noexcept {
            return std::hash<std::string>{}(k.connectionId) ^ (std::hash<std::string>{}(k.tool) << 1);
        }
    };

    mutable std::mutex mu_;
    DialogCallback callback_;
    std::unordered_map<SessionKey, std::chrono::steady_clock::time_point, SessionKeyHash> sessionApprovals_;
    static constexpr std::chrono::minutes kSessionTimeout{30};
};

}  // namespace gridex::mcp
