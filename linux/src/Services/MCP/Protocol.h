#pragma once

// MCP (Model Context Protocol) JSON-RPC 2.0 types + audit entry.
// Uses nlohmann::json for dynamic JSON (replaces Swift's hand-rolled JSONValue enum).

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/MCPConnectionMode.h"
#include "Core/Enums/MCPPermissionTier.h"

namespace gridex::mcp {

using nlohmann::json;

// JSON-RPC 2.0 error codes
struct JSONRPCError {
    int code = 0;
    std::string message;
    std::optional<json> data;

    json toJson() const {
        json j = {{"code", code}, {"message", message}};
        if (data) j["data"] = *data;
        return j;
    }

    static JSONRPCError parseError()      { return {-32700, "Parse error",      std::nullopt}; }
    static JSONRPCError invalidRequest()  { return {-32600, "Invalid Request",  std::nullopt}; }
    static JSONRPCError methodNotFound()  { return {-32601, "Method not found", std::nullopt}; }
    static JSONRPCError invalidParams()   { return {-32602, "Invalid params",   std::nullopt}; }
    static JSONRPCError internalError()   { return {-32603, "Internal error",   std::nullopt}; }
};

// MCP-specific JSON-RPC error codes (negative, per spec "reserved for server use")
enum class MCPErrorCode : int {
    PermissionDenied   = -32001,
    ApprovalTimeout    = -32002,
    ApprovalDenied     = -32003,
    ConnectionError    = -32004,
    SyntaxError        = -32005,
    NotFound           = -32006,
    RateLimitExceeded  = -32007,
    ScopeDenied        = -32008,
    QueryTimeout       = -32009,
};

// JSON-RPC request. id may be a string, int, or absent (notification).
struct JSONRPCRequest {
    json id;              // null if notification
    std::string method;
    json params;          // object or null

    static JSONRPCRequest fromJson(const json& j) {
        JSONRPCRequest r;
        if (j.contains("id"))     r.id = j["id"];
        if (j.contains("method")) r.method = j.value("method", "");
        if (j.contains("params")) r.params = j["params"];
        return r;
    }
};

// JSON-RPC response.
struct JSONRPCResponse {
    json id;                              // echoes request id
    std::optional<json> result;
    std::optional<JSONRPCError> error;

    static JSONRPCResponse ok(const json& id, json result) {
        JSONRPCResponse r;
        r.id = id;
        r.result = std::move(result);
        return r;
    }

    static JSONRPCResponse err(const json& id, JSONRPCError error) {
        JSONRPCResponse r;
        r.id = id;
        r.error = std::move(error);
        return r;
    }

    json toJson() const {
        json j = {{"jsonrpc", "2.0"}};
        j["id"] = id;  // may be null
        if (result)    j["result"] = *result;
        else if (error) j["error"] = error->toJson();
        return j;
    }
};

// MCP tool content block (as returned from tools/call).
struct MCPContent {
    std::string type = "text";
    std::optional<std::string> text;
    std::optional<std::string> data;       // base64 for images
    std::optional<std::string> mimeType;

    json toJson() const {
        json j = {{"type", type}};
        if (text)     j["text"]     = *text;
        if (data)     j["data"]     = *data;
        if (mimeType) j["mimeType"] = *mimeType;
        return j;
    }
};

// MCP tool execution result.
struct MCPToolResult {
    std::vector<MCPContent> content;
    bool isError = false;

    static MCPToolResult textResult(std::string text, bool err = false) {
        MCPToolResult r;
        MCPContent c;
        c.type = "text";
        c.text = std::move(text);
        r.content.push_back(std::move(c));
        r.isError = err;
        return r;
    }

    static MCPToolResult errorResult(std::string msg) {
        return textResult(std::move(msg), true);
    }

    json toJson() const {
        json arr = json::array();
        for (const auto& c : content) arr.push_back(c.toJson());
        json j = {{"content", arr}};
        if (isError) j["isError"] = true;
        return j;
    }
};

// MCP tool static definition (advertised via tools/list).
struct MCPToolDefinition {
    std::string name;
    std::string description;
    json inputSchema;       // JSON Schema object

    json toJson() const {
        return {{"name", name}, {"description", description}, {"inputSchema", inputSchema}};
    }
};

// MCP client handshake info (learned during initialize).
struct MCPClientInfo {
    std::string name    = "unknown";
    std::string version = "0.0.0";
};

// Audit record written to JSONL log.

enum class MCPAuditStatus { Success, Error, Denied, Timeout };

inline std::string toString(MCPAuditStatus s) {
    switch (s) {
        case MCPAuditStatus::Success: return "success";
        case MCPAuditStatus::Error:   return "error";
        case MCPAuditStatus::Denied:  return "denied";
        case MCPAuditStatus::Timeout: return "timeout";
    }
    return "error";
}

struct MCPAuditClient {
    std::string name      = "unknown";
    std::string version   = "0.0.0";
    std::string transport = "stdio";
};

struct MCPAuditInput {
    std::optional<std::string> sqlPreview;
    std::optional<int>         paramsCount;
    std::optional<std::string> inputHash;

