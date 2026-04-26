#pragma once

#include <string_view>

namespace gridex {

enum class MCPPermissionTier {
    Schema   = 1,
    Read     = 2,
    Write    = 3,
    Ddl      = 4,
    Advanced = 5,
};

inline int tierRawValue(MCPPermissionTier t) { return static_cast<int>(t); }

inline std::string_view displayName(MCPPermissionTier t) {
    switch (t) {
        case MCPPermissionTier::Schema:   return "Schema";
        case MCPPermissionTier::Read:     return "Read";
        case MCPPermissionTier::Write:    return "Write";
        case MCPPermissionTier::Ddl:      return "DDL";
        case MCPPermissionTier::Advanced: return "Advanced";
    }
    return "";
}

inline bool requiresApproval(MCPPermissionTier t) {
    switch (t) {
        case MCPPermissionTier::Schema:
        case MCPPermissionTier::Read:
            return false;
        case MCPPermissionTier::Write:
        case MCPPermissionTier::Ddl:
        case MCPPermissionTier::Advanced:
            return true;
    }
    return true;
}

inline bool isReadOnly(MCPPermissionTier t) {
    return t == MCPPermissionTier::Schema || t == MCPPermissionTier::Read;
}

}
