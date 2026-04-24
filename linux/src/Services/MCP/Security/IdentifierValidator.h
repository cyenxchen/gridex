#pragma once

// Allowlist check for SQL identifiers received from MCP clients. Rejects anything
// outside strict ASCII letters/digits/underscore with a letter-or-underscore
// first char. Defense-in-depth on top of SQLDialect::quoteIdentifier.

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "Services/MCP/Protocol.h"

namespace gridex::mcp {

struct IdentifierValidator {
    static constexpr std::size_t kMaxLength = 128;

    static bool isValid(std::string_view id) {
        if (id.empty() || id.size() > kMaxLength) return false;
        auto letter = [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        };
        auto digit = [](unsigned char c) { return c >= '0' && c <= '9'; };
        if (!letter(static_cast<unsigned char>(id[0]))) return false;
        for (std::size_t k = 1; k < id.size(); ++k) {
            unsigned char c = static_cast<unsigned char>(id[k]);
            if (!letter(c) && !digit(c)) return false;
        }
        return true;
    }

    static void validate(std::string_view id, const std::string& role = "identifier") {
        if (!isValid(id)) {
            throw MCPToolError::invalidParameters(
                role + " '" + std::string(id) + "' contains invalid characters. Allowed: "
                "letters, digits, underscore; must start with a letter or underscore; max "
                + std::to_string(kMaxLength) + " chars.");
        }
    }

    // Reads `table_name` and optional `schema` from params and validates both.
    static std::pair<std::string, std::optional<std::string>> extractTableAndSchema(const nlohmann::json& params) {
        if (!params.contains("table_name") || !params["table_name"].is_string()) {
            throw MCPToolError::invalidParameters("table_name is required");
        }
        std::string table = params["table_name"].get<std::string>();
        validate(table, "table_name");

        std::optional<std::string> schema;
        if (params.contains("schema") && params["schema"].is_string()) {
            schema = params["schema"].get<std::string>();
            validate(*schema, "schema");
        }
        return {std::move(table), std::move(schema)};
    }
};

}  // namespace gridex::mcp
