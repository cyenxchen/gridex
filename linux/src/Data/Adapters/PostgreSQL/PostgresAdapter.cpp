#include "Data/Adapters/PostgreSQL/PostgresAdapter.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <libpq-fe.h>
#include <sstream>
#include <stdexcept>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

std::string pgQuote(std::string_view ident) {
    std::string out;
    out.reserve(ident.size() + 2);
    out.push_back('"');
    for (char c : ident) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

QueryType detectQueryType(const std::string& sql) {
    auto start = sql.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return QueryType::Other;
    std::string head;
    for (auto i = start; i < sql.size() && i - start < 10; ++i) {
        head.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i]))));
    }
    auto starts = [&](const char* p) { return head.rfind(p, 0) == 0; };
    if (starts("SELECT") || starts("WITH") || starts("EXPLAIN") || starts("SHOW")) return QueryType::Select;
    if (starts("INSERT")) return QueryType::Insert;
    if (starts("UPDATE")) return QueryType::Update;
    if (starts("DELETE")) return QueryType::Delete;
    if (starts("CREATE") || starts("ALTER") || starts("DROP") || starts("TRUNCATE")) return QueryType::DDL;
    return QueryType::Other;
}

std::string serializeForParam(const RowValue& v) {
    if (v.isString())  return v.asString();
    if (v.isInteger()) return std::to_string(v.asInteger());
    if (v.isDouble())  return std::to_string(v.asDouble());
    if (v.isBoolean()) return v.asBoolean() ? "t" : "f";
    if (v.isDate())    return formatTimestampUtc(v.asDate());
    if (v.isJson())    return v.asJson();
    if (v.isUuid())    return v.asUuid();
    return v.description();
}

std::string currentPgError(PGconn* c) {
    const char* m = c ? PQerrorMessage(c) : nullptr;
    if (!m || !*m) return "Unknown libpq error";
    // Strip trailing newline commonly present in PQerrorMessage
    std::string out(m);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

Bytes decodePgByteaHex(std::string_view s) {
    Bytes out;
    // libpq text format: bytea values are returned as \x<hex> or the escape form.
    if (s.size() >= 2 && s[0] == '\\' && (s[1] == 'x' || s[1] == 'X')) {
        const auto hex = s.substr(2);
        out.reserve(hex.size() / 2);
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            const int hi = hexVal(hex[i]);
            const int lo = hexVal(hex[i + 1]);
            if (hi < 0 || lo < 0) { out.clear(); return out; }
            out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        }
        return out;
    }
    // Escape format fallback: treat as raw bytes.
    out.assign(reinterpret_cast<const std::uint8_t*>(s.data()),
               reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
    return out;
}

RowValue readCell(PGresult* r, int row, int col) {
    if (PQgetisnull(r, row, col)) return RowValue::makeNull();
    const Oid oid = PQftype(r, col);
    const char* raw = PQgetvalue(r, row, col);
    const int len = PQgetlength(r, row, col);
    // Build a length-aware string so embedded NULs survive.
    std::string s(raw, static_cast<std::size_t>(len > 0 ? len : 0));
    switch (oid) {
        case 16:   // bool
            return RowValue::makeBoolean(s == "t" || s == "true" || s == "1");
        case 20:   // int8
        case 21:   // int2
        case 23:   // int4
            try { return RowValue::makeInteger(std::stoll(s)); }
            catch (...) { return RowValue::makeString(s); }
        case 700:  // float4
        case 701:  // float8
        case 1700: // numeric (lossy via stod — acceptable for Phase 2; Phase 4+ may add decimal type)
        case 790:  // money — stored as formatted string; try double
            try { return RowValue::makeDouble(std::stod(s)); }
            catch (...) { return RowValue::makeString(s); }
        case 17:   // bytea
            return RowValue::makeData(decodePgByteaHex(s));
        case 114:  // json
        case 3802: // jsonb
            return RowValue::makeJson(s);
        case 2950: // uuid
            return RowValue::makeUuid(s);
        case 1082: // date
        case 1114: // timestamp without tz
        case 1184: // timestamp with tz
            // Keep as string for now; the format differs per column and needs
            // dialect-aware parsing. Phase 4+ introduces proper date parsing.
            return RowValue::makeString(s);
        default:
            return RowValue::makeString(s);
    }
}

}

PostgresAdapter::PostgresAdapter() = default;

