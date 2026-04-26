#pragma once

#include <string>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex::mcp {

// Pre-approval row count estimation for write tools. Runs against the same
// (already validated & quoted) SQL shape that the write tool will execute, so it
// never reconstructs SQL from user-supplied substrings.
struct RowCountEstimator {
    static int estimate(IDatabaseAdapter& adapter,
                        const std::string& qualifiedTable,
                        const std::string& whereClause,
                        const ConnectionConfig& config);
};

}  // namespace gridex::mcp
