#include "Data/Adapters/SQLite/SqliteAdapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>
#include <utility>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

// Use SQLITE_TRANSIENT directly — it is a macro that cannot be a constexpr value.

std::string toUpperTrim(std::string_view s) {
    const auto firstNonWs = s.find_first_not_of(" \t\n\r");
    if (firstNonWs == std::string_view::npos) return {};
    const auto lastNonWs = s.find_last_not_of(" \t\n\r");
    std::string out(s.substr(firstNonWs, lastNonWs - firstNonWs + 1));
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return out;
}

QueryType detectQueryType(std::string_view sql) {
    const auto u = toUpperTrim(sql);
    auto starts = [&](std::string_view p) { return u.rfind(p, 0) == 0; };
    if (starts("SELECT") || starts("PRAGMA") || starts("EXPLAIN")) return QueryType::Select;
    if (starts("INSERT")) return QueryType::Insert;
    if (starts("UPDATE")) return QueryType::Update;
    if (starts("DELETE")) return QueryType::Delete;
    if (starts("CREATE") || starts("ALTER") || starts("DROP")) return QueryType::DDL;
    return QueryType::Other;
}

std::string quoteIdent(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for (char c : name) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void bindRowValue(sqlite3_stmt* stmt, int idx, const RowValue& v) {
    if (v.isNull()) { sqlite3_bind_null(stmt, idx); return; }
    if (v.isString())  { sqlite3_bind_text(stmt, idx, v.asString().c_str(), -1, SQLITE_TRANSIENT); return; }
    if (v.isInteger()) { sqlite3_bind_int64(stmt, idx, v.asInteger()); return; }
    if (v.isDouble())  { sqlite3_bind_double(stmt, idx, v.asDouble()); return; }
    if (v.isBoolean()) { sqlite3_bind_int(stmt, idx, v.asBoolean() ? 1 : 0); return; }
    if (v.isDate())    {
        const auto s = formatTimestampUtc(v.asDate());
        sqlite3_bind_text(stmt, idx, s.c_str(), -1, SQLITE_TRANSIENT);
        return;
    }
    if (v.isData()) {
        const auto& b = v.asData();
        sqlite3_bind_blob(stmt, idx, b.data(), static_cast<int>(b.size()), SQLITE_TRANSIENT);
        return;
    }
    if (v.isJson()) { sqlite3_bind_text(stmt, idx, v.asJson().c_str(), -1, SQLITE_TRANSIENT); return; }
    if (v.isUuid()) { sqlite3_bind_text(stmt, idx, v.asUuid().c_str(), -1, SQLITE_TRANSIENT); return; }
    // array / fallback: serialize via description
    const auto s = v.description();
    sqlite3_bind_text(stmt, idx, s.c_str(), -1, SQLITE_TRANSIENT);
}

RowValue readColumn(sqlite3_stmt* stmt, int idx) {
    switch (sqlite3_column_type(stmt, idx)) {
        case SQLITE_NULL:    return RowValue::makeNull();
        case SQLITE_INTEGER: return RowValue::makeInteger(sqlite3_column_int64(stmt, idx));
        case SQLITE_FLOAT:   return RowValue::makeDouble(sqlite3_column_double(stmt, idx));
        case SQLITE_TEXT: {
            const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, idx));
            const int n = sqlite3_column_bytes(stmt, idx);
            return RowValue::makeString(txt ? std::string(txt, static_cast<std::size_t>(n)) : std::string{});
        }
        case SQLITE_BLOB: {
            const int n = sqlite3_column_bytes(stmt, idx);
            const void* ptr = sqlite3_column_blob(stmt, idx);
            if (!ptr || n <= 0) return RowValue::makeNull();
            const auto* bytes = static_cast<const std::uint8_t*>(ptr);
            return RowValue::makeData(Bytes(bytes, bytes + n));
        }
        default: return RowValue::makeNull();
    }
}

std::string errorMessage(sqlite3* db) {
    if (!db) return "Unknown SQLite error";
    const auto* msg = sqlite3_errmsg(db);
    return msg ? std::string(msg) : std::string{};
}

}

SqliteAdapter::SqliteAdapter() = default;

SqliteAdapter::~SqliteAdapter() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    connected_.store(false, std::memory_order_release);
}

