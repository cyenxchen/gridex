#include "Data/Adapters/MSSQL/MssqlAdapter.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>
#include <sybdb.h>

#include "Core/Errors/GridexError.h"

// Convenience: the header stores dbproc_ as void* to avoid leaking sybdb.h
// into translation units that include the header. Cast once here.
#define DBPROC() (static_cast<DBPROCESS*>(dbproc_))

namespace gridex {

namespace {

// dbinit() must be called exactly once before any db-lib usage.
void ensureLibraryInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (dbinit() == FAIL)
            throw InternalError("FreeTDS dbinit() failed");
        // Suppress default error/message handlers (they print to stderr).
        dberrhandle([](DBPROCESS*, int, int, int, char*, char*) -> int {
            return INT_CANCEL;
        });
        dbmsghandle([](DBPROCESS*, DBINT, int, int, char*, char*, char*, int) -> int {
            return 0;
        });
    });
}

// Quote a T-SQL identifier with square brackets.
std::string msQuote(std::string_view ident) {
    std::string out;
    out.reserve(ident.size() + 2);
    out.push_back('[');
    for (char c : ident) {
        if (c == ']') out.push_back(']'); // escape ]
        out.push_back(c);
    }
    out.push_back(']');
    return out;
}

// Quote a string literal — double any single quotes.
std::string msLiteral(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

QueryType detectQueryType(const std::string& sql) {
    auto start = sql.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return QueryType::Other;
    std::string head;
    for (auto i = start; i < sql.size() && i - start < 10; ++i)
        head.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i]))));
    auto starts = [&](const char* p) { return head.rfind(p, 0) == 0; };
    if (starts("SELECT") || starts("WITH") || starts("EXPLAIN") || starts("SHOW")) return QueryType::Select;
    if (starts("INSERT")) return QueryType::Insert;
    if (starts("UPDATE")) return QueryType::Update;
    if (starts("DELETE")) return QueryType::Delete;
    if (starts("CREATE") || starts("ALTER") || starts("DROP") || starts("TRUNCATE")) return QueryType::DDL;
    return QueryType::Other;
}

// Convert a RowValue to a T-SQL literal string for inline parameter expansion.
std::string rowValueToLiteral(const RowValue& v) {
    if (v.isNull())    return "NULL";
    if (v.isInteger()) return std::to_string(v.asInteger());
    if (v.isDouble())  return std::to_string(v.asDouble());
    if (v.isBoolean()) return v.asBoolean() ? "1" : "0";
    if (v.isString())  return msLiteral(v.asString());
    if (v.isDate())    return "'" + formatTimestampUtc(v.asDate()) + "'";
    if (v.isJson())    return msLiteral(v.asJson());
    if (v.isUuid())    return "'" + v.asUuid() + "'";
    return "NULL";
}

// Read a single cell from the current row in dbproc.
RowValue readCell(DBPROCESS* dbproc, int col) {
    const BYTE* data = dbdata(dbproc, col);
    const DBINT len  = dbdatlen(dbproc, col);
    if (!data || len <= 0) return RowValue::makeNull();

    const int colType = dbcoltype(dbproc, col);
    switch (colType) {
        case SYBBIT:
        case SYBBITN: {
            const std::uint8_t b = *reinterpret_cast<const std::uint8_t*>(data);
            return RowValue::makeBoolean(b != 0);
        }
        case SYBINT1: {
            const std::uint8_t v = *reinterpret_cast<const std::uint8_t*>(data);
            return RowValue::makeInteger(static_cast<std::int64_t>(v));
        }
        case SYBINT2: {
            std::int16_t v{};
            std::memcpy(&v, data, sizeof(v));
            return RowValue::makeInteger(static_cast<std::int64_t>(v));
        }
        case SYBINT4: {
            std::int32_t v{};
            std::memcpy(&v, data, sizeof(v));
            return RowValue::makeInteger(static_cast<std::int64_t>(v));
        }
        case SYBINT8: {
            std::int64_t v{};
            std::memcpy(&v, data, sizeof(v));
            return RowValue::makeInteger(v);
        }
        case SYBINTN: {
            switch (len) {
                case 1: { std::uint8_t  v{}; std::memcpy(&v, data, 1); return RowValue::makeInteger(v); }
                case 2: { std::int16_t  v{}; std::memcpy(&v, data, 2); return RowValue::makeInteger(v); }
                case 4: { std::int32_t  v{}; std::memcpy(&v, data, 4); return RowValue::makeInteger(v); }
                case 8: { std::int64_t  v{}; std::memcpy(&v, data, 8); return RowValue::makeInteger(v); }
                default: break;
            }
            break;
        }
        case SYBFLT8: {
            double v{};
            std::memcpy(&v, data, sizeof(v));
            return RowValue::makeDouble(v);
        }
        case SYBFLTN: {
            if (len == 4) { float  v{}; std::memcpy(&v, data, 4); return RowValue::makeDouble(static_cast<double>(v)); }
            if (len == 8) { double v{}; std::memcpy(&v, data, 8); return RowValue::makeDouble(v); }
            break;
        }
        default:
            break;
    }
    // Default: treat as string (char/nchar/varchar/nvarchar/text/datetime/numeric/money/…)
    return RowValue::makeString(std::string(reinterpret_cast<const char*>(data),
                                            static_cast<std::size_t>(len)));
}

} // anonymous namespace

