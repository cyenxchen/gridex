#include "Data/Adapters/MySQL/MysqlAdapter.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <mysql.h>
#include <sstream>
#include <stdexcept>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

// libmariadb / libmysqlclient requires mysql_library_init once before any thread touches it.
void ensureLibraryInit() {
    static std::once_flag once;
    std::call_once(once, []{ mysql_library_init(0, nullptr, nullptr); });
}

std::string myQuote(std::string_view ident) {
    std::string out;
    out.reserve(ident.size() + 2);
    out.push_back('`');
    for (char c : ident) {
        if (c == '`') out.push_back('`');
        out.push_back(c);
    }
    out.push_back('`');
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
    if (starts("SELECT") || starts("SHOW") || starts("EXPLAIN") || starts("WITH") || starts("DESCRIBE")) return QueryType::Select;
    if (starts("INSERT")) return QueryType::Insert;
    if (starts("UPDATE")) return QueryType::Update;
    if (starts("DELETE")) return QueryType::Delete;
    if (starts("CREATE") || starts("ALTER") || starts("DROP") || starts("TRUNCATE")) return QueryType::DDL;
    return QueryType::Other;
}

std::string escapeAsSqlLiteral(MYSQL* c, const RowValue& v) {
    assert(c != nullptr && "escapeAsSqlLiteral requires a live MYSQL*");
    if (v.isNull())    return "NULL";
    if (v.isInteger()) return std::to_string(v.asInteger());
    if (v.isDouble())  return std::to_string(v.asDouble());
    if (v.isBoolean()) return v.asBoolean() ? "1" : "0";

    std::string src;
    if      (v.isString()) src = v.asString();
    else if (v.isDate())   src = formatTimestampUtc(v.asDate());
    else if (v.isJson())   src = v.asJson();
    else if (v.isUuid())   src = v.asUuid();
    else                   src = v.description();

    std::string buf;
    buf.resize(src.size() * 2 + 1);
    const unsigned long n = mysql_real_escape_string(c, buf.data(), src.c_str(),
                                                     static_cast<unsigned long>(src.size()));
    buf.resize(n);
    return "'" + buf + "'";
}

RowValue readCell(const char* raw, MYSQL_FIELD* field, unsigned long length) {
    if (!raw) return RowValue::makeNull();
    const std::string s(raw, length);
    switch (field->type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
            try { return RowValue::makeInteger(std::stoll(s)); } catch (...) { return RowValue::makeString(s); }
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            try { return RowValue::makeDouble(std::stod(s)); } catch (...) { return RowValue::makeString(s); }
        case MYSQL_TYPE_JSON:
            return RowValue::makeJson(s);
        default:
            return RowValue::makeString(s);
    }
}

std::string currentMyError(MYSQL* c) {
    if (!c) return "Unknown MySQL error";
    const char* m = mysql_error(c);
    return m && *m ? std::string(m) : std::string("Unknown MySQL error");
}

}

MysqlAdapter::MysqlAdapter() {
    ensureLibraryInit();
    conn_ = mysql_init(nullptr);
    if (!conn_) throw ConnectionError("mysql_init failed");
}

MysqlAdapter::~MysqlAdapter() {
    std::lock_guard lock(mutex_);
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    databaseName_.reset();
}

void MysqlAdapter::connect(const ConnectionConfig& config, const std::optional<std::string>& password) {
    std::lock_guard lock(mutex_);
    ensureLibraryInit();
    if (!conn_) conn_ = mysql_init(nullptr);
    if (!conn_) throw ConnectionError("mysql_init failed");

    const std::string host     = config.host.value_or("localhost");
    const unsigned    port     = static_cast<unsigned>(config.port.value_or(3306));
    const std::string user     = config.username.value_or("root");
    const std::string database = config.database.value_or("");
    const char* pw             = password ? password->c_str() : nullptr;
    const char* dbName         = database.empty() ? nullptr : database.c_str();

    // CLIENT_LONG_FLAG + CLIENT_REMEMBER_OPTIONS improve compatibility; CLIENT_INTERACTIVE
    // bumps server idle timeout. CLIENT_MULTI_STATEMENTS stays off to keep the surface safe.
    const unsigned long flags = CLIENT_LONG_FLAG | CLIENT_REMEMBER_OPTIONS | CLIENT_INTERACTIVE;
    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pw, dbName, port, nullptr, flags)) {
        std::string msg = currentMyError(conn_);
        mysql_close(conn_);
        conn_ = mysql_init(nullptr);
        throw ConnectionError("MySQL connect failed: " + msg);
    }
    if (mysql_set_character_set(conn_, "utf8mb4") != 0) {
        // Not fatal: some older servers may lack utf8mb4. Keep connection but warn via stderr
        // so the user can see the fallback; writes of 4-byte UTF-8 may fail.
        std::fprintf(stderr, "[MysqlAdapter] utf8mb4 not available, falling back (%s)\n",
                     currentMyError(conn_).c_str());
    }
    connected_.store(true, std::memory_order_release);
    if (!database.empty()) databaseName_ = database;
}

void MysqlAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    databaseName_.reset();  // H1
}

bool MysqlAdapter::testConnection(const ConnectionConfig& config, const std::optional<std::string>& password) {
    try {
        MysqlAdapter probe;
        probe.connect(config, password);
        probe.disconnect();
        return true;
    } catch (const GridexError&) {
        throw;  // re-throw so caller sees typed error
    } catch (const std::exception& e) {
        throw ConnectionError(std::string("MySQL test: ") + e.what());
    }
}

void MysqlAdapter::ensureConnected() const {
    if (!connected_.load(std::memory_order_acquire) || !conn_)
        throw QueryError("Not connected to MySQL");
}

QueryResult MysqlAdapter::executeInternal(std::string sql, const std::vector<RowValue>& values) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);

    // Client-side parameter substitution for `?` placeholders.
    if (!values.empty()) {
        std::string out;
        out.reserve(sql.size());
        std::size_t i = 0;
        std::size_t pi = 0;
        while (i < sql.size()) {
            const char c = sql[i];
            if (c == '\'' || c == '"' || c == '`') {
                out.push_back(c);
                ++i;
                while (i < sql.size() && sql[i] != c) {
                    out.push_back(sql[i]);
                    if (sql[i] == '\\' && i + 1 < sql.size()) { out.push_back(sql[++i]); }
                    ++i;
                }
                if (i < sql.size()) { out.push_back(sql[i]); ++i; }
                continue;
            }
            if (c == '?' && pi < values.size()) {
                out += escapeAsSqlLiteral(conn_, values[pi++]);
                ++i;
                continue;
            }
            out.push_back(c);
            ++i;
        }
        sql = std::move(out);
    }

    if (mysql_real_query(conn_, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
        throw QueryError("MySQL query failed: " + currentMyError(conn_));
    }

    QueryResult result;
    result.queryType = detectQueryType(sql);

    MYSQL_RES* res = mysql_store_result(conn_);
    if (res) {
        const unsigned fieldCount = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);
        result.columns.reserve(fieldCount);
        for (unsigned i = 0; i < fieldCount; ++i) {
            ColumnHeader h;
            h.name = fields[i].name ? fields[i].name : "";
            h.dataType = std::to_string(static_cast<int>(fields[i].type));
            h.isNullable = !(fields[i].flags & NOT_NULL_FLAG);
            if (fields[i].table) h.tableName = std::string(fields[i].table);
            result.columns.push_back(std::move(h));
        }
        while (true) {
            MYSQL_ROW r = mysql_fetch_row(res);
            if (!r) break;
            unsigned long* lens = mysql_fetch_lengths(res);
            std::vector<RowValue> row;
            row.reserve(fieldCount);
            for (unsigned c = 0; c < fieldCount; ++c) {
                if (!r[c]) { row.push_back(RowValue::makeNull()); continue; }
                row.push_back(readCell(r[c], &fields[c], lens ? lens[c] : 0));
            }
            result.rows.push_back(std::move(row));
        }
        mysql_free_result(res);
        result.rowsAffected = static_cast<int>(result.rows.size());
    } else if (mysql_field_count(conn_) == 0) {
        result.rowsAffected = static_cast<int>(mysql_affected_rows(conn_));
    } else {
        throw QueryError("MySQL mysql_store_result failed: " + currentMyError(conn_));
    }

    result.executionTime = std::chrono::steady_clock::now() - start;
    return result;
}

QueryResult MysqlAdapter::execute(const std::string& query, const std::vector<QueryParameter>& parameters) {
    std::vector<RowValue> vals;
    vals.reserve(parameters.size());
    for (const auto& p : parameters) vals.push_back(p.value);
    return executeInternal(query, vals);
}

QueryResult MysqlAdapter::executeRaw(const std::string& sql) { return executeInternal(sql, {}); }

// ---- Schema ----

std::vector<std::string> MysqlAdapter::listDatabases() {
    const auto r = executeRaw("SHOW DATABASES");
    std::vector<std::string> out;
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::vector<std::string> MysqlAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    // MySQL doesn't have schemas distinct from databases; return [currentDatabase].
    const auto cur = currentDatabase();
    if (cur) return {*cur};
    return {};
}

