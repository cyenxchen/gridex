#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "Core/Protocols/Database/IDatabaseAdapter.h"

// Forward-declare mongocxx types to avoid including mongocxx headers in every
// translation unit that includes this header.
namespace mongocxx {
inline namespace v_noabi {
class client;
class database;
}
}

namespace gridex {

// MongoDB adapter using the official mongo-cxx-driver (mongocxx).
// IDatabaseAdapter method mapping for the document-oriented model:
//   listDatabases()   → client.list_database_names()
//   listSchemas()     → ["default"]  (MongoDB has no schema concept)
//   listTables()      → db.list_collection_names()  (collections = "tables")
//   describeTable()   → sample first document, infer field names as columns
//   execute()         → db.run_command() with JSON command string
//   executeRaw()      → same
//   fetchRows()       → collection.find({}).skip(offset).limit(limit)
//   insertRow()       → collection.insert_one()
//   updateRow()       → collection.update_one()
//   deleteRow()       → collection.delete_one()
//   serverVersion()   → db.run_command({"buildInfo": 1}) → version field
//   transactions      → client session with start/commit/abort
//
// mongocxx::instance must be created exactly once per process — enforced
// via std::call_once in the constructor.
class MongodbAdapter final : public IDatabaseAdapter {
public:
    MongodbAdapter();
    ~MongodbAdapter() override;

    MongodbAdapter(const MongodbAdapter&) = delete;
    MongodbAdapter& operator=(const MongodbAdapter&) = delete;

    [[nodiscard]] DatabaseType databaseType() const noexcept override { return DatabaseType::MongoDB; }
    [[nodiscard]] bool isConnected() const noexcept override { return connected_.load(std::memory_order_acquire); }

    void connect(const ConnectionConfig& config, const std::optional<std::string>& password) override;
    void disconnect() override;
    bool testConnection(const ConnectionConfig& config, const std::optional<std::string>& password) override;

    QueryResult execute(const std::string& query, const std::vector<QueryParameter>& parameters) override;
    QueryResult executeRaw(const std::string& sql) override;

    std::vector<std::string> listDatabases() override;
    std::vector<std::string> listSchemas(const std::optional<std::string>& database) override;
    std::vector<TableInfo>   listTables(const std::optional<std::string>& schema) override;
    std::vector<ViewInfo>    listViews(const std::optional<std::string>& schema) override;
    TableDescription         describeTable(const std::string& name,
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

    // Document-level operations for MongoCollectionView
    std::vector<std::string> findDocuments(const std::string& collection,
                                           const std::string& filterJson,
                                           int limit, int skip);
    std::string insertDocument(const std::string& collection,
                               const std::string& json);
    void updateDocument(const std::string& collection,
                        const std::string& idJson,
                        const std::string& json);
    void deleteDocument(const std::string& collection,
                        const std::string& idJson);
    long countDocuments(const std::string& collection,
                        const std::string& filterJson);

private:
    void ensureConnected() const;

    // Pimpl for mongocxx types — keeps mongocxx headers out of this header.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string currentDb_;
    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
};

} // namespace gridex
