#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "Services/MCP/Protocol.h"

namespace gridex::mcp {

// Async audit logger for MCP tool invocations. Writes JSONL to
// $XDG_DATA_HOME/gridex/mcp-audit.jsonl (defaults to ~/.local/share/gridex/...).
// Rotates when file size exceeds maxFileSize (default 100 MB).
class AuditLogger {
public:
    AuditLogger();
    ~AuditLogger();

    void log(const MCPAuditEntry& entry);

    std::vector<MCPAuditEntry> recentEntries(int limit = 100) const;
    void clearAll();
    void close();

    std::string logFilePath() const { return filePath_; }

    void setMaxFileSize(std::int64_t bytes) { maxFileSize_ = bytes; }

private:
    void rotateIfNeeded();
    void ensureOpen();

    mutable std::mutex mu_;
    std::string filePath_;
    std::ofstream out_;
    std::int64_t maxFileSize_ = 100LL * 1024 * 1024;
};

}  // namespace gridex::mcp
