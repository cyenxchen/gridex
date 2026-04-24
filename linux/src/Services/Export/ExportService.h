#pragma once

#include <string>

#include "Core/Models/Query/QueryResult.h"

namespace gridex {

// Static export helpers: CSV (RFC 4180), JSON array-of-objects, SQL INSERT.
// All methods throw std::runtime_error on I/O failure.
class ExportService {
public:
    ExportService() = delete;

    static void exportToCsv(const QueryResult& result,
                            const std::string& filePath);

    static void exportToJson(const QueryResult& result,
                             const std::string& filePath);

    static void exportToSql(const QueryResult& result,
                            const std::string& tableName,
                            const std::string& filePath);
};

}