// ---- Lifecycle ----

MssqlAdapter::MssqlAdapter() {
    ensureLibraryInit();
}

MssqlAdapter::~MssqlAdapter() {
    std::lock_guard lock(mutex_);
    if (dbproc_) { dbclose(DBPROC()); dbproc_ = nullptr; }
    connected_.store(false, std::memory_order_release);
}

void MssqlAdapter::connect(const ConnectionConfig& config,
                           const std::optional<std::string>& password) {
    std::lock_guard lock(mutex_);
    if (dbproc_) { dbclose(DBPROC()); dbproc_ = nullptr; }

    const std::string host     = config.host.value_or("localhost");
    const int         port     = config.port.value_or(1433);
    const std::string user     = config.username.value_or("sa");
    const std::string database = config.database.value_or("master");
    const std::string pwd      = password.value_or("");

    // FreeTDS accepts "host:port" as the server string.
    const std::string server = host + ":" + std::to_string(port);

    LOGINREC* login = dblogin();
    if (!login) throw ConnectionError("MSSQL: dblogin() returned null");

    DBSETLUSER(login, user.c_str());
    DBSETLPWD(login, pwd.c_str());
    DBSETLAPP(login, "gridex");
    DBSETLCHARSET(login, "UTF-8");
    DBSETLDBNAME(login, database.c_str());

    DBPROCESS* proc = dbopen(login, server.c_str());
    dbloginfree(login);

    if (!proc) {  // dbopen returns NULL on failure (FAIL == 0)
        throw ConnectionError("MSSQL: dbopen failed — server=" + server +
                              " user=" + user);
    }

    dbproc_ = static_cast<void*>(proc);
    connected_.store(true, std::memory_order_release);
    databaseName_ = database;
}

void MssqlAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    if (dbproc_) { dbclose(DBPROC()); dbproc_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    databaseName_.reset();
}

bool MssqlAdapter::testConnection(const ConnectionConfig& config,
                                  const std::optional<std::string>& password) {
    MssqlAdapter probe;
    probe.connect(config, password);
    probe.disconnect();
    return true;
}

void MssqlAdapter::ensureConnected() const {
    if (!connected_.load(std::memory_order_acquire) || !dbproc_)
        throw QueryError("Not connected to MSSQL");
}

// ---- Core query execution ----