PostgresAdapter::~PostgresAdapter() {
    std::lock_guard lock(mutex_);
    if (conn_) { PQfinish(conn_); conn_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    databaseName_.reset();
}

void PostgresAdapter::connect(const ConnectionConfig& config, const std::optional<std::string>& password) {
    std::lock_guard lock(mutex_);
    if (conn_) { PQfinish(conn_); conn_ = nullptr; }

    const std::string host     = config.host.value_or("localhost");
    const int         port     = config.port.value_or(5432);
    const std::string user     = config.username.value_or("postgres");
    const std::string database = config.database.value_or(user);

    std::vector<const char*> keys{"host", "port", "user", "dbname", "application_name", "sslmode"};
    const std::string portStr = std::to_string(port);
    const std::string sslmode = config.sslEnabled ? "prefer" : "disable";
    std::vector<std::string> buf{host, portStr, user, database, "gridex", sslmode};
    if (password) {
        keys.push_back("password");
        buf.push_back(*password);
    }
    keys.push_back(nullptr);

    std::vector<const char*> vals;
    vals.reserve(buf.size() + 1);
    for (const auto& s : buf) vals.push_back(s.c_str());
    vals.push_back(nullptr);

    conn_ = PQconnectdbParams(keys.data(), vals.data(), /*expand_dbname=*/0);
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        std::string msg = currentPgError(conn_);
        if (conn_) { PQfinish(conn_); conn_ = nullptr; }
        throw ConnectionError("Postgres connect failed: " + msg);
    }

    connected_.store(true, std::memory_order_release);
    databaseName_ = database;
}

void PostgresAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    if (conn_) { PQfinish(conn_); conn_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    databaseName_.reset();  // H1: avoid stale identity after reconnect to a different DB
}

bool PostgresAdapter::testConnection(const ConnectionConfig& config, const std::optional<std::string>& password) {
    PostgresAdapter probe;
    probe.connect(config, password);
    probe.disconnect();
    return true;
}

void PostgresAdapter::ensureConnected() const {
    if (!connected_.load(std::memory_order_acquire) || !conn_)
        throw QueryError("Not connected to PostgreSQL");
}

QueryResult PostgresAdapter::executeInternal(const std::string& sql, const std::vector<RowValue>& values) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);

    // Pre-serialize param values to keep c_str() pointers alive across PQexecParams.
    std::vector<std::string> buf;
    buf.reserve(values.size());
    std::vector<const char*> ptrs;
    ptrs.reserve(values.size());
    for (const auto& v : values) {
        if (v.isNull()) { buf.emplace_back(); ptrs.push_back(nullptr); }
        else { buf.push_back(serializeForParam(v)); ptrs.push_back(buf.back().c_str()); }
    }

    const char* const* paramValues = values.empty() ? nullptr : ptrs.data();
    PGresult* res = PQexecParams(conn_, sql.c_str(),
                                  static_cast<int>(values.size()),
                                  /*paramTypes*/ nullptr,
                                  paramValues,
                                  /*paramLengths*/ nullptr,
                                  /*paramFormats*/ nullptr,
                                  /*resultFormat text*/ 0);

    // RAII: guarantee PQclear even if subsequent code throws.
    struct PgResultGuard {
        PGresult* p;
        ~PgResultGuard() { if (p) PQclear(p); }
    } guard{res};

    const auto status = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
    if (status == PGRES_BAD_RESPONSE || status == PGRES_FATAL_ERROR) {
        throw QueryError("Postgres query failed: " + currentPgError(conn_));
    }

    QueryResult result;
    result.queryType = detectQueryType(sql);

    if (status == PGRES_TUPLES_OK) {
        const int rows = PQntuples(res);
        const int cols = PQnfields(res);
        result.columns.reserve(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            ColumnHeader h;
            h.name = PQfname(res, c);
            h.dataType = std::to_string(PQftype(res, c));
            result.columns.push_back(std::move(h));
        }
        result.rows.reserve(static_cast<std::size_t>(rows));
        for (int r = 0; r < rows; ++r) {
            std::vector<RowValue> row;
            row.reserve(static_cast<std::size_t>(cols));
            for (int c = 0; c < cols; ++c) row.push_back(readCell(res, r, c));
            result.rows.push_back(std::move(row));
        }
        result.rowsAffected = rows;
    } else if (status == PGRES_COMMAND_OK) {
        const char* tag = PQcmdTuples(res);
        result.rowsAffected = tag && *tag ? std::atoi(tag) : 0;
    }

    result.executionTime = std::chrono::steady_clock::now() - start;
    return result;
}

