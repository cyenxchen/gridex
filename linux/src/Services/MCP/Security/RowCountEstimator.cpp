#include "Services/MCP/Security/RowCountEstimator.h"

namespace gridex::mcp {

int RowCountEstimator::estimate(IDatabaseAdapter& adapter,
                                const std::string& qualifiedTable,
                                const std::string& whereClause,
                                const ConnectionConfig& config) {
    if (!isSQL(config.databaseType)) return 0;
    const std::string sql = "SELECT COUNT(*) FROM " + qualifiedTable + " WHERE " + whereClause;
    try {
        auto result = adapter.executeRaw(sql);
        if (result.rows.empty() || result.rows.front().empty()) return 0;
        const auto& v = result.rows.front().front();
        if (v.isInteger()) return static_cast<int>(v.asInteger());
        if (v.isString()) {
            try { return std::stoi(v.asString()); } catch (...) { return 0; }
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

}  // namespace gridex::mcp
