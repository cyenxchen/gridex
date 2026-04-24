#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Enums/DatabaseType.h"
#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Models/Database/RowValue.h"
#include "Core/Models/Query/FilterExpression.h"
#include "Core/Models/Query/QueryParameter.h"
#include "Core/Models/Query/QueryResult.h"
#include "Core/Models/Query/SortDescriptor.h"
#include "Core/Models/Schema/SchemaSnapshot.h"

namespace gridex {

// Interface mirrors macos/Core/Protocols/Database/DatabaseAdapter.swift.
// Adapters are synchronous; higher layers (QueryEngine) wrap calls onto threads.
// Methods throw gridex::GridexError subclasses on failure.
class IDatabaseAdapter {
public:
    virtual ~IDatabaseAdapter() = default;

    // Identity
    [[nodiscard]] virtual DatabaseType databaseType() const noexcept = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    // Connection lifecycle
    virtual void connect(const ConnectionConfig& config,
                         const std::optional<std::string>& password) = 0;
    virtual void disconnect() = 0;
    virtual bool testConnection(const ConnectionConfig& config,
                                const std::optional<std::string>& password) = 0;

    // Query execution
    virtual QueryResult execute(const std::string& query,
                                const std::vector<QueryParameter>& parameters) = 0;
    virtual QueryResult executeRaw(const std::string& sql) = 0;

    // Parameterized execution with RowValue. Default inlines values into SQL when
    // not overridden; adapters should override using native driver bindings.
    virtual QueryResult executeWithRowValues(const std::string& sql,
                                             const std::vector<RowValue>& parameters);

    // Schema inspection
    virtual std::vector<std::string> listDatabases() = 0;
    virtual std::vector<std::string> listSchemas(const std::optional<std::string>& database) = 0;
    virtual std::vector<TableInfo>  listTables(const std::optional<std::string>& schema) = 0;
    virtual std::vector<ViewInfo>   listViews(const std::optional<std::string>& schema) = 0;
    virtual TableDescription        describeTable(const std::string& name,
                                                  const std::optional<std::string>& schema) = 0;
    virtual std::vector<IndexInfo>      listIndexes(const std::string& table,
                                                    const std::optional<std::string>& schema) = 0;
    virtual std::vector<ForeignKeyInfo> listForeignKeys(const std::string& table,
                                                        const std::optional<std::string>& schema) = 0;
    virtual std::vector<std::string>    listFunctions(const std::optional<std::string>& schema) = 0;
    virtual std::string getFunctionSource(const std::string& name,
                                          const std::optional<std::string>& schema) = 0;

    // Stored procedures — default no-op (only MSSQL overrides in practice).
    virtual std::vector<std::string> listProcedures(const std::optional<std::string>& /*schema*/) { return {}; }
    virtual std::string getProcedureSource(const std::string& /*name*/,
                                           const std::optional<std::string>& /*schema*/) { return {}; }
    virtual std::vector<std::string> listProcedureParameters(const std::string& /*name*/,
                                                             const std::optional<std::string>& /*schema*/) { return {}; }

    // Data manipulation
    virtual QueryResult insertRow(const std::string& table,
                                  const std::optional<std::string>& schema,
                                  const std::unordered_map<std::string, RowValue>& values) = 0;
    virtual QueryResult updateRow(const std::string& table,
                                  const std::optional<std::string>& schema,
                                  const std::unordered_map<std::string, RowValue>& set,
                                  const std::unordered_map<std::string, RowValue>& where) = 0;
    virtual QueryResult deleteRow(const std::string& table,
                                  const std::optional<std::string>& schema,
                                  const std::unordered_map<std::string, RowValue>& where) = 0;

    // Transactions
    virtual void beginTransaction() = 0;
    virtual void commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;

    // Pagination
    virtual QueryResult fetchRows(const std::string& table,
                                  const std::optional<std::string>& schema,
                                  const std::optional<std::vector<std::string>>& columns,
                                  const std::optional<FilterExpression>& where,
                                  const std::optional<std::vector<QuerySortDescriptor>>& orderBy,
                                  int limit,
                                  int offset) = 0;

    // Database management — default implementations use DDL via executeRaw.
    virtual void createDatabase(const std::string& name);
    virtual void dropDatabase(const std::string& name);

