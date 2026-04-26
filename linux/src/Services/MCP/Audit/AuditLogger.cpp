#include "Services/MCP/Audit/AuditLogger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace gridex::mcp {

namespace fs = std::filesystem;

namespace {

std::string xdgDataHome() {
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return x;
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share";
    return ".";
}

std::string isoTimestamp(std::chrono::system_clock::time_point tp, bool fileSafe = false) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    if (fileSafe) {
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H-%M-%SZ", &tm);
    } else {
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }
    return buf;
}

}  // namespace

AuditLogger::AuditLogger() {
    fs::path dir = fs::path(xdgDataHome()) / "gridex";
    std::error_code ec;
    fs::create_directories(dir, ec);
    filePath_ = (dir / "mcp-audit.jsonl").string();
}

AuditLogger::~AuditLogger() { close(); }

void AuditLogger::ensureOpen() {
    if (out_.is_open()) return;
    out_.open(filePath_, std::ios::app);
}

void AuditLogger::rotateIfNeeded() {
    std::error_code ec;
    if (!fs::exists(filePath_, ec)) return;
    auto size = fs::file_size(filePath_, ec);
    if (ec || static_cast<std::int64_t>(size) < maxFileSize_) return;

    if (out_.is_open()) out_.close();

    std::string stamp = isoTimestamp(std::chrono::system_clock::now(), true);
    fs::path src(filePath_);
    fs::path dst = src.parent_path() / ("mcp-audit-" + stamp + ".jsonl");
    fs::rename(src, dst, ec);
}

void AuditLogger::log(const MCPAuditEntry& entry) {
    std::lock_guard lk(mu_);
    try {
        rotateIfNeeded();
        ensureOpen();
        if (!out_.is_open()) return;
        out_ << entry.toJson().dump() << '\n';
        out_.flush();
    } catch (const std::exception& e) {
        std::cerr << "[MCP Audit] Failed to log entry: " << e.what() << '\n';
    }
}

std::vector<MCPAuditEntry> AuditLogger::recentEntries(int limit) const {
    std::lock_guard lk(mu_);
    std::vector<MCPAuditEntry> out;
    std::ifstream in(filePath_);
    if (!in) return out;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(std::move(line));
    }
    int start = std::max<int>(0, static_cast<int>(lines.size()) - limit);
    for (int i = static_cast<int>(lines.size()) - 1; i >= start; --i) {
        try {
            auto j = nlohmann::json::parse(lines[i]);
            if (auto e = MCPAuditEntry::fromJson(j)) out.push_back(*e);
        } catch (...) {}
    }
    return out;
}

void AuditLogger::clearAll() {
    std::lock_guard lk(mu_);
    if (out_.is_open()) out_.close();
    std::error_code ec;
    fs::remove(filePath_, ec);
}

void AuditLogger::close() {
    std::lock_guard lk(mu_);
    if (out_.is_open()) out_.close();
}

// MCPAuditEntry::fromJson definition — placed here to avoid header bloat.
std::optional<MCPAuditEntry> MCPAuditEntry::fromJson(const nlohmann::json& j) {
    try {
        MCPAuditEntry e;
        e.timestampIso8601 = j.value("timestamp", "");
        e.eventId          = j.value("eventId", "");
        e.tool             = j.value("tool", "");
        e.tier             = j.value("tier", 1);
        if (j.contains("connectionId"))   e.connectionId   = j["connectionId"].get<std::string>();
        if (j.contains("connectionType")) e.connectionType = j["connectionType"].get<std::string>();
        if (j.contains("error"))          e.error          = j["error"].get<std::string>();
        if (j.contains("client")) {
            e.client.name      = j["client"].value("name", "unknown");
            e.client.version   = j["client"].value("version", "0.0.0");
            e.client.transport = j["client"].value("transport", "stdio");
        }
        if (j.contains("input")) {
            const auto& i = j["input"];
            if (i.contains("sqlPreview"))  e.input.sqlPreview  = i["sqlPreview"].get<std::string>();
            if (i.contains("paramsCount")) e.input.paramsCount = i["paramsCount"].get<int>();
            if (i.contains("inputHash"))   e.input.inputHash   = i["inputHash"].get<std::string>();
        }
        if (j.contains("result")) {
            const auto& r = j["result"];
            std::string s = r.value("status", "success");
            e.result.status = (s == "success") ? MCPAuditStatus::Success
                            : (s == "denied")  ? MCPAuditStatus::Denied
                            : (s == "timeout") ? MCPAuditStatus::Timeout
                                               : MCPAuditStatus::Error;
            e.result.durationMs = r.value("durationMs", 0);
            if (r.contains("rowsAffected"))  e.result.rowsAffected  = r["rowsAffected"].get<int>();
            if (r.contains("rowsReturned"))  e.result.rowsReturned  = r["rowsReturned"].get<int>();
            if (r.contains("bytesReturned")) e.result.bytesReturned = r["bytesReturned"].get<int>();
        }
        if (j.contains("security")) {
            const auto& s = j["security"];
            std::string modeStr = s.value("permissionMode", "locked");
            if (auto m = mcpConnectionModeFromRaw(modeStr)) e.security.mode = *m;
            if (s.contains("userApproved"))      e.security.userApproved      = s["userApproved"].get<bool>();
            if (s.contains("approvalSessionId")) e.security.approvalSessionId = s["approvalSessionId"].get<std::string>();
            if (s.contains("scopesApplied"))     e.security.scopesApplied     = s["scopesApplied"].get<std::vector<std::string>>();
        }
        return e;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace gridex::mcp
