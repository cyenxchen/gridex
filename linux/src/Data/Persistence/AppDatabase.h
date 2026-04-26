#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace gridex {

// SQLite-backed app persistence: connection list, query history, settings.
// Stored under XDG_DATA_HOME/gridex or ~/.local/share/gridex.
class AppDatabase {
public:
    struct ConnectionRecord {
        std::string id;                  // UUID
        std::string name;
        std::string databaseType;        // rawValue of DatabaseType
        std::string configJson;          // serialized ConnectionConfig (no secrets)
        std::chrono::system_clock::time_point updatedAt;
    };

    struct HistoryEntry {
        std::int64_t id = 0;
        std::string connectionId;
        std::string sql;
        std::chrono::system_clock::time_point executedAt;
        int durationMs = 0;
        int rowCount = 0;
        bool succeeded = true;
    };

    struct SavedQueryRecord {
        std::string id;
        std::string connectionId;
        std::string groupName;
        std::string name;
        std::string sql;
        std::chrono::system_clock::time_point createdAt;
        std::chrono::system_clock::time_point updatedAt;
    };

    AppDatabase();
    ~AppDatabase();

    AppDatabase(const AppDatabase&) = delete;
    AppDatabase& operator=(const AppDatabase&) = delete;

    // Opens the DB at the given path (or default path if empty) and applies migrations.
    void open(const std::string& path = {});
    void close();

    // Connection CRUD
    void upsertConnection(const ConnectionRecord& rec);
    std::vector<ConnectionRecord> listConnections();
    std::optional<ConnectionRecord> getConnection(const std::string& id);
    void deleteConnection(const std::string& id);

    // History
    void appendHistory(const HistoryEntry& entry);
    std::vector<HistoryEntry> listHistory(const std::string& connectionId, int limit = 100);
    std::vector<HistoryEntry> listAllHistory(int limit = 200);
    void clearAllHistory();

    // Saved queries
    void upsertSavedQuery(const SavedQueryRecord& rec);
    std::vector<SavedQueryRecord> listSavedQueries();
    void deleteSavedQuery(const std::string& id);

    // Connection group support
    void setConnectionGroup(const std::string& connectionId, const std::string& group);
    std::string getConnectionGroup(const std::string& connectionId);

    // Settings (string KV)
    void setSetting(const std::string& key, const std::string& value);
    std::optional<std::string> getSetting(const std::string& key);

    [[nodiscard]] std::string path() const { return path_; }

private:
    void applyMigrations();
    static std::string defaultPath();

    sqlite3* db_ = nullptr;
    std::string path_;
    mutable std::mutex mutex_;
};

}