QueryResult MssqlAdapter::executeInternal(const std::string& sql) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);

    // Clear any pending results from a previous query.
    dbcancel(DBPROC());

    if (dbcmd(DBPROC(), sql.c_str()) == FAIL)
        throw QueryError("MSSQL: dbcmd failed");

    if (dbsqlexec(DBPROC()) == FAIL)
        throw QueryError("MSSQL: dbsqlexec failed");

    QueryResult result;
    result.queryType = detectQueryType(sql);

    // Consume result sets — collect only the first SELECT result set.
    bool gotColumns = false;
    RETCODE rc = dbresults(DBPROC());
    while (rc != NO_MORE_RESULTS && rc != FAIL) {
        const int ncols = dbnumcols(DBPROC());
        if (ncols > 0 && !gotColumns) {
            // Build column headers
            result.columns.reserve(static_cast<std::size_t>(ncols));
            for (int c = 1; c <= ncols; ++c) {
                ColumnHeader h;
                const char* name = dbcolname(DBPROC(), c);
                h.name = name ? name : ("col" + std::to_string(c));
                h.dataType = std::to_string(dbcoltype(DBPROC(), c));
                result.columns.push_back(std::move(h));
            }
            gotColumns = true;

            // Fetch rows
            STATUS rowStatus;
            while ((rowStatus = dbnextrow(DBPROC())) != NO_MORE_ROWS) {
                if (rowStatus == FAIL) {
                    throw QueryError("MSSQL: dbnextrow failed");
                }
                if (rowStatus != REG_ROW) continue; // skip compute rows
                std::vector<RowValue> row;
                row.reserve(static_cast<std::size_t>(ncols));
                for (int c = 1; c <= ncols; ++c)
                    row.push_back(readCell(DBPROC(), c));
                result.rows.push_back(std::move(row));
            }
            result.rowsAffected = static_cast<int>(result.rows.size());
        } else {
            // Drain remaining result sets (e.g. row-count from INSERT/UPDATE/DELETE)
            STATUS rowStatus;
            while ((rowStatus = dbnextrow(DBPROC())) != NO_MORE_ROWS) {
                if (rowStatus == FAIL) break;
            }
            // DBCOUNT gives rows affected for DML
            const DBINT affected = dbcount(DBPROC());
            if (affected >= 0 && result.rowsAffected == 0)
                result.rowsAffected = static_cast<int>(affected);
        }
        rc = dbresults(DBPROC());
    }

    if (rc == FAIL)
        throw QueryError("MSSQL: dbresults failed");

    result.executionTime = std::chrono::steady_clock::now() - start;
    return result;
}

QueryResult MssqlAdapter::execute(const std::string& query,
                                  const std::vector<QueryParameter>& parameters) {
    // db-lib has no native parameterized queries — inline values.
    if (parameters.empty()) return executeInternal(query);

    std::string sql;
    sql.reserve(query.size() + parameters.size() * 8);
    std::size_t i = 0;
    std::size_t paramIdx = 0;
    const std::size_t n = query.size();
    while (i < n) {
        // Respect string literals
        if (query[i] == '\'' ) {
            sql.push_back(query[i++]);
            while (i < n) {
                if (query[i] == '\'' && i + 1 < n && query[i+1] == '\'') {
                    sql.push_back('\''); sql.push_back('\''); i += 2; continue;
                }
                if (query[i] == '\'') { sql.push_back(query[i++]); break; }
                sql.push_back(query[i++]);
            }
            continue;
        }
        // ? placeholder
        if (query[i] == '?' && paramIdx < parameters.size()) {
            sql += rowValueToLiteral(parameters[paramIdx++].value);
            ++i; continue;
        }
        // @N placeholder (MSSQL style)
        if (query[i] == '@' && i + 1 < n && std::isdigit(static_cast<unsigned char>(query[i+1]))) {
            std::size_t j = i + 1;
            std::size_t idx = 0;
            while (j < n && std::isdigit(static_cast<unsigned char>(query[j])))
                idx = idx * 10 + static_cast<std::size_t>(query[j++] - '0');
            if (idx >= 1 && idx <= parameters.size()) {
                sql += rowValueToLiteral(parameters[idx - 1].value);
                i = j; continue;
            }
        }
        sql.push_back(query[i++]);
    }
    return executeInternal(sql);
}

QueryResult MssqlAdapter::executeRaw(const std::string& sql) {
    return executeInternal(sql);
}

std::string MssqlAdapter::qualified(const std::string& table,
                                     const std::optional<std::string>& schema) const {
    if (schema) return msQuote(*schema) + "." + msQuote(table);
    return msQuote(table);
}

// ---- Schema inspection ----

