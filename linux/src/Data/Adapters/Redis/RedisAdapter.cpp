#include "Data/Adapters/Redis/RedisAdapter.h"

#include <chrono>
#include <hiredis/hiredis.h>
#include <sstream>
#include <stdexcept>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

// Split a command string on whitespace, respecting double-quoted tokens.
// e.g. "SET mykey \"hello world\"" → ["SET", "mykey", "hello world"]
std::vector<std::string> tokenize(const std::string& cmd) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = cmd.size();
    while (i < n) {
        // skip whitespace
        while (i < n && std::isspace(static_cast<unsigned char>(cmd[i]))) ++i;
        if (i >= n) break;
        if (cmd[i] == '"') {
            // quoted token
            ++i;
            std::string tok;
            while (i < n && cmd[i] != '"') {
                if (cmd[i] == '\\' && i + 1 < n) { tok.push_back(cmd[++i]); ++i; continue; }
                tok.push_back(cmd[i++]);
            }
            if (i < n) ++i; // consume closing "
            out.push_back(std::move(tok));
        } else {
            std::string tok;
            while (i < n && !std::isspace(static_cast<unsigned char>(cmd[i]))) tok.push_back(cmd[i++]);
            out.push_back(std::move(tok));
        }
    }
    return out;
}

// Classify the first token of a Redis command for QueryType mapping.
QueryType detectQueryType(const std::vector<std::string>& args) {
    if (args.empty()) return QueryType::Other;
    std::string verb;
    for (char c : args[0]) verb.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (verb == "GET"    || verb == "MGET"  || verb == "KEYS"   ||
        verb == "SCAN"   || verb == "HSCAN" || verb == "SSCAN"  ||
        verb == "ZSCAN"  || verb == "HGET"  || verb == "HGETALL" ||
        verb == "LRANGE" || verb == "SMEMBERS" || verb == "ZRANGE" ||
        verb == "TYPE"   || verb == "TTL"   || verb == "PTTL"   ||
        verb == "EXISTS" || verb == "INFO"  || verb == "DBSIZE" ||
        verb == "STRLEN" || verb == "LLEN"  || verb == "SCARD"  ||
        verb == "ZCARD"  || verb == "HLEN"  || verb == "GETRANGE")
        return QueryType::Select;
    if (verb == "SET"   || verb == "MSET"  || verb == "HSET"  ||
        verb == "LPUSH" || verb == "RPUSH" || verb == "SADD"  || verb == "ZADD")
        return QueryType::Insert;
    if (verb == "SETEX" || verb == "PSETEX" || verb == "GETSET" ||
        verb == "INCR"  || verb == "DECR"   || verb == "INCRBY" ||
        verb == "DECRBY"|| verb == "APPEND" || verb == "EXPIRE" ||
        verb == "RENAME"|| verb == "HMSET")
        return QueryType::Update;
    if (verb == "DEL" || verb == "UNLINK" || verb == "HDEL" ||
        verb == "LREM" || verb == "SREM"  || verb == "ZREM")
        return QueryType::Delete;
    return QueryType::Other;
}

// Convert a redisReply into a flat string for display.
std::string replyToString(redisReply* reply) {
    if (!reply) return "(nil)";
    switch (reply->type) {
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
            return std::string(reply->str, static_cast<std::size_t>(reply->len));
        case REDIS_REPLY_INTEGER:
            return std::to_string(reply->integer);
        case REDIS_REPLY_NIL:
            return "(nil)";
        case REDIS_REPLY_ERROR:
            return std::string("(error) ") + (reply->str ? reply->str : "");
        case REDIS_REPLY_ARRAY: {
            std::string out;
            for (std::size_t i = 0; i < static_cast<std::size_t>(reply->elements); ++i) {
                if (i > 0) out += '\n';
                out += std::to_string(i + 1) + ") " + replyToString(reply->element[i]);
            }
            return out;
        }
        default:
            return "(unknown)";
    }
}

