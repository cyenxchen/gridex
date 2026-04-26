#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Core/Protocols/Database/ISchemaInspectable.h"

// Opaque libpq connection type. Include <libpq-fe.h> in the .cpp only.
struct pg_conn;
typedef struct pg_conn PGconn;

namespace gridex {

// PostgreSQL adapter using libpq (sync API).
// Phase 2d scope: connection lifecycle, parameterized queries, transactions,
// basic schema inspection (databases/schemas/tables/columns/PKs/FKs, version).
// Advanced inspection methods (functions, procedures, detailed constraints)
// stay stub until Phase 4.
class PostgresAdapter final : public IDatabaseAdapter, public ISchemaInspectable {
public:
    PostgresAdapter();
    ~PostgresAdapter() override;

    PostgresAdapter(const PostgresAdapter&) = delete;
    PostgresAdapter& operator=(const PostgresAdapter&) = delete;

    [[nodiscard]] DatabaseType databaseType() const noexcept override { return DatabaseType::PostgreSQL; }
    [[nodiscard]] bool isConnected() const noexcept override { return connected_.load(std::memory_order_acquire); }

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
    void ensureConnected() const;
    std::string qualified(const std::string& table, const std::optional<std::string>& schema) const;

    PGconn* conn_ = nullptr;
    std::atomic<bool> connected_{false};
    std::optional<std::string> databaseName_;
    mutable std::mutex mutex_;
};

}
