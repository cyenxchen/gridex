#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Models/Database/RowValue.h"

namespace gridex {

enum class QueryType {
    Select,
    Insert,
    Update,
    Delete,
    DDL,
    Other,
};

inline std::string_view rawValue(QueryType t) {
    switch (t) {
        case QueryType::Select: return "select";
        case QueryType::Insert: return "insert";
        case QueryType::Update: return "update";
        case QueryType::Delete: return "delete";
        case QueryType::DDL:    return "ddl";
        case QueryType::Other:  return "other";
    }
    return "";
}

struct ColumnHeader {
    std::string name;
    std::string dataType{"unknown"};
    bool isNullable = true;
    std::optional<std::string> tableName;

    bool operator==(const ColumnHeader&) const noexcept = default;
};

struct QueryResult {
    std::vector<ColumnHeader> columns;
    std::vector<std::vector<RowValue>> rows;
    int rowsAffected = 0;
    std::chrono::duration<double> executionTime{};
    QueryType queryType = QueryType::Other;

    [[nodiscard]] std::size_t rowCount() const noexcept { return rows.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return rows.empty(); }
};

}