// Build a QueryResult from a single-value reply (status/integer/string/nil).
QueryResult scalarResult(redisReply* reply, QueryType qt,
                         std::chrono::duration<double> elapsed) {
    QueryResult r;
    r.queryType = qt;
    r.executionTime = elapsed;

    ColumnHeader h;
    h.name = "result";
    h.dataType = "string";
    r.columns.push_back(std::move(h));

    std::vector<RowValue> row;
    if (reply->type == REDIS_REPLY_NIL) {
        row.push_back(RowValue::makeNull());
    } else if (reply->type == REDIS_REPLY_INTEGER) {
        row.push_back(RowValue::makeInteger(static_cast<std::int64_t>(reply->integer)));
    } else {
        row.push_back(RowValue::makeString(replyToString(reply)));
    }
    r.rows.push_back(std::move(row));
    r.rowsAffected = 1;
    return r;
}

} // anonymous namespace

// ---- Lifecycle ----

RedisAdapter::RedisAdapter() = default;

RedisAdapter::~RedisAdapter() {
    std::lock_guard lock(mutex_);
    if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
    connected_.store(false, std::memory_order_release);
}

void RedisAdapter::connect(const ConnectionConfig& config,
                           const std::optional<std::string>& password) {
    std::lock_guard lock(mutex_);
    if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }

    const std::string host = config.host.value_or("localhost");
    const int         port = config.port.value_or(6379);

    struct timeval tv{5, 0}; // 5-second connect timeout
    ctx_ = redisConnectWithTimeout(host.c_str(), port, tv);
    if (!ctx_ || ctx_->err) {
        std::string msg = ctx_ ? ctx_->errstr : "out of memory";
        if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
        throw ConnectionError("Redis connect failed: " + msg);
    }

    // Optional AUTH
    if (password && !password->empty()) {
        redisReply* r = static_cast<redisReply*>(
            redisCommand(ctx_, "AUTH %s", password->c_str()));
        if (!r || r->type == REDIS_REPLY_ERROR) {
            std::string err = r ? std::string(r->str, static_cast<std::size_t>(r->len))
                                : "no reply";
            if (r) freeReplyObject(r);
            redisFree(ctx_); ctx_ = nullptr;
            throw AuthenticationError("Redis AUTH failed: " + err);
        }
        freeReplyObject(r);
    }

    // SELECT database index if specified (default 0)
    int dbIndex = 0;
    if (config.database) {
        try { dbIndex = std::stoi(*config.database); } catch (...) { dbIndex = 0; }
    }
    if (dbIndex != 0) {
        redisReply* r = static_cast<redisReply*>(
            redisCommand(ctx_, "SELECT %d", dbIndex));
        if (!r || r->type == REDIS_REPLY_ERROR) {
            std::string err = r ? std::string(r->str, static_cast<std::size_t>(r->len))
                                : "no reply";
            if (r) freeReplyObject(r);
            redisFree(ctx_); ctx_ = nullptr;
            throw ConnectionError("Redis SELECT " + std::to_string(dbIndex) + " failed: " + err);
        }
        freeReplyObject(r);
        currentDb_ = dbIndex;
    }

    connected_.store(true, std::memory_order_release);
}

void RedisAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
    connected_.store(false, std::memory_order_release);
    currentDb_ = 0;
}

bool RedisAdapter::testConnection(const ConnectionConfig& config,
                                  const std::optional<std::string>& password) {
    RedisAdapter probe;
    probe.connect(config, password);
    probe.disconnect();
    return true;
}

void RedisAdapter::ensureConnected() const {
    if (!connected_.load(std::memory_order_acquire) || !ctx_)
        throw QueryError("Not connected to Redis");
}

// ---- Command execution ----