std::vector<std::string> MssqlAdapter::listDatabases() {
    const auto r = executeInternal(
        "SELECT name FROM sys.databases WHERE state_desc = 'ONLINE' ORDER BY name");
    std::vector<std::string> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::vector<std::string> MssqlAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    const auto r = executeInternal(
        "SELECT schema_name FROM information_schema.schemata ORDER BY schema_name");
    std::vector<std::string> out;
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::vector<TableInfo> MssqlAdapter::listTables(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = " + msLiteral(s) +
        " AND table_type = 'BASE TABLE' ORDER BY table_name");
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

std::vector<ViewInfo> MssqlAdapter::listViews(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT table_name, view_definition FROM information_schema.views "
        "WHERE table_schema = " + msLiteral(s) + " ORDER BY table_name");
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

TableDescription MssqlAdapter::describeTable(const std::string& name,
                                              const std::optional<std::string>& schema) {
    TableDescription desc;
    desc.name = name;
    desc.schema = schema;

    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT column_name, data_type, is_nullable, column_default, ordinal_position "
        "FROM information_schema.columns "
        "WHERE table_schema = " + msLiteral(s) +
        " AND table_name = " + msLiteral(name) +
        " ORDER BY ordinal_position");

    const auto pks = primaryKeyColumns(name, schema);
    for (const auto& row : r.rows) {
        if (row.size() < 5) continue;
        ColumnInfo c;
        c.name     = row[0].tryStringValue().value_or("");
        c.dataType = row[1].tryStringValue().value_or("");
        c.isNullable = row[2].tryStringValue().value_or("YES") == "YES";
        if (!row[3].isNull()) c.defaultValue = row[3].tryStringValue();
        c.ordinalPosition = static_cast<int>(row[4].tryIntValue().value_or(0));
        for (const auto& pk : pks) if (pk == c.name) c.isPrimaryKey = true;
        desc.columns.push_back(std::move(c));
    }

    try { desc.foreignKeys = listForeignKeys(name, schema); } catch (...) {}
    try { desc.indexes     = listIndexes(name, schema); }     catch (...) {}
    try { desc.estimatedRowCount = tableRowCount(name, schema); } catch (...) {}
    return desc;
}

std::vector<IndexInfo> MssqlAdapter::listIndexes(const std::string& table,
                                                  const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT i.name, i.is_unique "
        "FROM sys.indexes i "
        "JOIN sys.objects o ON o.object_id = i.object_id "
        "JOIN sys.schemas sc ON sc.schema_id = o.schema_id "
        "WHERE o.name = " + msLiteral(table) +
        " AND sc.name = " + msLiteral(s) +
        " AND i.name IS NOT NULL ORDER BY i.name");
    std::vector<IndexInfo> out;
    for (const auto& row : r.rows) {
        if (row.size() < 2) continue;
        IndexInfo idx;
        idx.name = row[0].tryStringValue().value_or("");
        // is_unique is a bit column — may come as integer or "1"/"0"
        idx.isUnique = row[1].tryIntValue().value_or(0) != 0 ||
                       row[1].tryStringValue().value_or("") == "1";
        idx.tableName = table;
        out.push_back(std::move(idx));
    }
    return out;
}

std::vector<ForeignKeyInfo> MssqlAdapter::listForeignKeys(const std::string& table,
                                                           const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT fk.name, kcu.column_name, ccu.table_name, ccu.column_name "
        "FROM information_schema.table_constraints fk "
        "JOIN information_schema.key_column_usage kcu "
        "  ON fk.constraint_name = kcu.constraint_name "
        "JOIN information_schema.constraint_column_usage ccu "
        "  ON fk.constraint_name = ccu.constraint_name "
        "WHERE fk.table_schema = " + msLiteral(s) +
        " AND fk.table_name = " + msLiteral(table) +
        " AND fk.constraint_type = 'FOREIGN KEY'");
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

std::vector<std::string> MssqlAdapter::listFunctions(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT routine_name FROM information_schema.routines "
        "WHERE specific_schema = " + msLiteral(s) +
        " AND routine_type = 'FUNCTION' ORDER BY routine_name");
    std::vector<std::string> out;
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::string MssqlAdapter::getFunctionSource(const std::string& name,
                                             const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT OBJECT_DEFINITION(OBJECT_ID(" + msLiteral(s + "." + name) + "))");
    if (r.rows.empty() || r.rows[0].empty()) return {};
    return r.rows[0][0].tryStringValue().value_or("");
}

// ---- Stored procedures ----

