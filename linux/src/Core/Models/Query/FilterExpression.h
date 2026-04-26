#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Enums/SQLDialect.h"
#include "Core/Models/Database/RowValue.h"

namespace gridex {

enum class FilterOperator {
    Equal,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual,
    Like,
    NotLike,
    IsNull,
    IsNotNull,
    In,
};

inline constexpr std::array<FilterOperator, 11> kAllFilterOperators = {
    FilterOperator::Equal,          FilterOperator::NotEqual,
    FilterOperator::GreaterThan,    FilterOperator::LessThan,
    FilterOperator::GreaterOrEqual, FilterOperator::LessOrEqual,
    FilterOperator::Like,           FilterOperator::NotLike,
    FilterOperator::IsNull,         FilterOperator::IsNotNull,
    FilterOperator::In,
};

inline std::string_view rawValue(FilterOperator op) {
    switch (op) {
        case FilterOperator::Equal:          return "=";
        case FilterOperator::NotEqual:       return "!=";
        case FilterOperator::GreaterThan:    return ">";
        case FilterOperator::LessThan:       return "<";
        case FilterOperator::GreaterOrEqual: return ">=";
        case FilterOperator::LessOrEqual:    return "<=";
        case FilterOperator::Like:           return "LIKE";
        case FilterOperator::NotLike:        return "NOT LIKE";
        case FilterOperator::IsNull:         return "IS NULL";
        case FilterOperator::IsNotNull:      return "IS NOT NULL";
        case FilterOperator::In:             return "IN";
    }
    return "";
}

enum class FilterCombinator {
    And,
    Or,
};

inline std::string_view rawValue(FilterCombinator c) {
    switch (c) {
        case FilterCombinator::And: return "AND";
        case FilterCombinator::Or:  return "OR";
    }
    return "";
}

namespace detail {

inline std::string escapeFilterValue(const RowValue& value, SQLDialect dialect) {
    if (value.isNull()) return "NULL";

    auto escapeSingle = [](std::string_view v) {
        std::string out;
        out.reserve(v.size() + 2);
        out.push_back('\'');
        for (char c : v) {
            if (c == '\'') out.push_back('\'');
            out.push_back(c);
        }
        out.push_back('\'');
        return out;
    };

    if (value.isString()) {
        const auto& s = value.asString();
        // Pass numeric-looking strings through unquoted for compatibility with macOS behavior.
        try {
            std::size_t pos = 0;
            (void)std::stoll(s, &pos);
            if (pos == s.size()) return s;
        } catch (...) {}
        try {
            std::size_t pos = 0;
            (void)std::stod(s, &pos);
            if (pos == s.size()) return s;
        } catch (...) {}
        return escapeSingle(s);
    }
    if (value.isInteger()) return std::to_string(value.asInteger());
    if (value.isDouble())  return std::to_string(value.asDouble());
    if (value.isBoolean()) return value.asBoolean() ? "TRUE" : "FALSE";
    if (value.isDate())    return "'" + formatTimestampUtc(value.asDate()) + "'";
    if (value.isUuid())    return "'" + value.asUuid() + "'";
    if (value.isJson()) {
        const auto quoted = escapeSingle(value.asJson());
        // H11: PG requires ::jsonb cast for json comparisons
        return dialect == SQLDialect::PostgreSQL ? quoted + "::jsonb" : quoted;
    }
    // data, array → NULL (matches Swift behavior)
    return "NULL";
}

}

struct FilterCondition {
    std::string column;
    FilterOperator op = FilterOperator::Equal;
    RowValue value;

    [[nodiscard]] std::string toSQL(SQLDialect dialect) const {
        const std::string qc = quoteIdentifier(dialect, column);
        const std::string val = detail::escapeFilterValue(value, dialect);
        switch (op) {
            case FilterOperator::Equal:          return qc + " = " + val;
            case FilterOperator::NotEqual:       return qc + " != " + val;
            case FilterOperator::GreaterThan:    return qc + " > " + val;
            case FilterOperator::LessThan:       return qc + " < " + val;
            case FilterOperator::GreaterOrEqual: return qc + " >= " + val;
            case FilterOperator::LessOrEqual:    return qc + " <= " + val;
            case FilterOperator::Like:           return qc + " LIKE " + val;
            case FilterOperator::NotLike:        return qc + " NOT LIKE " + val;
            case FilterOperator::IsNull:         return qc + " IS NULL";
            case FilterOperator::IsNotNull:      return qc + " IS NOT NULL";
            case FilterOperator::In:             return qc + " IN (" + val + ")";
        }
        return qc;
    }
};

struct FilterExpression {
    std::vector<FilterCondition> conditions;
    FilterCombinator combinator = FilterCombinator::And;

    [[nodiscard]] std::string toSQL(SQLDialect dialect) const {
        if (conditions.empty()) return {};
        const std::string sep = combinator == FilterCombinator::And ? " AND " : " OR ";
        std::string out;
        for (std::size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0) out += sep;
            out += conditions[i].toSQL(dialect);
        }
        return out;
    }
};

}
