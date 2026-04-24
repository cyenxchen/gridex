#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "Core/Enums/MCPPermissionTier.h"

namespace gridex::mcp {

class RateLimitResult {
public:
    static RateLimitResult allowed() { return {true, 0}; }
    static RateLimitResult exceeded(int retryAfter) { return {false, retryAfter}; }

    bool isAllowed() const noexcept { return allowed_; }
    std::optional<int> retryAfterSeconds() const noexcept {
        return allowed_ ? std::optional<int>{} : std::optional<int>{retryAfter_};
    }

private:
    RateLimitResult(bool a, int r) : allowed_(a), retryAfter_(r) {}
    bool allowed_;
    int retryAfter_;
};

struct RateLimits {
    int queriesPerMinute = 60;
    int queriesPerHour   = 1000;
    int writesPerMinute  = 10;
    int ddlPerMinute     = 1;
};

class RateLimiter {
public:
    // Limits are looked up at check time so QSettings changes apply live.
    void setLimits(RateLimits limits);
    RateLimits limits() const;

    RateLimitResult checkLimit(MCPPermissionTier tier, const std::string& connectionId);
    void recordUsage(MCPPermissionTier tier, const std::string& connectionId);

    void resetLimits(const std::string& connectionId);
    void resetAll();

private:
    using Clock = std::chrono::steady_clock;
    using Bucket = std::deque<Clock::time_point>;

    RateLimitResult checkBucket(Bucket& b, int limit, std::chrono::seconds window, Clock::time_point now);
    void recordBucket(Bucket& b, Clock::time_point now, std::chrono::seconds maxAge);

    mutable std::mutex mu_;
    RateLimits limits_;
    std::unordered_map<std::string, Bucket> queryCount_;
    std::unordered_map<std::string, Bucket> writeCount_;
    std::unordered_map<std::string, Bucket> ddlCount_;
};

}  // namespace gridex::mcp