QueryResult RedisAdapter::runCommand(const std::vector<std::string>& args) {
    ensureConnected();
    if (args.empty()) throw QueryError("Empty Redis command");

    const auto start = std::chrono::steady_clock::now();
    const QueryType qt = detectQueryType(args);

    std::lock_guard lock(mutex_);

    // Build argv/argvlen arrays for redisCommandArgv
    std::vector<const char*> argv;
    std::vector<std::size_t> argvlen;
    argv.reserve(args.size());
    argvlen.reserve(args.size());
    for (const auto& a : args) {
        argv.push_back(a.c_str());
        argvlen.push_back(a.size());
    }

    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(ctx_, static_cast<int>(args.size()),
                         argv.data(), argvlen.data()));

    const auto elapsed = std::chrono::steady_clock::now() - start;

    if (!reply) {
        const std::string err = ctx_->err ? ctx_->errstr : "connection lost";
        throw QueryError("Redis command failed: " + err);
    }

    // RAII guard
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } guard{reply};

    if (reply->type == REDIS_REPLY_ERROR) {
        throw QueryError("Redis error: " + std::string(reply->str,
                         static_cast<std::size_t>(reply->len)));
    }

    QueryResult result;
    result.queryType = qt;
    result.executionTime = elapsed;

    if (reply->type == REDIS_REPLY_ARRAY) {
        // Multi-bulk reply: present as single column "value" with one row per element
        ColumnHeader h;
        h.name = "value";
        h.dataType = "string";
        result.columns.push_back(std::move(h));
        result.rows.reserve(static_cast<std::size_t>(reply->elements));
        for (std::size_t i = 0; i < static_cast<std::size_t>(reply->elements); ++i) {
            redisReply* elem = reply->element[i];
            std::vector<RowValue> row;
            if (!elem || elem->type == REDIS_REPLY_NIL) {
                row.push_back(RowValue::makeNull());
            } else if (elem->type == REDIS_REPLY_INTEGER) {
                row.push_back(RowValue::makeInteger(static_cast<std::int64_t>(elem->integer)));
            } else {
                row.push_back(RowValue::makeString(
                    std::string(elem->str, static_cast<std::size_t>(elem->len))));
            }
            result.rows.push_back(std::move(row));
        }
        result.rowsAffected = static_cast<int>(reply->elements);
    } else {
        // Scalar
        result = scalarResult(reply, qt, elapsed);
    }

    return result;
}

QueryResult RedisAdapter::execute(const std::string& query,
                                  const std::vector<QueryParameter>& /*parameters*/) {
    return executeRaw(query);
}

QueryResult RedisAdapter::executeRaw(const std::string& sql) {
    return runCommand(tokenize(sql));
}

// ---- Schema inspection ----

std::vector<std::string> RedisAdapter::listDatabases() {
    // Redis has 16 logical DBs (0-15) by default
    std::vector<std::string> dbs;
    dbs.reserve(16);
    for (int i = 0; i < 16; ++i) dbs.push_back(std::to_string(i));
    return dbs;
}

std::vector<std::string> RedisAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    return {"default"};
}