QueryResult PostgresAdapter::execute(const std::string& query, const std::vector<QueryParameter>& parameters) {
    std::vector<RowValue> vals;
    vals.reserve(parameters.size());
    for (const auto& p : parameters) vals.push_back(p.value);
    return executeInternal(query, vals);
}

QueryResult PostgresAdapter::executeRaw(const std::string& sql) {
    return executeInternal(sql, {});
}

std::string PostgresAdapter::qualified(const std::string& table, const std::optional<std::string>& schema) const {
    if (schema) return pgQuote(*schema) + "." + pgQuote(table);
    return pgQuote(table);
}

// ---- Schema inspection ----

std::vector<std::string> PostgresAdapter::listDatabases() {
    const auto r = executeRaw("SELECT datname FROM pg_database WHERE datistemplate = false ORDER BY datname");
    std::vector<std::string> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    }
    return out;
}

std::vector<std::string> PostgresAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    const auto r = executeRaw(
        "SELECT schema_name FROM information_schema.schemata "
        "WHERE schema_name NOT IN ('pg_catalog','information_schema','pg_toast') "
        "AND schema_name NOT LIKE 'pg_temp_%' ORDER BY schema_name");
    std::vector<std::string> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::vector<TableInfo> PostgresAdapter::listTables(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("public");
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(s));
    const auto r = execute(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = $1 AND table_type = 'BASE TABLE' "
        "ORDER BY table_name", params);
    std::vector<TableInfo> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        TableInfo t;
        t.name = row[0].asString();
        t.schema = s;
        t.type = TableKind::Table;
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<ViewInfo> PostgresAdapter::listViews(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("public");
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(s));
    const auto r = execute(
        "SELECT table_name, view_definition FROM information_schema.views "
        "WHERE table_schema = $1 ORDER BY table_name", params);
    std::vector<ViewInfo> out;
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        ViewInfo v;
        v.name = row[0].asString();
        v.schema = s;
        if (row.size() > 1) v.definition = row[1].tryStringValue();
        out.push_back(std::move(v));
    }
    return out;
}

TableDescription PostgresAdapter::describeTable(const std::string& name, const std::optional<std::string>& schema) {
    TableDescription desc;
    desc.name = name;
    desc.schema = schema;

    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(name));
    const auto r = execute(
        "SELECT column_name, data_type, is_nullable, column_default, ordinal_position "
        "FROM information_schema.columns "
        "WHERE table_schema = $1 AND table_name = $2 "
        "ORDER BY ordinal_position", params);

    const auto pks = primaryKeyColumns(name, schema);
    for (const auto& row : r.rows) {
        if (row.size() < 5) continue;
        ColumnInfo c;
        c.name = row[0].tryStringValue().value_or("");
        c.dataType = row[1].tryStringValue().value_or("");
        c.isNullable = row[2].tryStringValue().value_or("YES") == "YES";
        if (!row[3].isNull()) c.defaultValue = row[3].tryStringValue();
        c.ordinalPosition = static_cast<int>(row[4].tryIntValue().value_or(0));
        for (const auto& pk : pks) if (pk == c.name) c.isPrimaryKey = true;
        desc.columns.push_back(std::move(c));
    }

    try { desc.foreignKeys = listForeignKeys(name, schema); } catch (...) {}
    try { desc.indexes = listIndexes(name, schema); } catch (...) {}
    try { desc.estimatedRowCount = tableRowCount(name, schema); } catch (...) {}
    return desc;
}

std::vector<IndexInfo> PostgresAdapter::listIndexes(const std::string& table, const std::optional<std::string>& schema) {
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT i.relname AS idx_name, ix.indisunique, "
        "pg_catalog.pg_get_indexdef(ix.indexrelid) "
        "FROM pg_index ix "
        "JOIN pg_class i  ON i.oid = ix.indexrelid "
        "JOIN pg_class t  ON t.oid = ix.indrelid "
        "JOIN pg_namespace n ON n.oid = t.relnamespace "
        "WHERE n.nspname = $1 AND t.relname = $2 ORDER BY i.relname", params);
    std::vector<IndexInfo> out;
    for (const auto& row : r.rows) {
        if (row.size() < 3) continue;
        IndexInfo idx;
        idx.name = row[0].tryStringValue().value_or("");
        idx.isUnique = row[1].tryStringValue().value_or("f") == "t";
        idx.tableName = table;
        out.push_back(std::move(idx));
    }
    return out;
}