void SqliteAdapter::connect(const ConnectionConfig& config, const std::optional<std::string>& /*password*/) {
    if (!config.filePath || config.filePath->empty()) {
        throw ConnectionError("SQLite: no file path");
    }

    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(config.filePath->c_str(), &handle, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = handle ? errorMessage(handle) : sqlite3_errstr(rc);
        sqlite3_close_v2(handle);
        throw ConnectionError("SQLite open failed: " + msg);
    }

    sqlite3_exec(handle, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(handle, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);

    db_ = handle;
    filePath_ = *config.filePath;
    connected_.store(true, std::memory_order_release);
}

void SqliteAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
    filePath_.clear();
    connected_.store(false, std::memory_order_release);
}

bool SqliteAdapter::testConnection(const ConnectionConfig& config, const std::optional<std::string>& /*password*/) {
    if (!config.filePath || config.filePath->empty()) {
        throw ConnectionError("SQLite: no file path");
    }
    sqlite3* test = nullptr;
    const int rc = sqlite3_open_v2(config.filePath->c_str(), &test, SQLITE_OPEN_READONLY, nullptr);
    const bool ok = rc == SQLITE_OK;
    if (!ok) {
        std::string msg = test ? errorMessage(test) : sqlite3_errstr(rc);
        sqlite3_close_v2(test);
        throw ConnectionError("SQLite test failed: " + msg);
    }
    sqlite3_close_v2(test);
    return true;
}

void SqliteAdapter::ensureConnected() const {
    // connected_ is atomic; db_ is stable once connected_ is true under our mutex discipline.
    if (!connected_.load(std::memory_order_acquire) || !db_) {
        throw QueryError("Not connected to SQLite database");
    }
}

QueryResult SqliteAdapter::executeInternal(const std::string& sql, const std::vector<RowValue>& values) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
        const auto msg = errorMessage(db_);
        if (stmt) sqlite3_finalize(stmt);
        throw QueryError("Prepare failed: " + msg);
    }

    struct Finalizer {
        sqlite3_stmt* s;
        ~Finalizer() { if (s) sqlite3_finalize(s); }
    } fin{stmt};

    for (std::size_t i = 0; i < values.size(); ++i) {
        bindRowValue(stmt, static_cast<int>(i + 1), values[i]);
    }

    const auto qt = detectQueryType(sql);

    QueryResult result;
    result.queryType = qt;

    if (qt == QueryType::Select) {
        const int colCount = sqlite3_column_count(stmt);
        result.columns.reserve(static_cast<std::size_t>(colCount));
        for (int i = 0; i < colCount; ++i) {
            ColumnHeader h;
            const auto* name = sqlite3_column_name(stmt, i);
            const auto* decl = sqlite3_column_decltype(stmt, i);
            h.name = name ? std::string(name) : std::string{};
            h.dataType = decl ? std::string(decl) : std::string{"TEXT"};
            result.columns.push_back(std::move(h));
        }

        while (true) {
            const int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                std::vector<RowValue> row;
                row.reserve(static_cast<std::size_t>(colCount));
                for (int i = 0; i < colCount; ++i) row.push_back(readColumn(stmt, i));
                result.rows.push_back(std::move(row));
                continue;
            }
            if (rc == SQLITE_DONE) break;
            throw QueryError("Step failed: " + errorMessage(db_));
        }
        result.rowsAffected = static_cast<int>(result.rows.size());
    } else {
        const int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw QueryError("Execute failed: " + errorMessage(db_));
        }
        result.rowsAffected = sqlite3_changes(db_);
    }

    result.executionTime = std::chrono::steady_clock::now() - start;
    return result;
}

QueryResult SqliteAdapter::execute(const std::string& query, const std::vector<QueryParameter>& parameters) {
    std::vector<RowValue> values;
    values.reserve(parameters.size());
    for (const auto& p : parameters) values.push_back(p.value);
    return executeInternal(query, values);
}

QueryResult SqliteAdapter::executeRaw(const std::string& sql) {
    return executeInternal(sql, {});
}

std::string SqliteAdapter::lastBasename() const {
    if (filePath_.empty()) return "main";
    try {
        return std::filesystem::path(filePath_).filename().string();
    } catch (...) {
        return "main";
    }
}

