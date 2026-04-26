#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Core/Protocols/Database/ISchemaInspectable.h"

struct sqlite3;

namespace gridex {

// SQLite adapter using the sqlite3 C API.
// Thread-safe: opens with SQLITE_OPEN_FULLMUTEX so concurrent calls are serialized inside the lib.
// A separate std::mutex protects adapter-owned state (db pointer, connection flag).
class SqliteAdapter final : public IDatabaseAdapter, public ISchemaInspectable {
public:
    SqliteAdapter();
    ~SqliteAdapter() override;

    SqliteAdapter(const SqliteAdapter&) = delete;
    SqliteAdapter& operator=(const SqliteAdapter&) = delete;

    // IDatabaseAdapter
    [[nodiscard]] DatabaseType databaseType() const noexcept override { return DatabaseType::SQLite; }
    [[nodiscard]] bool isConnected() const noexcept override { return connected_; }

    void connect(const ConnectionConfig& config, const std::optional<std::string>& password) override;
    void disconnect() override;
    bool testConnection(const ConnectionConfig& config, const std::optional<std::string>& password) override;

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

    // ISchemaInspectable
    SchemaSnapshot fullSchemaSnapshot(const std::optional<std::string>& database) override;
    std::vector<ColumnStatistics> columnStatistics(const std::string& table,
                                                   const std::optional<std::string>& schema,
                                                   int sampleSize) override;
    int tableRowCount(const std::string& table,
                      const std::optional<std::string>& schema) override;
    std::optional<std::int64_t> tableSizeBytes(const std::string& table,
                                               const std::optional<std::string>& schema) override;
    std::vector<QueryStatisticsEntry> queryStatistics() override;
    std::vector<std::string> primaryKeyColumns(const std::string& table,
                                               const std::optional<std::string>& schema) override;

private:
    QueryResult executeInternal(const std::string& sql, const std::vector<RowValue>& values);
    std::vector<ColumnInfo> describeColumns(const std::string& table);
    void ensureConnected() const;
    std::string lastBasename() const;

    sqlite3* db_ = nullptr;
    std::string filePath_;              // empty when disconnected
    std::atomic<bool> connected_{false}; // race-free read in isConnected/ensureConnected
    mutable std::mutex mutex_;
};

}