std::vector<ForeignKeyInfo> PostgresAdapter::listForeignKeys(const std::string& table, const std::optional<std::string>& schema) {
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT kcu.constraint_name, kcu.column_name, ccu.table_name, ccu.column_name "
        "FROM information_schema.table_constraints tc "
        "JOIN information_schema.key_column_usage kcu ON tc.constraint_name = kcu.constraint_name "
        "JOIN information_schema.constraint_column_usage ccu ON tc.constraint_name = ccu.constraint_name "
        "WHERE tc.table_schema = $1 AND tc.table_name = $2 AND tc.constraint_type = 'FOREIGN KEY'", params);
    std::vector<ForeignKeyInfo> out;
    for (const auto& row : r.rows) {
        if (row.size() < 4) continue;
        ForeignKeyInfo fk;
        fk.name = row[0].tryStringValue();
        fk.columns.push_back(row[1].tryStringValue().value_or(""));
        fk.referencedTable = row[2].tryStringValue().value_or("");
        fk.referencedColumns.push_back(row[3].tryStringValue().value_or(""));
        out.push_back(std::move(fk));
    }
    return out;
}

std::vector<std::string> PostgresAdapter::listFunctions(const std::optional<std::string>& schema) {
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    const auto r = execute(
        "SELECT routine_name FROM information_schema.routines "
        "WHERE specific_schema = $1 AND routine_type = 'FUNCTION' "
        "ORDER BY routine_name", params);
    std::vector<std::string> out;
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::string PostgresAdapter::getFunctionSource(const std::string& name, const std::optional<std::string>& schema) {
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(name));
    const auto r = execute(
        "SELECT pg_get_functiondef(p.oid) "
        "FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace "
        "WHERE n.nspname = $1 AND p.proname = $2 LIMIT 1", params);
    if (r.rows.empty() || r.rows[0].empty()) return {};
    return r.rows[0][0].tryStringValue().value_or("");
}

// ---- DML ----

QueryResult PostgresAdapter::insertRow(const std::string& table, const std::optional<std::string>& schema,
                                       const std::unordered_map<std::string, RowValue>& values) {
    if (values.empty()) throw QueryError("insertRow: empty values");
    std::string cols, placeholders;
    std::vector<QueryParameter> params;
    int i = 0;
    for (const auto& [k, v] : values) {
        if (i > 0) { cols += ", "; placeholders += ", "; }
        cols += pgQuote(k);
        placeholders += "$" + std::to_string(++i);
        params.emplace_back(v);
    }
    return execute("INSERT INTO " + qualified(table, schema) + " (" + cols + ") VALUES (" + placeholders + ")", params);
}

QueryResult PostgresAdapter::updateRow(const std::string& table, const std::optional<std::string>& schema,
                                       const std::unordered_map<std::string, RowValue>& set,
                                       const std::unordered_map<std::string, RowValue>& where) {
    if (set.empty() || where.empty()) throw QueryError("updateRow: empty set/where");
    std::string setClause, whereClause;
    std::vector<QueryParameter> params;
    int i = 0;
    for (const auto& [k, v] : set) {
        if (i > 0) setClause += ", ";
        setClause += pgQuote(k) + " = $" + std::to_string(++i);
        params.emplace_back(v);
    }
    bool first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += pgQuote(k) + " = $" + std::to_string(++i);
        params.emplace_back(v);
        first = false;
    }
    return execute("UPDATE " + qualified(table, schema) + " SET " + setClause + " WHERE " + whereClause, params);
}

QueryResult PostgresAdapter::deleteRow(const std::string& table, const std::optional<std::string>& schema,
                                       const std::unordered_map<std::string, RowValue>& where) {
    if (where.empty()) throw QueryError("deleteRow: empty where");
    std::string whereClause;
    std::vector<QueryParameter> params;
    int i = 0;
    bool first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += pgQuote(k) + " = $" + std::to_string(++i);
        params.emplace_back(v);
        first = false;
    }
    return execute("DELETE FROM " + qualified(table, schema) + " WHERE " + whereClause, params);
}