std::vector<TableInfo> RedisAdapter::listTables(const std::optional<std::string>& /*schema*/) {
    // Treat each key as a "table"
    auto r = runCommand({"KEYS", "*"});
    std::vector<TableInfo> out;
    out.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (row.empty()) continue;
        TableInfo t;
        t.name = row[0].tryStringValue().value_or("");
        t.schema = "default";
        t.type = TableKind::Table;
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<ViewInfo> RedisAdapter::listViews(const std::optional<std::string>& /*schema*/) {
    return {}; // Redis has no views
}

TableDescription RedisAdapter::describeTable(const std::string& name,
                                              const std::optional<std::string>& schema) {
    TableDescription desc;
    desc.name = name;
    desc.schema = schema;

    // Get key type
    auto typeReply = runCommand({"TYPE", name});
    std::string keyType = "none";
    if (!typeReply.rows.empty() && !typeReply.rows[0].empty())
        keyType = typeReply.rows[0][0].tryStringValue().value_or("none");

    // Get TTL
    auto ttlReply = runCommand({"TTL", name});
    std::int64_t ttlVal = -1;
    if (!ttlReply.rows.empty() && !ttlReply.rows[0].empty())
        ttlVal = ttlReply.rows[0][0].tryIntValue().value_or(-1);

    // Represent as two virtual columns: "value" (type=keyType) and "ttl"
    ColumnInfo cKey;
    cKey.name = "value";
    cKey.dataType = keyType;
    cKey.isNullable = true;
    cKey.ordinalPosition = 1;
    desc.columns.push_back(std::move(cKey));

    ColumnInfo cTtl;
    cTtl.name = "ttl";
    cTtl.dataType = "integer";
    cTtl.isNullable = false;
    cTtl.defaultValue = std::to_string(ttlVal);
    cTtl.ordinalPosition = 2;
    desc.columns.push_back(std::move(cTtl));

    return desc;
}

std::vector<IndexInfo> RedisAdapter::listIndexes(const std::string& /*table*/,
                                                  const std::optional<std::string>& /*schema*/) {
    return {}; // Redis has no indexes
}

std::vector<ForeignKeyInfo> RedisAdapter::listForeignKeys(const std::string& /*table*/,
                                                          const std::optional<std::string>& /*schema*/) {
    return {}; // Redis has no foreign keys
}

std::vector<std::string> RedisAdapter::listFunctions(const std::optional<std::string>& /*schema*/) {
    return {};
}

std::string RedisAdapter::getFunctionSource(const std::string& /*name*/,
                                            const std::optional<std::string>& /*schema*/) {
    return {};
}

// ---- DML (key-value mapping) ----

QueryResult RedisAdapter::insertRow(const std::string& table,
                                    const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& values) {
    // table = key name; values["value"] = value to SET
    auto it = values.find("value");
    if (it == values.end()) {
        // Fallback: use the first value, key = table name
        if (values.empty()) throw QueryError("insertRow: no values provided");
        const auto& [k, v] = *values.begin();
        return runCommand({"SET", table + ":" + k, v.description()});
    }
    return runCommand({"SET", table, it->second.description()});
}

QueryResult RedisAdapter::updateRow(const std::string& table,
                                    const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& set,
                                    const std::unordered_map<std::string, RowValue>& /*where*/) {
    // SET key value (overwrite)
    auto it = set.find("value");
    if (it == set.end()) {
        if (set.empty()) throw QueryError("updateRow: no set values provided");
        const auto& [k, v] = *set.begin();
        return runCommand({"SET", table + ":" + k, v.description()});
    }
    return runCommand({"SET", table, it->second.description()});
}

QueryResult RedisAdapter::deleteRow(const std::string& table,
                                    const std::optional<std::string>& /*schema*/,
                                    const std::unordered_map<std::string, RowValue>& where) {
    // where["key"] overrides; otherwise delete the key named by `table`
    auto it = where.find("key");
    const std::string key = (it != where.end())
        ? it->second.tryStringValue().value_or(table)
        : table;
    return runCommand({"DEL", key});
}

// ---- Transactions ----

void RedisAdapter::beginTransaction()    { (void)runCommand({"MULTI"}); }
void RedisAdapter::commitTransaction()   { (void)runCommand({"EXEC"}); }
void RedisAdapter::rollbackTransaction() { (void)runCommand({"DISCARD"}); }

// ---- Pagination (SCAN-based) ----

QueryResult RedisAdapter::fetchRows(const std::string& /*table*/,
                                    const std::optional<std::string>& /*schema*/,
                                    const std::optional<std::vector<std::string>>& /*columns*/,
                                    const std::optional<FilterExpression>& /*where*/,
                                    const std::optional<std::vector<QuerySortDescriptor>>& /*orderBy*/,
                                    int limit, int offset) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    // Column layout: key | type | value | ttl
    QueryResult result;
    result.queryType = QueryType::Select;
    for (const char* name : {"key", "type", "value", "ttl"}) {
        ColumnHeader h;
        h.name = name;
        h.dataType = "string";
        result.columns.push_back(std::move(h));
    }

    // SCAN to collect up to (offset + limit) keys, then skip `offset`
    std::string cursor = "0";
    int collected = 0;
    int target = offset + limit;
    std::vector<std::string> keys;
    keys.reserve(static_cast<std::size_t>(target));

    do {
        auto r = runCommand({"SCAN", cursor, "COUNT", "100"});
        // SCAN returns array[2]: [next_cursor, [key0, key1, ...]]
        if (r.rows.size() < 2) break;
        cursor = r.rows[0][0].tryStringValue().value_or("0");

        // The elements from index 1 onward are keys in a nested array format.
        // runCommand flattens the outer array; SCAN specifically returns
        // element[0]=cursor, element[1..N]=keys from the inner array.
        // We need to re-issue with ARGV so we get the raw structure.
        // Instead, re-run SCAN and parse the array reply manually.
        break; // sentinel — handled below via direct hiredis call
    } while (false);

    // Direct SCAN loop using hiredis to access nested reply structure
    {
        std::lock_guard lock(mutex_);
        cursor = "0";
        do {
            const std::string countStr = "100";
            const char* scanArgv[] = {"SCAN", cursor.c_str(), "COUNT", countStr.c_str()};
            const std::size_t scanArgvLen[] = {4, cursor.size(), 5, countStr.size()};
            redisReply* reply = static_cast<redisReply*>(
                redisCommandArgv(ctx_, 4, scanArgv, scanArgvLen));
            if (!reply) throw QueryError("Redis SCAN failed: connection lost");
            struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } guard{reply};

            if (reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) break;
            cursor = std::string(reply->element[0]->str,
                                 static_cast<std::size_t>(reply->element[0]->len));
            redisReply* keyArray = reply->element[1];
            if (keyArray->type == REDIS_REPLY_ARRAY) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(keyArray->elements); ++i) {
                    redisReply* ke = keyArray->element[i];
                    if (!ke || ke->type != REDIS_REPLY_STRING) continue;
                    keys.emplace_back(ke->str, static_cast<std::size_t>(ke->len));
                    ++collected;
                    if (collected >= target) { cursor = "0"; break; }
                }
            }
        } while (cursor != "0");
    }

    // Apply offset/limit
    int start_idx = offset;
    int end_idx = std::min(static_cast<int>(keys.size()), offset + limit);
    result.rows.reserve(static_cast<std::size_t>(std::max(0, end_idx - start_idx)));

    for (int i = start_idx; i < end_idx; ++i) {
        const std::string& key = keys[static_cast<std::size_t>(i)];

        // Get type
        std::string keyType = "string";
        {
            std::lock_guard lock(mutex_);
            redisReply* tr = static_cast<redisReply*>(
                redisCommand(ctx_, "TYPE %s", key.c_str()));
            if (tr) {
                if (tr->type == REDIS_REPLY_STATUS)
                    keyType = std::string(tr->str, static_cast<std::size_t>(tr->len));
                freeReplyObject(tr);
            }
        }

        // Get value (string type only; others show type name)
        std::string value;
        if (keyType == "string") {
            std::lock_guard lock(mutex_);
            redisReply* vr = static_cast<redisReply*>(
                redisCommand(ctx_, "GET %s", key.c_str()));
            if (vr) {
                if (vr->type == REDIS_REPLY_STRING)
                    value = std::string(vr->str, static_cast<std::size_t>(vr->len));
                freeReplyObject(vr);
            }
        } else {
            value = "(" + keyType + ")";
        }

        // Get TTL
        std::int64_t ttl = -1;
        {
            std::lock_guard lock(mutex_);
            redisReply* tr = static_cast<redisReply*>(
                redisCommand(ctx_, "TTL %s", key.c_str()));
            if (tr) {
                if (tr->type == REDIS_REPLY_INTEGER)
                    ttl = static_cast<std::int64_t>(tr->integer);
                freeReplyObject(tr);
            }
        }

        std::vector<RowValue> row;
        row.push_back(RowValue::makeString(key));
        row.push_back(RowValue::makeString(keyType));
        row.push_back(value.empty() ? RowValue::makeNull() : RowValue::makeString(value));
        row.push_back(RowValue::makeInteger(ttl));
        result.rows.push_back(std::move(row));
    }

    result.rowsAffected = static_cast<int>(result.rows.size());
    result.executionTime = std::chrono::steady_clock::now() - start;
    return result;
}

