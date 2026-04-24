#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "Core/Protocols/Database/IDatabaseAdapter.h"

// Opaque hiredis context; include <hiredis/hiredis.h> in the .cpp only.
struct redisContext;

namespace gridex {

// Redis adapter using hiredis sync API.
// Since Redis is a key-value store, IDatabaseAdapter methods are mapped as:
//   listDatabases() → ["0".."15"]
//   listTables()    → KEYS * results (key names treated as "tables")
//   listSchemas()   → ["default"]
//   describeTable() → TYPE + TTL for the given key
//   execute()/executeRaw() → raw Redis command string (space-separated tokens)
//   fetchRows()     → SCAN cursor-based iteration, returns key+type+value rows
//   insertRow()     → SET key value
//   deleteRow()     → DEL key
//   transactions    → MULTI / EXEC / DISCARD
class RedisAdapter final : public IDatabaseAdapter {
public:
    RedisAdapter();
    ~RedisAdapter() override;

    RedisAdapter(const RedisAdapter&) = delete;
    RedisAdapter& operator=(const RedisAdapter&) = delete;

    [[nodiscard]] DatabaseType databaseType() const noexcept override { return DatabaseType::Redis; }
    [[nodiscard]] bool isConnected() const noexcept override { return connected_.load(std::memory_order_acquire); }

    void connect(const ConnectionConfig& config, const std::optional<std::string>& password) override;
    void disconnect() override;
    bool testConnection(const ConnectionConfig& config, const std::optional<std::string>& password) override;

    // Raw Redis command: treat `query`/`sql` as a space-separated Redis command.
    QueryResult execute(const std::string& query, const std::vector<QueryParameter>& parameters) override;
    QueryResult executeRaw(const std::string& sql) override;

    std::vector<std::string> listDatabases() override;
    std::vector<std::string> listSchemas(const std::optional<std::string>& database) override;
    std::vector<TableInfo>  listTables(const std::optional<std::string>& schema) override;
    std::vector<ViewInfo>   listViews(const std::optional<std::string>& schema) override;
    TableDescription        describeTable(const std::string& name,
                                          const std::optional<std::string>& schema) override;
    std::vector<IndexInfo>      listIndexes(const std::string& table,
                                            const std::optional<std::string>& schema) override;
    std::vector<ForeignKeyInfo> listForeignKeys(const std::string& table,
                                                const std::optional<std::string>& schema) override;
    std::vector<std::string>    listFunctions(const std::optional<std::string>& schema) override;
    std::string getFunctionSource(const std::string& name,
                                  const std::optional<std::string>& schema) override;

    QueryResult insertRow(const std::string& table, const std::optional<std::string>& schema,
                          const std::unordered_map<std::string, RowValue>& values) override;
    QueryResult updateRow(const std::string& table, const std::optional<std::string>& schema,
                          const std::unordered_map<std::string, RowValue>& set,
                          const std::unordered_map<std::string, RowValue>& where) override;
    QueryResult deleteRow(const std::string& table, const std::optional<std::string>& schema,
                          const std::unordered_map<std::string, RowValue>& where) override;

    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;

    QueryResult fetchRows(const std::string& table,
                          const std::optional<std::string>& schema,
                          const std::optional<std::vector<std::string>>& columns,
                          const std::optional<FilterExpression>& where,
                          const std::optional<std::vector<QuerySortDescriptor>>& orderBy,
                          int limit, int offset) override;

    std::string serverVersion() override;
    std::optional<std::string> currentDatabase() override;

    // Redis key-detail helpers (used by RedisKeyDetailView)
    struct KeyDetail {
        std::string type;   // string|list|hash|set|zset|stream
        std::int64_t ttl;   // -1=persist, -2=missing
    };
    KeyDetail keyDetail(const std::string& key);
    void expireKey(const std::string& key, int seconds);
    void persistKey(const std::string& key);
    void deleteKey(const std::string& key);

    // String
    std::string getString(const std::string& key);
    void setString(const std::string& key, const std::string& value);

    // List
    std::vector<std::string> lrange(const std::string& key);
    void lpush(const std::string& key, const std::string& value);
    void rpush(const std::string& key, const std::string& value);
    void lpop(const std::string& key);
    void rpop(const std::string& key);
    void lrem(const std::string& key, const std::string& value);

    // Hash
    std::vector<std::pair<std::string,std::string>> hgetall(const std::string& key);
    void hset(const std::string& key, const std::string& field, const std::string& value);
    void hdel(const std::string& key, const std::string& field);

    // Set
    std::vector<std::string> smembers(const std::string& key);
    void sadd(const std::string& key, const std::string& member);
    void srem(const std::string& key, const std::string& member);

    // ZSet
    std::vector<std::pair<std::string,double>> zrangeWithScores(const std::string& key);
    void zadd(const std::string& key, double score, const std::string& member);
    void zrem(const std::string& key, const std::string& member);

    // Stream (read-only)
    struct StreamEntry { std::string id; std::vector<std::pair<std::string,std::string>> fields; };
    std::vector<StreamEntry> xrevrange(const std::string& key, int count = 50);

private:
    // Execute a pre-tokenized Redis command via the hiredis API.
    QueryResult runCommand(const std::vector<std::string>& args);
    void ensureConnected() const;

    redisContext* ctx_ = nullptr;
    int currentDb_{0};
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
};

}
