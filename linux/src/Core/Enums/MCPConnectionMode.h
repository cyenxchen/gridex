#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace gridex {

enum class MCPConnectionMode {
    Locked,
    ReadOnly,
    ReadWrite,
};

inline constexpr std::array<MCPConnectionMode, 3> kAllMCPConnectionModes = {
    MCPConnectionMode::Locked,
    MCPConnectionMode::ReadOnly,
    MCPConnectionMode::ReadWrite,
};

inline std::string_view rawValue(MCPConnectionMode m) {
    switch (m) {
        case MCPConnectionMode::Locked:    return "locked";
        case MCPConnectionMode::ReadOnly:  return "read_only";
        case MCPConnectionMode::ReadWrite: return "read_write";
    }
    return "";
}

inline std::optional<MCPConnectionMode> mcpConnectionModeFromRaw(std::string_view raw) {
    if (raw == "locked")     return MCPConnectionMode::Locked;
    if (raw == "read_only")  return MCPConnectionMode::ReadOnly;
    if (raw == "read_write") return MCPConnectionMode::ReadWrite;
    return std::nullopt;
}

inline std::string_view displayName(MCPConnectionMode m) {
    switch (m) {
        case MCPConnectionMode::Locked:    return "Locked";
        case MCPConnectionMode::ReadOnly:  return "Read-only";
        case MCPConnectionMode::ReadWrite: return "Read-write";
    }
    return "";
}

inline std::string_view description(MCPConnectionMode m) {
    switch (m) {
        case MCPConnectionMode::Locked:
            return "AI cannot access this connection";
        case MCPConnectionMode::ReadOnly:
            return "AI can query but not modify (recommended for production)";
        case MCPConnectionMode::ReadWrite:
            return "AI can modify with your approval (use for dev only)";
    }
    return "";
}

inline bool allowsTier1(MCPConnectionMode m) { return m != MCPConnectionMode::Locked; }
inline bool allowsTier2(MCPConnectionMode m) { return m != MCPConnectionMode::Locked; }
inline bool allowsTier3(MCPConnectionMode m) { return m == MCPConnectionMode::ReadWrite; }
inline bool allowsTier4(MCPConnectionMode m) { return m == MCPConnectionMode::ReadWrite; }
inline bool allowsTier5(MCPConnectionMode m) { return m != MCPConnectionMode::Locked; }

}