// ---- Server info ----

std::string RedisAdapter::serverVersion() {
    ensureConnected();
    std::lock_guard lock(mutex_);
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "INFO server"));
    if (!r) throw QueryError("Redis INFO failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } guard{r};

    std::string info(r->str, static_cast<std::size_t>(r->len));
    // Find "redis_version:X.Y.Z"
    const std::string marker = "redis_version:";
    auto pos = info.find(marker);
    if (pos == std::string::npos) return "Redis";
    pos += marker.size();
    auto end = info.find('\r', pos);
    if (end == std::string::npos) end = info.find('\n', pos);
    const std::string ver = end != std::string::npos
        ? info.substr(pos, end - pos)
        : info.substr(pos);
    return "Redis " + ver;
}

std::optional<std::string> RedisAdapter::currentDatabase() {
    return std::to_string(currentDb_);
}

// ---- Redis key-detail helpers ----

RedisAdapter::KeyDetail RedisAdapter::keyDetail(const std::string& key) {
    KeyDetail d;
    {
        std::lock_guard lock(mutex_);
        ensureConnected();
        redisReply* tr = static_cast<redisReply*>(redisCommand(ctx_, "TYPE %s", key.c_str()));
        if (tr) {
            d.type = (tr->type == REDIS_REPLY_STATUS)
                ? std::string(tr->str, static_cast<std::size_t>(tr->len)) : "none";
            freeReplyObject(tr);
        }
        redisReply* ttlr = static_cast<redisReply*>(redisCommand(ctx_, "TTL %s", key.c_str()));
        if (ttlr) {
            d.ttl = (ttlr->type == REDIS_REPLY_INTEGER)
                ? static_cast<std::int64_t>(ttlr->integer) : -1;
            freeReplyObject(ttlr);
        }
    }
    return d;
}