std::vector<std::string> SqliteAdapter::listDatabases() {
    return {lastBasename()};
}

std::vector<std::string> SqliteAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    return {"main"};
}

std::vector<TableInfo> SqliteAdapter::listTables(const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name");
    std::vector<TableInfo> tables;
    tables.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        TableInfo info;
        info.name = row[0].asString();
        info.type = TableKind::Table;
        try { info.estimatedRowCount = tableRowCount(info.name, std::nullopt); } catch (...) {}
        tables.push_back(std::move(info));
    }
    return tables;
}

std::vector<ViewInfo> SqliteAdapter::listViews(const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw("SELECT name, sql FROM sqlite_master WHERE type='view' ORDER BY name");
    std::vector<ViewInfo> views;
    views.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        ViewInfo v;
        v.name = row[0].asString();
        if (row.size() > 1) {
            auto s = row[1].tryStringValue();
            if (s) v.definition = std::move(*s);
        }
        views.push_back(std::move(v));
    }
    return views;
}

std::vector<ColumnInfo> SqliteAdapter::describeColumns(const std::string& table) {
    const auto r = executeRaw("PRAGMA table_info(" + quoteIdent(table) + ")");
    std::vector<ColumnInfo> cols;
    cols.reserve(r.rows.size());
    for (std::size_t idx = 0; idx < r.rows.size(); ++idx) {
        const auto& row = r.rows[idx];
        ColumnInfo c;
        if (row.size() < 6) continue;
        c.name = row[1].tryStringValue().value_or(std::string{});
        c.dataType = row[2].tryStringValue().value_or(std::string{"TEXT"});
        c.isNullable = row[3].tryIntValue().value_or(0) == 0;
        if (!row[4].isNull()) c.defaultValue = row[4].tryStringValue();
        const bool pk = row[5].tryIntValue().value_or(0) != 0;
        c.isPrimaryKey = pk;
        std::string typeUpper = c.dataType;
        std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(),
                       [](unsigned char ch){ return std::toupper(ch); });
        c.isAutoIncrement = pk && typeUpper == "INTEGER";
        c.ordinalPosition = static_cast<int>(row[0].tryIntValue().value_or(static_cast<std::int64_t>(idx)));
        cols.push_back(std::move(c));
    }
    return cols;
}

TableDescription SqliteAdapter::describeTable(const std::string& name,
                                              const std::optional<std::string>& /*schema*/) {
    TableDescription desc;
    desc.name = name;
    desc.columns = describeColumns(name);
    desc.indexes = listIndexes(name, std::nullopt);
    desc.foreignKeys = listForeignKeys(name, std::nullopt);
    try { desc.estimatedRowCount = tableRowCount(name, std::nullopt); } catch (...) {}
    return desc;
}

