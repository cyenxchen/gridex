#include "Services/MCP/Security/PermissionEngine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <string_view>
#include <unordered_set>

#include "Services/MCP/Security/SQLSanitizer.h"

namespace gridex::mcp {

namespace {

static const std::array<std::string_view, 6> kReadOnlyPrefixes = {
    "SELECT", "SHOW", "EXPLAIN", "DESCRIBE", "DESC", "WITH"
};

static const std::array<std::string_view, 34> kDangerousKeywords = {
    "INSERT", "UPDATE", "DELETE", "MERGE", "UPSERT",
    "DROP", "CREATE", "ALTER", "TRUNCATE", "RENAME",
    "GRANT", "REVOKE",
    "CALL", "EXEC", "EXECUTE", "DO",
    "NEXTVAL", "SETVAL",
    "LO_IMPORT", "LO_EXPORT",
    "COPY", "VACUUM", "ANALYZE", "REINDEX", "CLUSTER", "REFRESH",
    "LOCK", "UNLOCK",
    "SET", "RESET", "BEGIN", "COMMIT", "ROLLBACK", "SAVEPOINT"
};

std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool startsWith(const std::string& s, std::string_view prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool containsWord(const std::string& code, std::string_view word) {
    // Simple word boundary scan (ASCII only).
    auto isWord = [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    };
    const std::size_t n = code.size();
    const std::size_t m = word.size();
    if (m == 0 || m > n) return false;
    for (std::size_t i = 0; i + m <= n; ++i) {
        if (i > 0 && isWord(static_cast<unsigned char>(code[i - 1]))) continue;
        if (i + m < n && isWord(static_cast<unsigned char>(code[i + m]))) continue;
        bool eq = true;
        for (std::size_t k = 0; k < m; ++k) {
            if (std::toupper(static_cast<unsigned char>(code[i + k])) != word[k]) { eq = false; break; }
        }
        if (eq) return true;
    }
    return false;
}

}  // namespace

void PermissionEngine::setMode(const std::string& id, MCPConnectionMode mode) {
    std::lock_guard lk(mu_);
    modes_[id] = mode;
}

MCPConnectionMode PermissionEngine::getMode(const std::string& id) const {
    std::lock_guard lk(mu_);
    auto it = modes_.find(id);
    return it != modes_.end() ? it->second : MCPConnectionMode::Locked;
}

void PermissionEngine::removeMode(const std::string& id) {
    std::lock_guard lk(mu_);
    modes_.erase(id);
}

PermissionResult PermissionEngine::checkPermission(MCPPermissionTier tier, const std::string& id) const {
    return checkPermission(tier, getMode(id));
}

PermissionResult PermissionEngine::checkPermission(MCPPermissionTier tier, MCPConnectionMode mode) const {
    switch (tier) {
        case MCPPermissionTier::Schema:
            return allowsTier1(mode) ? PermissionResult::allowed()
                : PermissionResult::denied("Connection is locked. MCP access is disabled.");
        case MCPPermissionTier::Read:
            return allowsTier2(mode) ? PermissionResult::allowed()
                : PermissionResult::denied("Connection is locked. MCP access is disabled.");
        case MCPPermissionTier::Write:
            if (!allowsTier3(mode)) {
                return PermissionResult::denied(
                    "This operation requires read-write mode. Ask the user to enable it in "
                    "Connection Settings > MCP Access.");
            }
            return PermissionResult::requiresApproval();
        case MCPPermissionTier::Ddl:
            if (!allowsTier4(mode)) {
                return PermissionResult::denied(
                    "DDL operations require read-write mode. Ask the user to enable it in "
                    "Connection Settings > MCP Access.");
            }
            return PermissionResult::requiresApproval();
        case MCPPermissionTier::Advanced:
            return allowsTier5(mode) ? PermissionResult::allowed()
                : PermissionResult::denied("Connection is locked. MCP access is disabled.");
    }
    return PermissionResult::denied("Unknown tier.");
}

PermissionResult PermissionEngine::validateReadOnlyQuery(const std::string& sql) const {
    std::string code = stripCommentsAndStrings(sql);
    std::string upper = toUpper(trim(code));

    // One optional trailing `;` is fine; anything else is multi-statement.
    std::string withoutSemi = upper;
    if (!withoutSemi.empty() && withoutSemi.back() == ';') withoutSemi.pop_back();
    withoutSemi = trim(withoutSemi);
    if (withoutSemi.find(';') != std::string::npos) {
        return PermissionResult::denied("Multiple statements are not allowed in read-only mode.");
    }

    bool isRO = false;
    for (auto p : kReadOnlyPrefixes) if (startsWith(upper, p)) { isRO = true; break; }
    if (!isRO) {
        return PermissionResult::denied(
            "Only SELECT queries are allowed in read-only mode. This query appears to modify data.");
    }

    for (auto kw : kDangerousKeywords) {
        if (containsWord(code, kw)) {
            return PermissionResult::denied(
                "Query contains '" + std::string(kw) + "' which is not allowed in read-only mode.");
        }
    }
    return PermissionResult::allowed();
}

PermissionResult PermissionEngine::validateWhereClause(const std::optional<std::string>& where) const {
    if (!where) {
        return PermissionResult::denied(
            "WHERE clause is required for UPDATE/DELETE operations. Bare UPDATE/DELETE without WHERE is not allowed.");
    }
    std::string trimmed = trim(*where);
    if (trimmed.empty()) {
        return PermissionResult::denied("WHERE clause is required for UPDATE/DELETE operations.");
    }
    if (trimmed.find(';') != std::string::npos) {
        return PermissionResult::denied("WHERE clause must not contain ';' — statement terminators are forbidden.");
    }
    if (trimmed.find("--") != std::string::npos) {
        return PermissionResult::denied("WHERE clause must not contain '--' — SQL line comments are forbidden.");
    }
    if (trimmed.find("/*") != std::string::npos || trimmed.find("*/") != std::string::npos) {
        return PermissionResult::denied("WHERE clause must not contain '/*' or '*/' — SQL block comments are forbidden.");
    }

    std::string compact;
    compact.reserve(trimmed.size());
    for (char c : toUpper(trimmed)) {
        if (!std::isspace(static_cast<unsigned char>(c))) compact.push_back(c);
    }
    static const std::unordered_set<std::string> kTrivial = {
        "1=1", "TRUE", "1", "'1'='1'", "1<>0", "0=0", "2>1", "TRUE=TRUE", "NULLISNULL"
    };
    if (kTrivial.count(compact)) {
        return PermissionResult::denied(
            "Trivial WHERE clause '" + trimmed + "' is not allowed. Provide a meaningful predicate.");
    }
    return PermissionResult::allowed();
}

}  // namespace gridex::mcp
