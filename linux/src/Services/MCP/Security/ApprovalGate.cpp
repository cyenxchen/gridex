#include "Services/MCP/Security/ApprovalGate.h"

namespace gridex::mcp {

void ApprovalGate::setDialogCallback(DialogCallback cb) {
    std::lock_guard lk(mu_);
    callback_ = std::move(cb);
}

bool ApprovalGate::requestApproval(const std::string& tool,
                                   const std::string& description,
                                   const std::string& details,
                                   const std::string& connectionId,
                                   const MCPAuditClient& client,
                                   std::chrono::seconds timeout) {
    // Fast-path: existing session approval.
    {
        std::lock_guard lk(mu_);
        SessionKey key{connectionId, tool};
        auto it = sessionApprovals_.find(key);
        if (it != sessionApprovals_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second;
            if (age < kSessionTimeout) return true;
            sessionApprovals_.erase(it);
        }
    }

    DialogCallback cb;
    {
        std::lock_guard lk(mu_);
        cb = callback_;
    }
    if (!cb) {
        // No GUI dialog wired up → deny by default (stdio/CLI mode).
        return false;
    }

    // Run the dialog on a worker thread wrapped in future; the callback itself
    // is expected to marshal onto Qt main thread as needed.
    std::promise<ApprovalResult> prom;
    auto fut = prom.get_future();

    std::thread([cb, tool, description, details, connectionId, client, p = std::move(prom)]() mutable {
        try {
            p.set_value(cb(tool, description, details, connectionId, client));
        } catch (...) {
            p.set_value(ApprovalResult::Denied);
        }
    }).detach();

    if (fut.wait_for(timeout) != std::future_status::ready) {
        return false;  // timeout → treat as denied
    }
    ApprovalResult r = fut.get();
    if (r == ApprovalResult::ApprovedForSession) {
        std::lock_guard lk(mu_);
        sessionApprovals_[SessionKey{connectionId, tool}] = std::chrono::steady_clock::now();
    }
    return r == ApprovalResult::Approved || r == ApprovalResult::ApprovedForSession;
}

void ApprovalGate::revokeSessionApproval(const std::string& connectionId) {
    std::lock_guard lk(mu_);
    for (auto it = sessionApprovals_.begin(); it != sessionApprovals_.end();) {
        if (it->first.connectionId == connectionId) it = sessionApprovals_.erase(it);
        else ++it;
    }
}

void ApprovalGate::revokeAllSessionApprovals() {
    std::lock_guard lk(mu_);
    sessionApprovals_.clear();
}

}  // namespace gridex::mcp