std::vector<std::string> MssqlAdapter::listProcedures(const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT p.name FROM sys.procedures p "
        "JOIN sys.schemas sc ON sc.schema_id = p.schema_id "
        "WHERE sc.name = " + msLiteral(s) + " ORDER BY p.name");
    std::vector<std::string> out;
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

std::string MssqlAdapter::getProcedureSource(const std::string& name,
                                              const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT OBJECT_DEFINITION(OBJECT_ID(" + msLiteral(s + "." + name) + "))");
    if (r.rows.empty() || r.rows[0].empty()) return {};
    return r.rows[0][0].tryStringValue().value_or("");
}

std::vector<std::string> MssqlAdapter::listProcedureParameters(
    const std::string& name, const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT p.name + ' ' + t.name "
        "FROM sys.parameters p "
        "JOIN sys.objects o ON o.object_id = p.object_id "
        "JOIN sys.schemas sc ON sc.schema_id = o.schema_id "
        "JOIN sys.types t ON t.user_type_id = p.user_type_id "
        "WHERE o.name = " + msLiteral(name) +
        " AND sc.name = " + msLiteral(s) +
        " ORDER BY p.parameter_id");
    std::vector<std::string> out;
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

// ---- DML ----

QueryResult MssqlAdapter::insertRow(const std::string& table,
                                     const std::optional<std::string>& schema,
                                     const std::unordered_map<std::string, RowValue>& values) {
    if (values.empty()) throw QueryError("insertRow: empty values");
    std::string cols, vals;
    int i = 0;
    for (const auto& [k, v] : values) {
        if (i > 0) { cols += ", "; vals += ", "; }
        cols += msQuote(k);
        vals += rowValueToLiteral(v);
        ++i;
    }
    return executeInternal("INSERT INTO " + qualified(table, schema) +
                            " (" + cols + ") VALUES (" + vals + ")");
}

QueryResult MssqlAdapter::updateRow(const std::string& table,
                                     const std::optional<std::string>& schema,
                                     const std::unordered_map<std::string, RowValue>& set,
                                     const std::unordered_map<std::string, RowValue>& where) {
    if (set.empty() || where.empty()) throw QueryError("updateRow: empty set/where");
    std::string setClause, whereClause;
    {
        int i = 0;
        for (const auto& [k, v] : set) {
            if (i > 0) setClause += ", ";
            setClause += msQuote(k) + " = " + rowValueToLiteral(v);
            ++i;
        }
    }
    {
        bool first = true;
        for (const auto& [k, v] : where) {
            if (!first) whereClause += " AND ";
            whereClause += msQuote(k) + " = " + rowValueToLiteral(v);
            first = false;
        }
    }
    return executeInternal("UPDATE " + qualified(table, schema) +
                            " SET " + setClause + " WHERE " + whereClause);
}

QueryResult MssqlAdapter::deleteRow(const std::string& table,
                                     const std::optional<std::string>& schema,
                                     const std::unordered_map<std::string, RowValue>& where) {
    if (where.empty()) throw QueryError("deleteRow: empty where");
    std::string whereClause;
    bool first = true;
    for (const auto& [k, v] : where) {
        if (!first) whereClause += " AND ";
        whereClause += msQuote(k) + " = " + rowValueToLiteral(v);
        first = false;
    }
    return executeInternal("DELETE FROM " + qualified(table, schema) +
                            " WHERE " + whereClause);
}

// ---- Transactions ----

void MssqlAdapter::beginTransaction()    { (void)executeInternal("BEGIN TRANSACTION"); }
void MssqlAdapter::commitTransaction()   { (void)executeInternal("COMMIT TRANSACTION"); }
void MssqlAdapter::rollbackTransaction() { (void)executeInternal("ROLLBACK TRANSACTION"); }

// ---- Pagination ----

