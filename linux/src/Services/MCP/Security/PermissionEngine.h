#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "Core/Enums/MCPConnectionMode.h"
#include "Core/Enums/MCPPermissionTier.h"

namespace gridex::mcp {

// Three-way permission result: allowed, needs user approval, or denied with message.
class PermissionResult {
public:
    enum class Kind { Allowed, RequiresApproval, Denied };

    static PermissionResult allowed()                    { return {Kind::Allowed, {}}; }
    static PermissionResult requiresApproval()           { return {Kind::RequiresApproval, {}}; }
    static PermissionResult denied(std::string message)  { return {Kind::Denied, std::move(message)}; }

    Kind kind() const noexcept { return kind_; }
    bool isAllowed() const noexcept { return kind_ == Kind::Allowed; }
    bool requiresUserApproval() const noexcept { return kind_ == Kind::RequiresApproval; }
    const std::string& errorMessage() const noexcept { return message_; }

private:
    PermissionResult(Kind k, std::string msg) : kind_(k), message_(std::move(msg)) {}
    Kind kind_;
    std::string message_;
};

// Thread-safe permission engine. Holds per-connection MCPConnectionMode and
// exposes tier/SQL/WHERE-clause validation helpers.
class PermissionEngine {
public:
    void setMode(const std::string& connectionId, MCPConnectionMode mode);
    MCPConnectionMode getMode(const std::string& connectionId) const;
    void removeMode(const std::string& connectionId);

    PermissionResult checkPermission(MCPPermissionTier tier, const std::string& connectionId) const;
    PermissionResult checkPermission(MCPPermissionTier tier, MCPConnectionMode mode) const;

    PermissionResult validateReadOnlyQuery(const std::string& sql) const;
    PermissionResult validateWhereClause(const std::optional<std::string>& whereClause) const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, MCPConnectionMode> modes_;
};

}  // namespace gridex::mcp
