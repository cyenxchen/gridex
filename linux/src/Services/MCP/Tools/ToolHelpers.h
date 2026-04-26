#pragma once

// Shared helpers for MCP tool implementations.

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "Core/Enums/SQLDialect.h"
#include "Core/Models/Database/RowValue.h"

namespace gridex::mcp {

inline RowValue jsonToRowValue(const nlohmann::json& v) {
    if (v.is_null())           return RowValue::makeNull();
    if (v.is_boolean())        return RowValue::makeBoolean(v.get<bool>());
    if (v.is_number_integer()) return RowValue::makeInteger(v.get<std::int64_t>());
    if (v.is_number_float())   return RowValue::makeDouble(v.get<double>());
    if (v.is_string())         return RowValue::makeString(v.get<std::string>());
    if (v.is_array() || v.is_object()) return RowValue::makeJson(v.dump());
    return RowValue::makeNull();
}

inline std::string formatValueForSQL(const nlohmann::json& v, SQLDialect dialect) {
    if (v.is_null())           return "NULL";
    if (v.is_boolean()) {
        bool b = v.get<bool>();
        return dialect == SQLDialect::PostgreSQL
            ? (b ? "true" : "false")
            : (b ? "1" : "0");
    }
    if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
    if (v.is_number_float())   return std::to_string(v.get<double>());
    auto escape = [](const std::string& s) {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('\'');
        for (char c : s) {
            if (c == '\'') out.push_back('\'');
            out.push_back(c);
        }
        out.push_back('\'');
        return out;
    };
    if (v.is_string()) return escape(v.get<std::string>());
    return escape(v.dump());
}

inline std::string qualifiedIdentifier(SQLDialect dialect,
                                       const std::string& name,
                                       const std::optional<std::string>& schema) {
    if (schema && !schema->empty()) {
        return quoteIdentifier(dialect, *schema) + "." + quoteIdentifier(dialect, name);
    }
    return quoteIdentifier(dialect, name);
}

inline std::string toLowerAscii(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return s;
}

inline std::string toUpperAscii(std::string s) {
    for (auto& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    }
    return s;
}

inline std::string trimSpaces(std::string s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline bool startsWith(const std::string& s, std::string_view prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

}  // namespace gridex::mcp
