#include "Services/MCP/Security/RateLimiter.h"

#include <algorithm>

namespace gridex::mcp {

using namespace std::chrono;

void RateLimiter::setLimits(RateLimits l) {
    std::lock_guard lk(mu_);
    limits_ = l;
}

RateLimits RateLimiter::limits() const {
    std::lock_guard lk(mu_);
    return limits_;
}

RateLimitResult RateLimiter::checkBucket(Bucket& b, int limit, seconds window, Clock::time_point now) {
    auto cutoff = now - window;
    int count = 0;
    Clock::time_point oldest = Clock::time_point::max();
    for (auto t : b) {
        if (t > cutoff) {
            ++count;
            oldest = std::min(oldest, t);
        }
    }
    if (count >= limit) {
        int retry = static_cast<int>(duration_cast<seconds>(window - (now - oldest)).count());
        return RateLimitResult::exceeded(std::max(1, retry));
    }
    return RateLimitResult::allowed();
}

void RateLimiter::recordBucket(Bucket& b, Clock::time_point now, seconds maxAge) {
    b.push_back(now);
    auto cutoff = now - maxAge;
    while (!b.empty() && b.front() <= cutoff) b.pop_front();
}

RateLimitResult RateLimiter::checkLimit(MCPPermissionTier tier, const std::string& id) {
    std::lock_guard lk(mu_);
    auto now = Clock::now();
    switch (tier) {
        case MCPPermissionTier::Schema:
        case MCPPermissionTier::Read:
        case MCPPermissionTier::Advanced: {
            auto& b = queryCount_[id];
            if (auto r = checkBucket(b, limits_.queriesPerMinute, seconds{60}, now); !r.isAllowed()) return r;
            return checkBucket(b, limits_.queriesPerHour, seconds{3600}, now);
        }
        case MCPPermissionTier::Write:
            return checkBucket(writeCount_[id], limits_.writesPerMinute, seconds{60}, now);
        case MCPPermissionTier::Ddl:
            return checkBucket(ddlCount_[id], limits_.ddlPerMinute, seconds{60}, now);
    }
    return RateLimitResult::allowed();
}

void RateLimiter::recordUsage(MCPPermissionTier tier, const std::string& id) {
    std::lock_guard lk(mu_);
    auto now = Clock::now();
    switch (tier) {
        case MCPPermissionTier::Schema:
        case MCPPermissionTier::Read:
        case MCPPermissionTier::Advanced:
            recordBucket(queryCount_[id], now, seconds{3600});
            break;
        case MCPPermissionTier::Write:
            recordBucket(writeCount_[id], now, seconds{60});
            break;
        case MCPPermissionTier::Ddl:
            recordBucket(ddlCount_[id], now, seconds{60});
            break;
    }
}

void RateLimiter::resetLimits(const std::string& id) {
    std::lock_guard lk(mu_);
    queryCount_.erase(id);
    writeCount_.erase(id);
    ddlCount_.erase(id);
}

void RateLimiter::resetAll() {
    std::lock_guard lk(mu_);
    queryCount_.clear();
    writeCount_.clear();
    ddlCount_.clear();
}

}  // namespace gridex::mcp