void PostgresAdapter::beginTransaction()    { (void)executeRaw("BEGIN"); }
void PostgresAdapter::commitTransaction()   { (void)executeRaw("COMMIT"); }
void PostgresAdapter::rollbackTransaction() { (void)executeRaw("ROLLBACK"); }

QueryResult PostgresAdapter::fetchRows(const std::string& table,
                                       const std::optional<std::string>& schema,
                                       const std::optional<std::vector<std::string>>& columns,
                                       const std::optional<FilterExpression>& where,
                                       const std::optional<std::vector<QuerySortDescriptor>>& orderBy,
                                       int limit, int offset) {
    std::string colList = "*";
    if (columns && !columns->empty()) {
        colList.clear();
        for (std::size_t i = 0; i < columns->size(); ++i) {
            if (i > 0) colList += ", ";
            colList += pgQuote((*columns)[i]);
        }
    }
    std::string sql = "SELECT " + colList + " FROM " + qualified(table, schema);
    if (where && !where->conditions.empty()) sql += " WHERE " + where->toSQL(SQLDialect::PostgreSQL);
    if (orderBy && !orderBy->empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < orderBy->size(); ++i) {
            if (i > 0) sql += ", ";
            sql += (*orderBy)[i].toSQL(SQLDialect::PostgreSQL);
        }
    }
    sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    return executeRaw(sql);
}

std::string PostgresAdapter::serverVersion() {
    const auto r = executeRaw("SHOW server_version");
    if (r.rows.empty() || r.rows[0].empty()) return "PostgreSQL";
    return "PostgreSQL " + r.rows[0][0].tryStringValue().value_or("");
}

std::optional<std::string> PostgresAdapter::currentDatabase() {
    if (databaseName_) return databaseName_;
    const auto r = executeRaw("SELECT current_database()");
    if (r.rows.empty() || r.rows[0].empty()) return std::nullopt;
    return r.rows[0][0].tryStringValue();
}

// ---- ISchemaInspectable ----

SchemaSnapshot PostgresAdapter::fullSchemaSnapshot(const std::optional<std::string>& /*database*/) {
    SchemaSnapshot snap;
    snap.databaseName = currentDatabase().value_or("");
    snap.databaseType = DatabaseType::PostgreSQL;
    snap.capturedAt = std::chrono::system_clock::now();
    for (const auto& s : listSchemas(std::nullopt)) {
        SchemaInfo si;
        si.name = s;
        for (const auto& t : listTables(s)) {
            si.tables.push_back(describeTable(t.name, s));
        }
        si.views = listViews(s);
        snap.schemas.push_back(std::move(si));
    }
    return snap;
}

std::vector<ColumnStatistics> PostgresAdapter::columnStatistics(const std::string& /*table*/,
                                                                 const std::optional<std::string>& /*schema*/,
                                                                 int /*sampleSize*/) {
    return {}; // deferred to Phase 7
}

int PostgresAdapter::tableRowCount(const std::string& table, const std::optional<std::string>& schema) {
    const auto r = executeRaw("SELECT COUNT(*) FROM " + qualified(table, schema));
    if (r.rows.empty() || r.rows[0].empty()) return 0;
    return static_cast<int>(r.rows[0][0].tryIntValue().value_or(0));
}

std::optional<std::int64_t> PostgresAdapter::tableSizeBytes(const std::string& table, const std::optional<std::string>& schema) {
    // Use format('%I.%I', ...)::regclass so the server does identifier quoting.
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT pg_total_relation_size(format('%I.%I', $1::text, $2::text)::regclass)", params);
    if (r.rows.empty() || r.rows[0].empty()) return std::nullopt;
    return r.rows[0][0].tryIntValue();
}

std::vector<QueryStatisticsEntry> PostgresAdapter::queryStatistics() { return {}; }

std::vector<std::string> PostgresAdapter::primaryKeyColumns(const std::string& table, const std::optional<std::string>& schema) {
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(schema.value_or("public")));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT kcu.column_name "
        "FROM information_schema.table_constraints tc "
        "JOIN information_schema.key_column_usage kcu ON tc.constraint_name = kcu.constraint_name "
        "WHERE tc.table_schema = $1 AND tc.table_name = $2 AND tc.constraint_type = 'PRIMARY KEY' "
        "ORDER BY kcu.ordinal_position", params);
    std::vector<std::string> out;
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

}