std::vector<TableInfo> MysqlAdapter::listTables(const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    if (dbName.empty()) return {};
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    const auto r = execute(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = ? AND table_type = 'BASE TABLE' ORDER BY table_name", params);
    std::vector<TableInfo> out;
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        TableInfo t;
        t.name = row[0].asString();
        t.schema = dbName;
        t.type = TableKind::Table;
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<ViewInfo> MysqlAdapter::listViews(const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    if (dbName.empty()) return {};
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    const auto r = execute(
        "SELECT table_name, view_definition FROM information_schema.views "
        "WHERE table_schema = ? ORDER BY table_name", params);
    std::vector<ViewInfo> out;
    for (const auto& row : r.rows) {
        if (row.empty() || !row[0].isString()) continue;
        ViewInfo v;
        v.name = row[0].asString();
        v.schema = dbName;
        if (row.size() > 1) v.definition = row[1].tryStringValue();
        out.push_back(std::move(v));
    }
    return out;
}

TableDescription MysqlAdapter::describeTable(const std::string& name, const std::optional<std::string>& schema) {
    TableDescription desc;
    desc.name = name;
    desc.schema = schema;
    const auto dbName = schema.value_or(currentDatabase().value_or(""));

    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(name));
    const auto r = execute(
        "SELECT column_name, column_type, is_nullable, column_default, ordinal_position, column_key, extra "
        "FROM information_schema.columns WHERE table_schema = ? AND table_name = ? ORDER BY ordinal_position",
        params);
    for (const auto& row : r.rows) {
        if (row.size() < 7) continue;
        ColumnInfo c;
        c.name = row[0].tryStringValue().value_or("");
        c.dataType = row[1].tryStringValue().value_or("");
        c.isNullable = row[2].tryStringValue().value_or("YES") == "YES";
        if (!row[3].isNull()) c.defaultValue = row[3].tryStringValue();
        c.ordinalPosition = static_cast<int>(row[4].tryIntValue().value_or(0));
        c.isPrimaryKey = row[5].tryStringValue().value_or("") == "PRI";
        c.isAutoIncrement = row[6].tryStringValue().value_or("").find("auto_increment") != std::string::npos;
        desc.columns.push_back(std::move(c));
    }

    try { desc.foreignKeys = listForeignKeys(name, schema); } catch (...) {}
    try { desc.estimatedRowCount = tableRowCount(name, schema); } catch (...) {}
    return desc;
}

std::vector<IndexInfo> MysqlAdapter::listIndexes(const std::string& table, const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT index_name, non_unique, GROUP_CONCAT(column_name ORDER BY seq_in_index) "
        "FROM information_schema.statistics "
        "WHERE table_schema = ? AND table_name = ? "
        "GROUP BY index_name, non_unique ORDER BY index_name", params);
    std::vector<IndexInfo> out;
    for (const auto& row : r.rows) {
        if (row.size() < 3) continue;
        IndexInfo idx;
        idx.name = row[0].tryStringValue().value_or("");
        idx.isUnique = row[1].tryStringValue().value_or("1") == "0";
        const auto colsJoined = row[2].tryStringValue().value_or("");
        std::string cur;
        for (char c : colsJoined) {
            if (c == ',') { if (!cur.empty()) { idx.columns.push_back(std::move(cur)); cur.clear(); } }
            else cur.push_back(c);
        }
        if (!cur.empty()) idx.columns.push_back(std::move(cur));
        idx.tableName = table;
        out.push_back(std::move(idx));
    }
    return out;
}

std::vector<ForeignKeyInfo> MysqlAdapter::listForeignKeys(const std::string& table, const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT constraint_name, column_name, referenced_table_name, referenced_column_name "
        "FROM information_schema.key_column_usage "
        "WHERE table_schema = ? AND table_name = ? AND referenced_table_name IS NOT NULL", params);
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

std::vector<std::string> MysqlAdapter::listFunctions(const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    const auto r = execute(
        "SELECT routine_name FROM information_schema.routines "
        "WHERE routine_schema = ? AND routine_type = 'FUNCTION' ORDER BY routine_name", params);
    std::vector<std::string> out;
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::string MysqlAdapter::getFunctionSource(const std::string& name, const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(name));
    const auto r = execute(
        "SELECT routine_definition FROM information_schema.routines "
        "WHERE routine_schema = ? AND routine_name = ? LIMIT 1", params);
    if (r.rows.empty() || r.rows[0].empty()) return {};
    return r.rows[0][0].tryStringValue().value_or("");
}

// ---- DML ----

QueryResult MysqlAdapter::insertRow(const std::string& table, const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& values) {
    if (values.empty()) throw QueryError("insertRow: empty values");
    std::string cols, placeholders;
    std::vector<QueryParameter> params;
    bool first = true;
    for (const auto& [k, v] : values) {
        if (!first) { cols += ", "; placeholders += ", "; }
        cols += myQuote(k);
        placeholders += "?";
        params.emplace_back(v);
        first = false;
    }
    return execute("INSERT INTO " + myQuote(table) + " (" + cols + ") VALUES (" + placeholders + ")", params);
}