void RedisAdapter::expireKey(const std::string& key, int seconds) {
    runCommand({"EXPIRE", key, std::to_string(seconds)});
}

void RedisAdapter::persistKey(const std::string& key) {
    runCommand({"PERSIST", key});
}

void RedisAdapter::deleteKey(const std::string& key) {
    runCommand({"DEL", key});
}

std::string RedisAdapter::getString(const std::string& key) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "GET %s", key.c_str()));
    if (!r) throw QueryError("GET failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_NIL) return {};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    return std::string(r->str, static_cast<std::size_t>(r->len));
}

void RedisAdapter::setString(const std::string& key, const std::string& value) {
    runCommand({"SET", key, value});
}

std::vector<std::string> RedisAdapter::lrange(const std::string& key) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "LRANGE %s 0 -1", key.c_str()));
    if (!r) throw QueryError("LRANGE failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    std::vector<std::string> out;
    if (r->type == REDIS_REPLY_ARRAY) {
        out.reserve(static_cast<std::size_t>(r->elements));
        for (std::size_t i = 0; i < static_cast<std::size_t>(r->elements); ++i) {
            redisReply* e = r->element[i];
            out.push_back(e && e->type == REDIS_REPLY_STRING
                ? std::string(e->str, static_cast<std::size_t>(e->len)) : "");
        }
    }
    return out;
}

void RedisAdapter::lpush(const std::string& key, const std::string& value) {
    runCommand({"LPUSH", key, value});
}

void RedisAdapter::rpush(const std::string& key, const std::string& value) {
    runCommand({"RPUSH", key, value});
}

void RedisAdapter::lpop(const std::string& key) { runCommand({"LPOP", key}); }
void RedisAdapter::rpop(const std::string& key) { runCommand({"RPOP", key}); }

void RedisAdapter::lrem(const std::string& key, const std::string& value) {
    runCommand({"LREM", key, "0", value});
}

std::vector<std::pair<std::string,std::string>> RedisAdapter::hgetall(const std::string& key) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "HGETALL %s", key.c_str()));
    if (!r) throw QueryError("HGETALL failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    std::vector<std::pair<std::string,std::string>> out;
    if (r->type == REDIS_REPLY_ARRAY && r->elements % 2 == 0) {
        out.reserve(static_cast<std::size_t>(r->elements / 2));
        for (std::size_t i = 0; i + 1 < static_cast<std::size_t>(r->elements); i += 2) {
            auto* fk = r->element[i];
            auto* fv = r->element[i+1];
            std::string field = fk && fk->type == REDIS_REPLY_STRING
                ? std::string(fk->str, static_cast<std::size_t>(fk->len)) : "";
            std::string val = fv && fv->type == REDIS_REPLY_STRING
                ? std::string(fv->str, static_cast<std::size_t>(fv->len)) : "";
            out.push_back({field, val});
        }
    }
    return out;
}

void RedisAdapter::hset(const std::string& key, const std::string& field, const std::string& value) {
    runCommand({"HSET", key, field, value});
}

void RedisAdapter::hdel(const std::string& key, const std::string& field) {
    runCommand({"HDEL", key, field});
}