    // Server info
    virtual std::string serverVersion() = 0;
    virtual std::optional<std::string> currentDatabase() = 0;
};

// Inline default implementations mirroring Swift protocol extensions.

inline QueryResult IDatabaseAdapter::executeWithRowValues(const std::string& sql,
                                                          const std::vector<RowValue>& parameters) {
    // WARNING: default impl inlines values into SQL using naive single-quote-doubling.
    // Safe for PostgreSQL + SQLite which don't interpret backslash by default.
    // MySQL adapters MUST override with mysql_real_escape_string-based inlining.
    auto inlineValue = [](const RowValue& v) -> std::string {
        if (v.isNull())    return "NULL";
        auto quote = [](std::string_view src) {
            std::string out;
            out.reserve(src.size() + 2);
            out.push_back('\'');
            for (char c : src) {
                if (c == '\'') out.push_back('\'');
                out.push_back(c);
            }
            out.push_back('\'');
            return out;
        };
        if (v.isString())  return quote(v.asString());
        if (v.isInteger()) return std::to_string(v.asInteger());
        if (v.isDouble())  return std::to_string(v.asDouble());
        if (v.isBoolean()) return v.asBoolean() ? "1" : "0";
        if (v.isDate())    return "'" + formatTimestampUtc(v.asDate()) + "'";
        if (v.isUuid())    return "'" + v.asUuid() + "'";
        if (v.isJson())    return quote(v.asJson());
        return "NULL"; // data, array
    };

    // Single-pass tokenizer: walks SQL, skips over string/identifier literals and comments,
    // and substitutes $N or ? placeholders with inlined values.
    std::string out;
    out.reserve(sql.size() + 64);
    std::size_t i = 0;
    std::size_t qIndex = 0;                  // counts `?` placeholders already consumed
    const std::size_t n = sql.size();
    while (i < n) {
        const char c = sql[i];
        // Skip string / quoted identifier literals verbatim.
        if (c == '\'' || c == '"' || c == '`') {
            const char quote = c;
            out.push_back(c); ++i;
            while (i < n) {
                if (sql[i] == '\\' && i + 1 < n) { out.push_back(sql[i]); out.push_back(sql[i + 1]); i += 2; continue; }
                if (sql[i] == quote) {
                    // Handle doubled quote as escape: ''  ""  ``
                    if (i + 1 < n && sql[i + 1] == quote) { out.push_back(quote); out.push_back(quote); i += 2; continue; }
                    out.push_back(quote); ++i; break;
                }
                out.push_back(sql[i]); ++i;
            }
            continue;
        }
        // Skip line + block comments verbatim.
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
            while (i < n && sql[i] != '\n') { out.push_back(sql[i++]); }
            continue;
        }
        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
            out.push_back(sql[i++]); out.push_back(sql[i++]);
            while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) { out.push_back(sql[i++]); }
            if (i + 1 < n) { out.push_back(sql[i++]); out.push_back(sql[i++]); }
            continue;
        }
        // `?` placeholder (MySQL/SQLite).
        if (c == '?') {
            if (qIndex < parameters.size()) {
                out += inlineValue(parameters[qIndex++]);
            } else {
                out.push_back(c);
            }
            ++i;
            continue;
        }
        // `$N` placeholder (PostgreSQL). Read digits and substitute when N <= params.
        if (c == '$' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1]))) {
            std::size_t j = i + 1;
            std::size_t idx = 0;
            while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) {
                idx = idx * 10 + static_cast<std::size_t>(sql[j] - '0');
                ++j;
            }
            if (idx >= 1 && idx <= parameters.size()) {
                out += inlineValue(parameters[idx - 1]);
                i = j;
                continue;
            }
            // Out-of-range or literal $0 — leave as-is.
            out.push_back(c); ++i;
            continue;
        }
        out.push_back(c); ++i;
    }
    return executeRaw(out);
}

inline void IDatabaseAdapter::createDatabase(const std::string& name) {
    const auto quoted = quoteIdentifier(sqlDialect(databaseType()), name);
    (void)executeRaw("CREATE DATABASE " + quoted);
}

inline void IDatabaseAdapter::dropDatabase(const std::string& name) {
    const auto quoted = quoteIdentifier(sqlDialect(databaseType()), name);
    (void)executeRaw("DROP DATABASE " + quoted);
}

}