QueryResult MysqlAdapter::updateRow(const std::string& table, const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& set,
                                    const std::unordered_map<std::string, RowValue>& where) {
    if (set.empty() || where.empty()) throw QueryError("updateRow: empty set/where");
    std::string setClause, whereClause;
    std::vector<QueryParameter> params;
    bool first = true;
    for (const auto& [k, v] : set) {
        if (!first) setClause += ", ";
        setClause += myQuote(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += myQuote(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    return execute("UPDATE " + myQuote(table) + " SET " + setClause + " WHERE " + whereClause, params);
}

QueryResult MysqlAdapter::deleteRow(const std::string& table, const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& where) {
    if (where.empty()) throw QueryError("deleteRow: empty where");
    std::string whereClause;
    std::vector<QueryParameter> params;
    bool first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += myQuote(k) + " = ?";
        params.emplace_back(v);
        first = false;
    }
    return execute("DELETE FROM " + myQuote(table) + " WHERE " + whereClause, params);
}

void MysqlAdapter::beginTransaction()    { (void)executeRaw("START TRANSACTION"); }
void MysqlAdapter::commitTransaction()   { (void)executeRaw("COMMIT"); }
void MysqlAdapter::rollbackTransaction() { (void)executeRaw("ROLLBACK"); }

QueryResult MysqlAdapter::fetchRows(const std::string& table,
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
            colList += myQuote((*columns)[i]);
        }
    }
    std::string sql = "SELECT " + colList + " FROM " + myQuote(table);
    if (where && !where->conditions.empty()) sql += " WHERE " + where->toSQL(SQLDialect::MySQL);
    if (orderBy && !orderBy->empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < orderBy->size(); ++i) {
            if (i > 0) sql += ", ";
            sql += (*orderBy)[i].toSQL(SQLDialect::MySQL);
        }
    }
    sql += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    return executeRaw(sql);
}

std::string MysqlAdapter::serverVersion() {
    const auto r = executeRaw("SELECT VERSION()");
    if (r.rows.empty() || r.rows[0].empty()) return "MySQL";
    return "MySQL " + r.rows[0][0].tryStringValue().value_or("");
}

std::optional<std::string> MysqlAdapter::currentDatabase() {
    if (databaseName_) return databaseName_;
    const auto r = executeRaw("SELECT DATABASE()");
    if (r.rows.empty() || r.rows[0].empty() || r.rows[0][0].isNull()) return std::nullopt;
    return r.rows[0][0].tryStringValue();
}

// ---- ISchemaInspectable ----

SchemaSnapshot MysqlAdapter::fullSchemaSnapshot(const std::optional<std::string>& /*database*/) {
    SchemaSnapshot snap;
    snap.databaseName = currentDatabase().value_or("");
    snap.databaseType = DatabaseType::MySQL;
    snap.capturedAt = std::chrono::system_clock::now();
    SchemaInfo si;
    si.name = snap.databaseName;
    for (const auto& t : listTables(snap.databaseName)) {
        si.tables.push_back(describeTable(t.name, snap.databaseName));
    }
    si.views = listViews(snap.databaseName);
    snap.schemas.push_back(std::move(si));
    return snap;
}

std::vector<ColumnStatistics> MysqlAdapter::columnStatistics(const std::string&,
                                                             const std::optional<std::string>&,
                                                             int) { return {}; }

int MysqlAdapter::tableRowCount(const std::string& table, const std::optional<std::string>& /*schema*/) {
    const auto r = executeRaw("SELECT COUNT(*) FROM " + myQuote(table));
    if (r.rows.empty() || r.rows[0].empty()) return 0;
    return static_cast<int>(r.rows[0][0].tryIntValue().value_or(0));
}

std::optional<std::int64_t> MysqlAdapter::tableSizeBytes(const std::string& table, const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT data_length + index_length FROM information_schema.tables "
        "WHERE table_schema = ? AND table_name = ? LIMIT 1", params);
    if (r.rows.empty() || r.rows[0].empty()) return std::nullopt;
    return r.rows[0][0].tryIntValue();
}

std::vector<QueryStatisticsEntry> MysqlAdapter::queryStatistics() { return {}; }

std::vector<std::string> MysqlAdapter::primaryKeyColumns(const std::string& table, const std::optional<std::string>& schema) {
    const auto dbName = schema.value_or(currentDatabase().value_or(""));
    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString(dbName));
    params.emplace_back(RowValue::makeString(table));
    const auto r = execute(
        "SELECT column_name FROM information_schema.key_column_usage "
        "WHERE table_schema = ? AND table_name = ? AND constraint_name = 'PRIMARY' "
        "ORDER BY ordinal_position", params);
    std::vector<std::string> out;
    for (const auto& row : r.rows) if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

}