std::vector<std::string> RedisAdapter::smembers(const std::string& key) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "SMEMBERS %s", key.c_str()));
    if (!r) throw QueryError("SMEMBERS failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    std::vector<std::string> out;
    if (r->type == REDIS_REPLY_ARRAY) {
        out.reserve(static_cast<std::size_t>(r->elements));
        for (std::size_t i = 0; i < static_cast<std::size_t>(r->elements); ++i) {
            redisReply* e = r->element[i];
            out.push_back(e && e->type == REDIS_REPLY_STRING
                ? std::string(e->str, static_cast<std::size_t>(e->len)) : "");
        }
    }
    return out;
}

void RedisAdapter::sadd(const std::string& key, const std::string& member) {
    runCommand({"SADD", key, member});
}

void RedisAdapter::srem(const std::string& key, const std::string& member) {
    runCommand({"SREM", key, member});
}

std::vector<std::pair<std::string,double>> RedisAdapter::zrangeWithScores(const std::string& key) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "ZRANGE %s 0 -1 WITHSCORES", key.c_str()));
    if (!r) throw QueryError("ZRANGE failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    std::vector<std::pair<std::string,double>> out;
    if (r->type == REDIS_REPLY_ARRAY && r->elements % 2 == 0) {
        out.reserve(static_cast<std::size_t>(r->elements / 2));
        for (std::size_t i = 0; i + 1 < static_cast<std::size_t>(r->elements); i += 2) {
            auto* me = r->element[i];
            auto* se = r->element[i+1];
            std::string member = me && me->type == REDIS_REPLY_STRING
                ? std::string(me->str, static_cast<std::size_t>(me->len)) : "";
            double score = 0.0;
            if (se && se->type == REDIS_REPLY_STRING)
                score = std::stod(std::string(se->str, static_cast<std::size_t>(se->len)));
            out.push_back({member, score});
        }
    }
    return out;
}

void RedisAdapter::zadd(const std::string& key, double score, const std::string& member) {
    runCommand({"ZADD", key, std::to_string(score), member});
}

void RedisAdapter::zrem(const std::string& key, const std::string& member) {
    runCommand({"ZREM", key, member});
}

std::vector<RedisAdapter::StreamEntry> RedisAdapter::xrevrange(const std::string& key, int count) {
    std::lock_guard lock(mutex_);
    ensureConnected();
    const std::string countStr = std::to_string(count);
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "XREVRANGE %s + - COUNT %s", key.c_str(), countStr.c_str()));
    if (!r) throw QueryError("XREVRANGE failed");
    struct Guard { redisReply* p; ~Guard() { if (p) freeReplyObject(p); } } g{r};
    if (r->type == REDIS_REPLY_ERROR)
        throw QueryError(std::string(r->str, static_cast<std::size_t>(r->len)));
    std::vector<StreamEntry> out;
    if (r->type != REDIS_REPLY_ARRAY) return out;
    out.reserve(static_cast<std::size_t>(r->elements));
    for (std::size_t i = 0; i < static_cast<std::size_t>(r->elements); ++i) {
        redisReply* entry = r->element[i];
        if (!entry || entry->type != REDIS_REPLY_ARRAY || entry->elements < 2) continue;
        StreamEntry se;
        auto* idElem = entry->element[0];
        se.id = idElem && idElem->type == REDIS_REPLY_STRING
            ? std::string(idElem->str, static_cast<std::size_t>(idElem->len)) : "";
        auto* fields = entry->element[1];
        if (fields && fields->type == REDIS_REPLY_ARRAY && fields->elements % 2 == 0) {
            for (std::size_t j = 0; j + 1 < static_cast<std::size_t>(fields->elements); j += 2) {
                auto* fk = fields->element[j];
                auto* fv = fields->element[j+1];
                std::string fkStr = fk && fk->type == REDIS_REPLY_STRING
                    ? std::string(fk->str, static_cast<std::size_t>(fk->len)) : "";
                std::string fvStr = fv && fv->type == REDIS_REPLY_STRING
                    ? std::string(fv->str, static_cast<std::size_t>(fv->len)) : "";
                se.fields.push_back({fkStr, fvStr});
            }
        }
        out.push_back(std::move(se));
    }
    return out;
}

} // namespace gridex