QueryResult MssqlAdapter::fetchRows(const std::string& table,
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
            colList += msQuote((*columns)[i]);
        }
    }

    // MSSQL 2012+ OFFSET/FETCH syntax requires an ORDER BY clause.
    std::string orderClause;
    if (orderBy && !orderBy->empty()) {
        for (std::size_t i = 0; i < orderBy->size(); ++i) {
            if (i > 0) orderClause += ", ";
            orderClause += (*orderBy)[i].toSQL(SQLDialect::MSSQL);
        }
    } else {
        // Provide a deterministic default so OFFSET/FETCH is valid.
        orderClause = "(SELECT NULL)";
    }

    std::string sql = "SELECT " + colList + " FROM " + qualified(table, schema);
    if (where && !where->conditions.empty())
        sql += " WHERE " + where->toSQL(SQLDialect::MSSQL);
    sql += " ORDER BY " + orderClause;
    sql += " OFFSET " + std::to_string(offset) + " ROWS";
    sql += " FETCH NEXT " + std::to_string(limit) + " ROWS ONLY";
    return executeInternal(sql);
}

// ---- Server info ----

std::string MssqlAdapter::serverVersion() {
    const auto r = executeInternal("SELECT @@VERSION");
    if (r.rows.empty() || r.rows[0].empty()) return "MSSQL";
    return r.rows[0][0].tryStringValue().value_or("MSSQL");
}

std::optional<std::string> MssqlAdapter::currentDatabase() {
    if (databaseName_) return databaseName_;
    const auto r = executeInternal("SELECT DB_NAME()");
    if (r.rows.empty() || r.rows[0].empty()) return std::nullopt;
    return r.rows[0][0].tryStringValue();
}

// ---- ISchemaInspectable ----

SchemaSnapshot MssqlAdapter::fullSchemaSnapshot(const std::optional<std::string>& /*database*/) {
    SchemaSnapshot snap;
    snap.databaseName = currentDatabase().value_or("");
    snap.databaseType = DatabaseType::MSSQL;
    snap.capturedAt = std::chrono::system_clock::now();
    for (const auto& s : listSchemas(std::nullopt)) {
        SchemaInfo si;
        si.name = s;
        for (const auto& t : listTables(s))
            si.tables.push_back(describeTable(t.name, s));
        si.views = listViews(s);
        snap.schemas.push_back(std::move(si));
    }
    return snap;
}

std::vector<ColumnStatistics> MssqlAdapter::columnStatistics(const std::string& /*table*/,
                                                              const std::optional<std::string>& /*schema*/,
                                                              int /*sampleSize*/) {
    return {}; // deferred to Phase 7
}

int MssqlAdapter::tableRowCount(const std::string& table,
                                 const std::optional<std::string>& schema) {
    const auto r = executeInternal("SELECT COUNT(*) FROM " + qualified(table, schema));
    if (r.rows.empty() || r.rows[0].empty()) return 0;
    return static_cast<int>(r.rows[0][0].tryIntValue().value_or(0));
}

std::optional<std::int64_t> MssqlAdapter::tableSizeBytes(const std::string& table,
                                                          const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT SUM(a.total_pages) * 8192 "
        "FROM sys.tables t "
        "JOIN sys.schemas sc ON sc.schema_id = t.schema_id "
        "JOIN sys.indexes i ON i.object_id = t.object_id "
        "JOIN sys.partitions p ON p.object_id = t.object_id AND p.index_id = i.index_id "
        "JOIN sys.allocation_units a ON a.container_id = p.partition_id "
        "WHERE t.name = " + msLiteral(table) +
        " AND sc.name = " + msLiteral(s));
    if (r.rows.empty() || r.rows[0].empty() || r.rows[0][0].isNull()) return std::nullopt;
    return r.rows[0][0].tryIntValue();
}

std::vector<QueryStatisticsEntry> MssqlAdapter::queryStatistics() { return {}; }

std::vector<std::string> MssqlAdapter::primaryKeyColumns(const std::string& table,
                                                          const std::optional<std::string>& schema) {
    const std::string s = schema.value_or("dbo");
    const auto r = executeInternal(
        "SELECT kcu.column_name "
        "FROM information_schema.table_constraints tc "
        "JOIN information_schema.key_column_usage kcu "
        "  ON tc.constraint_name = kcu.constraint_name "
        "WHERE tc.table_schema = " + msLiteral(s) +
        " AND tc.table_name = " + msLiteral(table) +
        " AND tc.constraint_type = 'PRIMARY KEY' "
        "ORDER BY kcu.ordinal_position");
    std::vector<std::string> out;
    for (const auto& row : r.rows)
        if (!row.empty() && row[0].isString()) out.push_back(row[0].asString());
    return out;
}

} // namespace gridex