    static MCPAuditInput fromSql(const std::optional<std::string>& sql, std::optional<int> pc) {
        MCPAuditInput i;
        i.paramsCount = pc;
        if (sql) {
            const auto& s = *sql;
            i.sqlPreview = s.size() > 200 ? (s.substr(0, 200) + "...") : s;
            i.inputHash  = "sha256:" + std::to_string(std::hash<std::string>{}(s));
        }
        return i;
    }
};

struct MCPAuditResult {
    MCPAuditStatus status = MCPAuditStatus::Success;
    std::optional<int> rowsAffected;
    std::optional<int> rowsReturned;
    int durationMs = 0;
    std::optional<int> bytesReturned;
};

struct MCPAuditSecurity {
    MCPConnectionMode mode = MCPConnectionMode::Locked;
    std::optional<bool> userApproved;
    std::optional<std::string> approvalSessionId;
    std::optional<std::vector<std::string>> scopesApplied;
};

struct MCPAuditEntry {
    std::string timestampIso8601;
    std::string eventId;
    MCPAuditClient client;
    std::string tool;
    int tier = 1;
    std::optional<std::string> connectionId;    // UUID string
    std::optional<std::string> connectionType;  // db type raw
    MCPAuditInput input;
    MCPAuditResult result;
    MCPAuditSecurity security;
    std::optional<std::string> error;

    json toJson() const {
        json clientJ = {{"name", client.name}, {"version", client.version}, {"transport", client.transport}};

        json inputJ = json::object();
        if (input.sqlPreview)  inputJ["sqlPreview"] = *input.sqlPreview;
        if (input.paramsCount) inputJ["paramsCount"] = *input.paramsCount;
        if (input.inputHash)   inputJ["inputHash"] = *input.inputHash;

        json resultJ = {{"status", toString(result.status)}, {"durationMs", result.durationMs}};
        if (result.rowsAffected)  resultJ["rowsAffected"]  = *result.rowsAffected;
        if (result.rowsReturned)  resultJ["rowsReturned"]  = *result.rowsReturned;
        if (result.bytesReturned) resultJ["bytesReturned"] = *result.bytesReturned;

        json securityJ = {{"permissionMode", std::string(rawValue(security.mode))}};
        if (security.userApproved)      securityJ["userApproved"]      = *security.userApproved;
        if (security.approvalSessionId) securityJ["approvalSessionId"] = *security.approvalSessionId;
        if (security.scopesApplied)     securityJ["scopesApplied"]     = *security.scopesApplied;

        json j = {
            {"timestamp", timestampIso8601},
            {"eventId",   eventId},
            {"client",    clientJ},
            {"tool",      tool},
            {"tier",      tier},
            {"input",     inputJ},
            {"result",    resultJ},
            {"security",  securityJ},
        };
        if (connectionId)   j["connectionId"]   = *connectionId;
        if (connectionType) j["connectionType"] = *connectionType;
        if (error)          j["error"]          = *error;
        return j;
    }

    static std::optional<MCPAuditEntry> fromJson(const json& j);
};

// MCP tool error — thrown from inside tool.execute() to be caught by MCPServer.
class MCPToolError : public std::runtime_error {
public:
    enum class Kind {
        ConnectionNotFound,
        ConnectionNotConnected,
        TableNotFound,
        InvalidParameters,
        PermissionDenied,
        QueryFailed,
        RateLimitExceeded,
    };

    MCPToolError(Kind k, std::string message, int retryAfter = 0)
        : std::runtime_error(std::move(message)), kind_(k), retryAfter_(retryAfter) {}

    Kind kind() const noexcept { return kind_; }
    int retryAfterSeconds() const noexcept { return retryAfter_; }

    static MCPToolError invalidParameters(std::string msg) {
        return {Kind::InvalidParameters, "Invalid parameters: " + msg};
    }
    static MCPToolError connectionNotFound(const std::string& id) {
        return {Kind::ConnectionNotFound,
                "Connection '" + id + "' not found. Use list_connections to see available connections."};
    }
    static MCPToolError connectionNotConnected(const std::string& id) {
        return {Kind::ConnectionNotConnected,
                "Connection '" + id + "' is not active. The user needs to connect first."};
    }
    static MCPToolError tableNotFound(const std::string& name) {
        return {Kind::TableNotFound, "Table '" + name + "' not found."};
    }
    static MCPToolError permissionDenied(std::string msg) {
        return {Kind::PermissionDenied, std::move(msg)};
    }
    static MCPToolError queryFailed(const std::string& msg) {
        return {Kind::QueryFailed, "Query failed: " + msg};
    }
    static MCPToolError rateLimitExceeded(int retryAfter) {
        return {Kind::RateLimitExceeded,
                "Rate limit exceeded. Retry after " + std::to_string(retryAfter) + " seconds.",
                retryAfter};
    }

private:
    Kind kind_;
    int retryAfter_ = 0;
};

}  // namespace gridex::mcp
