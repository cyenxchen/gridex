#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Models/Schema/SchemaSnapshot.h"

namespace gridex {

// Mirrors macos/Core/Protocols/Database/SchemaInspectable.swift.
// Implemented by adapters to feed AI context and schema browser views.
class ISchemaInspectable {
public:
    virtual ~ISchemaInspectable() = default;

    virtual SchemaSnapshot fullSchemaSnapshot(const std::optional<std::string>& database) = 0;
    virtual std::vector<ColumnStatistics> columnStatistics(const std::string& table,
                                                           const std::optional<std::string>& schema,
                                                           int sampleSize) = 0;
    virtual int tableRowCount(const std::string& table,
                              const std::optional<std::string>& schema) = 0;
    virtual std::optional<std::int64_t> tableSizeBytes(const std::string& table,
                                                       const std::optional<std::string>& schema) = 0;
    virtual std::vector<QueryStatisticsEntry> queryStatistics() = 0;
    virtual std::vector<std::string> primaryKeyColumns(const std::string& table,
                                                       const std::optional<std::string>& schema) = 0;
};

}
