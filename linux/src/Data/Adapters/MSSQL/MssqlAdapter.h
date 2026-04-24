#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "Core/Protocols/Database/IDatabaseAdapter.h"
#include "Core/Protocols/Database/ISchemaInspectable.h"

namespace gridex {

// Microsoft SQL Server adapter using FreeTDS db-lib (libsybdb).
// Mirrors the PostgresAdapter pattern: synchronous, mutex-protected,
// std::atomic<bool> connected_ for race-free isConnected().
//
// Schema inspection uses information_schema / sys catalog queries
// identical to the macOS CosmoMSSQL adapter.
class MssqlAdapter final : public IDatabaseAdapter, public ISchemaInspectable {
public:
    MssqlAdapter();
    ~MssqlAdapter() override;

    MssqlAdapter(const MssqlAdapter&) = delete;
    MssqlAdapter& operator=(const MssqlAdapter&) = delete;

    [[nodiscard]] DatabaseType databaseType() const noexcept override { return DatabaseType::MSSQL; }
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

    // Stored procedures — MSSQL implements these.
    std::vector<std::string> listProcedures(const std::optional<std::string>& schema) override;
    std::string getProcedureSource(const std::string& name,
                                   const std::optional<std::string>& schema) override;
    std::vector<std::string> listProcedureParameters(const std::string& name,
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
    QueryResult executeInternal(const std::string& sql);
    void ensureConnected() const;
    std::string qualified(const std::string& table, const std::optional<std::string>& schema) const;

    void* dbproc_ = nullptr;  // actually DBPROCESS*; sybdb.h only included in .cpp
    std::optional<std::string> databaseName_;
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
};

}