std::vector<IndexInfo> SqliteAdapter::listIndexes(const std::string& table,
                                                  const std::optional<std::string>& /*schema*/) {
    std::string escaped;
    escaped.reserve(table.size());
    for (char c : table) {
        if (c == '\'') escaped.push_back('\'');
        escaped.push_back(c);
    }
    const std::string sql =
        "SELECT il.name, il.\"unique\", GROUP_CONCAT(ii.name) "
        "FROM pragma_index_list('" + escaped + "') il "
        "JOIN pragma_index_info(il.name) ii "
        "GROUP BY il.name, il.\"unique\" ORDER BY il.name, ii.seqno";
    const auto r = executeRaw(sql);
    std::vector<IndexInfo> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (row.size() < 3) continue;
        IndexInfo idx;
        idx.name = row[0].tryStringValue().value_or(std::string{});
        idx.isUnique = row[1].tryStringValue().value_or("0") == "1";
        const auto cols = row[2].tryStringValue().value_or(std::string{});
        std::string cur;
        for (char c : cols) {
            if (c == ',') { if (!cur.empty()) idx.columns.push_back(std::move(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) idx.columns.push_back(std::move(cur));
        idx.type = std::string{"btree"};
        idx.tableName = table;
        out.push_back(std::move(idx));
    }
    return out;
}

std::vector<ForeignKeyInfo> SqliteAdapter::listForeignKeys(const std::string& table,
                                                            const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw("PRAGMA foreign_key_list(" + quoteIdent(table) + ")");
    std::vector<ForeignKeyInfo> out;
    out.reserve(r.rows.size());

    auto parseAction = [](std::string_view s) {
        if (s == "CASCADE")     return ForeignKeyAction::Cascade;
        if (s == "SET NULL")    return ForeignKeyAction::SetNull;
        if (s == "SET DEFAULT") return ForeignKeyAction::SetDefault;
        if (s == "RESTRICT")    return ForeignKeyAction::Restrict;
        return ForeignKeyAction::NoAction;
    };

    for (const auto& row : r.rows) {
        if (row.size() < 7) continue;
        ForeignKeyInfo fk;
        fk.columns.push_back(row[3].tryStringValue().value_or(std::string{}));
        fk.referencedTable = row[2].tryStringValue().value_or(std::string{});
        fk.referencedColumns.push_back(row[4].tryStringValue().value_or(std::string{}));
        fk.onUpdate = parseAction(row[5].tryStringValue().value_or("NO ACTION"));
        fk.onDelete = parseAction(row[6].tryStringValue().value_or("NO ACTION"));
        out.push_back(std::move(fk));
    }
    return out;
}

std::vector<std::string> SqliteAdapter::listFunctions(const std::optional<std::string>& /*schema*/) {
    return {};
}

std::string SqliteAdapter::getFunctionSource(const std::string& /*name*/,
                                             const std::optional<std::string>& /*schema*/) {
    throw QueryError("SQLite does not support stored functions");
}

QueryResult SqliteAdapter::insertRow(const std::string& table,
                                     const std::optional<std::string>& /*schema*/,
                                     const std::unordered_map<std::string, RowValue>& values) {
    if (values.empty()) throw QueryError("insertRow: values map is empty");
    std::string cols, placeholders;
    std::vector<QueryParameter> params;
    params.reserve(values.size());
    bool first = true;
    for (const auto& [k, v] : values) {
        if (!first) { cols += ", "; placeholders += ", "; }
        cols += quoteIdent(k);
        placeholders += "?";
        params.emplace_back(v);
        first = false;
    }
    return execute("INSERT INTO " + quoteIdent(table) + " (" + cols + ") VALUES (" + placeholders + ")",
                   params);
}

QueryResult SqliteAdapter::updateRow(const std::string& table,
                                     const std::optional<std::string>& /*schema*/,
                                     const std::unordered_map<std::string, RowValue>& set,
                                     const std::unordered_map<std::string, RowValue>& where) {
    if (set.empty() || where.empty()) throw QueryError("updateRow: set or where is empty");
    std::string setClause, whereClause;
    std::vector<QueryParameter> params;
    params.reserve(set.size() + where.size());

    bool first = true;
    for (const auto& [k, v] : set) {
        if (!first) setClause += ", ";
        setClause += quoteIdent(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += quoteIdent(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    return execute("UPDATE " + quoteIdent(table) + " SET " + setClause + " WHERE " + whereClause, params);
}

QueryResult SqliteAdapter::deleteRow(const std::string& table,
                                     const std::optional<std::string>& /*schema*/,
                                     const std::unordered_map<std::string, RowValue>& where) {
    if (where.empty()) throw QueryError("deleteRow: where is empty");
    std::string whereClause;
    std::vector<QueryParameter> params;
    params.reserve(where.size());
    bool first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += quoteIdent(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    return execute("DELETE FROM " + quoteIdent(table) + " WHERE " + whereClause, params);
}

void SqliteAdapter::beginTransaction()    { (void)executeRaw("BEGIN TRANSACTION"); }
void SqliteAdapter::commitTransaction()   { (void)executeRaw("COMMIT"); }
void SqliteAdapter::rollbackTransaction() { (void)executeRaw("ROLLBACK"); }

QueryResult SqliteAdapter::fetchRows(const std::string& table,
                                     const std::optional<std::string>& /*schema*/,
                                     const std::optional<std::vector<std::string>>& columns,
                                     const std::optional<FilterExpression>& where,
                                     const std::optional<std::vector<QuerySortDescriptor>>& orderBy,
                                     int limit, int offset) {
    std::string colList = "*";
    if (columns && !columns->empty()) {
        colList.clear();
        for (std::size_t i = 0; i < columns->size(); ++i) {
            if (i > 0) colList += ", ";
            colList += quoteIdent((*columns)[i]);
        }
    }
    std::string sql = "SELECT " + colList + " FROM " + quoteIdent(table);
    if (where && !where->conditions.empty()) {
        sql += " WHERE " + where->toSQL(SQLDialect::SQLite);
    }
    if (orderBy && !orderBy->empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < orderBy->size(); ++i) {
            if (i > 0) sql += ", ";
            sql += (*orderBy)[i].toSQL(SQLDialect::SQLite);
        }
    }
    sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    return executeRaw(sql);
}

std::string SqliteAdapter::serverVersion() {
    const auto r = executeRaw("SELECT sqlite_version()");
    if (r.rows.empty() || r.rows[0].empty()) return "SQLite";
    const auto v = r.rows[0][0].tryStringValue().value_or(std::string{});
    return "SQLite " + v;
}

std::optional<std::string> SqliteAdapter::currentDatabase() {
    if (filePath_.empty()) return std::nullopt;
    return filePath_;
}

SchemaSnapshot SqliteAdapter::fullSchemaSnapshot(const std::optional<std::string>& /*database*/) {
    SchemaSnapshot snap;
    snap.databaseName = lastBasename();
    snap.databaseType = DatabaseType::SQLite;
    snap.capturedAt = std::chrono::system_clock::now();

    const auto tables = listTables(std::nullopt);
    SchemaInfo main;
    main.name = "main";
    main.tables.reserve(tables.size());
    for (const auto& t : tables) {
        main.tables.push_back(describeTable(t.name, std::nullopt));
    }
    main.views = listViews(std::nullopt);
    snap.schemas.push_back(std::move(main));
    return snap;
}

std::vector<ColumnStatistics> SqliteAdapter::columnStatistics(const std::string& table,
                                                              const std::optional<std::string>& /*schema*/,
                                                              int /*sampleSize*/) {
    const auto cols = describeColumns(table);
    std::vector<ColumnStatistics> stats;
    stats.reserve(cols.size());
    for (const auto& c : cols) {
        const auto qc = quoteIdent(c.name);
        const auto sql =
            "SELECT COUNT(DISTINCT " + qc + "), "
            "CAST(SUM(CASE WHEN " + qc + " IS NULL THEN 1 ELSE 0 END) AS REAL) / NULLIF(COUNT(*), 0), "
            "MIN(" + qc + "), MAX(" + qc + ") FROM " + quoteIdent(table);
        const auto r = executeRaw(sql);
        if (r.rows.empty()) continue;
        const auto& row = r.rows[0];
        if (row.size() < 4) continue;
        ColumnStatistics cs;
        cs.columnName = c.name;
        if (auto n = row[0].tryIntValue()) cs.distinctCount = static_cast<int>(*n);
        if (auto d = row[1].tryDoubleValue()) cs.nullRatio = *d;
        cs.minValue = row[2].tryStringValue();
        cs.maxValue = row[3].tryStringValue();
        stats.push_back(std::move(cs));
    }
    return stats;
}

int SqliteAdapter::tableRowCount(const std::string& table, const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw("SELECT COUNT(*) FROM " + quoteIdent(table));
    if (r.rows.empty() || r.rows[0].empty()) return 0;
    return static_cast<int>(r.rows[0][0].tryIntValue().value_or(0));
}

std::optional<std::int64_t> SqliteAdapter::tableSizeBytes(const std::string& /*table*/,
                                                          const std::optional<std::string>& /*schema*/) {
    return std::nullopt;
}

std::vector<QueryStatisticsEntry> SqliteAdapter::queryStatistics() { return {}; }

std::vector<std::string> SqliteAdapter::primaryKeyColumns(const std::string& table,
                                                          const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw("PRAGMA table_info(" + quoteIdent(table) + ")");
    std::vector<std::string> cols;
    for (const auto& row : r.rows) {
        if (row.size() < 6) continue;
        const auto pk = row[5].tryIntValue().value_or(0);
        if (pk > 0) {
            if (auto n = row[1].tryStringValue()) cols.push_back(std::move(*n));
        }
    }
    return cols;
}

}
