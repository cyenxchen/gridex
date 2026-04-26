#include "Data/Persistence/AppDatabase.h"

#include <cstdlib>
#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

std::int64_t timePointToUnix(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point unixToTimePoint(std::int64_t secs) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{secs}};
}

std::string errmsg(sqlite3* db) {
    const auto* m = db ? sqlite3_errmsg(db) : nullptr;
    return m ? std::string(m) : std::string{"unknown"};
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? std::string(err) : errmsg(db);
        if (err) sqlite3_free(err);
        throw InternalError("AppDatabase exec failed: " + msg);
    }
}

// Null-safe text column read. Returns "" when the column is SQL NULL — avoids
// UB from std::string(nullptr) if a future migration adds a nullable TEXT col.
std::string safeText(sqlite3_stmt* stmt, int col) {
    const auto* v = sqlite3_column_text(stmt, col);
    return v ? std::string(reinterpret_cast<const char*>(v)) : std::string{};
}

}

std::string AppDatabase::defaultPath() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path base;
    if (xdg && *xdg) base = xdg;
    else if (home && *home) base = std::filesystem::path(home) / ".local" / "share";
    else base = std::filesystem::current_path();
    base /= "gridex";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return (base / "app.sqlite").string();
}

AppDatabase::AppDatabase() = default;

AppDatabase::~AppDatabase() {
    close();
}

void AppDatabase::open(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }

    path_ = path.empty() ? defaultPath() : path;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path_.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string msg = errmsg(db_);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw InternalError("AppDatabase open failed: " + msg);
    }

    exec(db_, "PRAGMA journal_mode=WAL");
    exec(db_, "PRAGMA foreign_keys=ON");
    applyMigrations();
}

void AppDatabase::close() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void AppDatabase::applyMigrations() {
    // schema_version table tracks forward migrations so future versions know
    // what is already applied. Always created first; each migration adds a row.
    exec(db_,
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version INTEGER PRIMARY KEY,"
        "  applied_at INTEGER NOT NULL"
        ")");
    exec(db_,
        "CREATE TABLE IF NOT EXISTS connections ("
        "  id TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  database_type TEXT NOT NULL,"
        "  config_json TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ")");
    exec(db_,
        "CREATE TABLE IF NOT EXISTS history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  connection_id TEXT NOT NULL,"
        "  sql TEXT NOT NULL,"
        "  executed_at INTEGER NOT NULL,"
        "  duration_ms INTEGER NOT NULL DEFAULT 0,"
        "  succeeded INTEGER NOT NULL DEFAULT 1"
        ")");
    exec(db_,
        "CREATE INDEX IF NOT EXISTS idx_history_conn_time "
        "ON history(connection_id, executed_at DESC)");
    exec(db_,
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ")");

    const auto now = std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Mark v1 as applied (idempotent).
    exec(db_, ("INSERT OR IGNORE INTO schema_version(version, applied_at) VALUES(1, " + now + ")").c_str());

    // v2: add row_count to history, saved_queries table, group_name to connections
    sqlite3_stmt* verStmt = nullptr;
    bool v2Applied = false;
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM schema_version WHERE version=2", -1, &verStmt, nullptr) == SQLITE_OK) {
        v2Applied = sqlite3_step(verStmt) == SQLITE_ROW;
        sqlite3_finalize(verStmt);
    }
    if (!v2Applied) {
        // Add row_count column to history (ignore if already exists)
        sqlite3_exec(db_, "ALTER TABLE history ADD COLUMN row_count INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
        // Add group_name column to connections
        sqlite3_exec(db_, "ALTER TABLE connections ADD COLUMN group_name TEXT", nullptr, nullptr, nullptr);
        exec(db_,
            "CREATE TABLE IF NOT EXISTS saved_queries ("
            "  id TEXT PRIMARY KEY,"
            "  connection_id TEXT,"
            "  group_name TEXT NOT NULL DEFAULT '',"
            "  name TEXT NOT NULL,"
            "  sql TEXT NOT NULL,"
            "  created_at INTEGER NOT NULL,"
            "  updated_at INTEGER NOT NULL"
            ")");
        exec(db_, ("INSERT OR IGNORE INTO schema_version(version, applied_at) VALUES(2, " + now + ")").c_str());
    }
}

void AppDatabase::upsertConnection(const ConnectionRecord& rec) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "INSERT INTO connections(id, name, database_type, config_json, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  name=excluded.name,"
        "  database_type=excluded.database_type,"
        "  config_json=excluded.config_json,"
        "  updated_at=excluded.updated_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("upsertConnection prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, rec.id.c_str(),           -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.name.c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.databaseType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.configJson.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, timePointToUnix(rec.updatedAt));
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw InternalError("upsertConnection step: " + errmsg(db_));
    }
}

std::vector<AppDatabase::ConnectionRecord> AppDatabase::listConnections() {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "SELECT id, name, database_type, config_json, updated_at FROM connections ORDER BY name";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("listConnections prepare: " + errmsg(db_));
    }
    std::vector<ConnectionRecord> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ConnectionRecord r;
        r.id            = safeText(stmt, 0);
        r.name          = safeText(stmt, 1);
        r.databaseType  = safeText(stmt, 2);
        r.configJson    = safeText(stmt, 3);
        r.updatedAt     = unixToTimePoint(sqlite3_column_int64(stmt, 4));
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<AppDatabase::ConnectionRecord> AppDatabase::getConnection(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "SELECT id, name, database_type, config_json, updated_at FROM connections WHERE id = ?1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("getConnection prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<ConnectionRecord> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ConnectionRecord r;
        r.id           = safeText(stmt, 0);
        r.name         = safeText(stmt, 1);
        r.databaseType = safeText(stmt, 2);
        r.configJson   = safeText(stmt, 3);
        r.updatedAt    = unixToTimePoint(sqlite3_column_int64(stmt, 4));
        out = std::move(r);
    }
    sqlite3_finalize(stmt);
    return out;
}

void AppDatabase::deleteConnection(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM connections WHERE id = ?1", -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("deleteConnection prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("deleteConnection step: " + errmsg(db_));
}

void AppDatabase::appendHistory(const HistoryEntry& entry) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "INSERT INTO history(connection_id, sql, executed_at, duration_ms, row_count, succeeded) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("appendHistory prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, entry.connectionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.sql.c_str(),          -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, timePointToUnix(entry.executedAt));
    sqlite3_bind_int(stmt, 4, entry.durationMs);
    sqlite3_bind_int(stmt, 5, entry.rowCount);
    sqlite3_bind_int(stmt, 6, entry.succeeded ? 1 : 0);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("appendHistory step: " + errmsg(db_));
}

std::vector<AppDatabase::HistoryEntry> AppDatabase::listHistory(const std::string& connectionId, int limit) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "SELECT id, connection_id, sql, executed_at, duration_ms, row_count, succeeded "
        "FROM history WHERE connection_id = ?1 ORDER BY executed_at DESC LIMIT ?2";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("listHistory prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, connectionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    std::vector<HistoryEntry> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryEntry h;
        h.id            = sqlite3_column_int64(stmt, 0);
        h.connectionId  = safeText(stmt, 1);
        h.sql           = safeText(stmt, 2);
        h.executedAt    = unixToTimePoint(sqlite3_column_int64(stmt, 3));
        h.durationMs    = sqlite3_column_int(stmt, 4);
        h.rowCount      = sqlite3_column_int(stmt, 5);
        h.succeeded     = sqlite3_column_int(stmt, 6) != 0;
        out.push_back(std::move(h));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<AppDatabase::HistoryEntry> AppDatabase::listAllHistory(int limit) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "SELECT id, connection_id, sql, executed_at, duration_ms, row_count, succeeded "
        "FROM history ORDER BY executed_at DESC LIMIT ?1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("listAllHistory prepare: " + errmsg(db_));
    }
    sqlite3_bind_int(stmt, 1, limit);
    std::vector<HistoryEntry> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HistoryEntry h;
        h.id           = sqlite3_column_int64(stmt, 0);
        h.connectionId = safeText(stmt, 1);
        h.sql          = safeText(stmt, 2);
        h.executedAt   = unixToTimePoint(sqlite3_column_int64(stmt, 3));
        h.durationMs   = sqlite3_column_int(stmt, 4);
        h.rowCount     = sqlite3_column_int(stmt, 5);
        h.succeeded    = sqlite3_column_int(stmt, 6) != 0;
        out.push_back(std::move(h));
    }
    sqlite3_finalize(stmt);
    return out;
}

void AppDatabase::clearAllHistory() {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");
    exec(db_, "DELETE FROM history");
}

void AppDatabase::upsertSavedQuery(const SavedQueryRecord& rec) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "INSERT INTO saved_queries(id, connection_id, group_name, name, sql, created_at, updated_at) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7) "
        "ON CONFLICT(id) DO UPDATE SET "
        "  connection_id=excluded.connection_id,"
        "  group_name=excluded.group_name,"
        "  name=excluded.name,"
        "  sql=excluded.sql,"
        "  updated_at=excluded.updated_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("upsertSavedQuery prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, rec.id.c_str(),           -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.connectionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.groupName.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, rec.name.c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, rec.sql.c_str(),          -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, timePointToUnix(rec.createdAt));
    sqlite3_bind_int64(stmt, 7, timePointToUnix(rec.updatedAt));
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("upsertSavedQuery step: " + errmsg(db_));
}

std::vector<AppDatabase::SavedQueryRecord> AppDatabase::listSavedQueries() {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "SELECT id, connection_id, group_name, name, sql, created_at, updated_at "
        "FROM saved_queries ORDER BY group_name, name";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("listSavedQueries prepare: " + errmsg(db_));
    }
    std::vector<SavedQueryRecord> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SavedQueryRecord r;
        r.id           = safeText(stmt, 0);
        r.connectionId = safeText(stmt, 1);
        r.groupName    = safeText(stmt, 2);
        r.name         = safeText(stmt, 3);
        r.sql          = safeText(stmt, 4);
        r.createdAt    = unixToTimePoint(sqlite3_column_int64(stmt, 5));
        r.updatedAt    = unixToTimePoint(sqlite3_column_int64(stmt, 6));
        out.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return out;
}

void AppDatabase::deleteSavedQuery(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM saved_queries WHERE id=?1", -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("deleteSavedQuery prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("deleteSavedQuery step: " + errmsg(db_));
}

void AppDatabase::setConnectionGroup(const std::string& connectionId, const std::string& group) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "UPDATE connections SET group_name=?1 WHERE id=?2", -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("setConnectionGroup prepare: " + errmsg(db_));
    }
    if (group.empty()) sqlite3_bind_null(stmt, 1);
    else sqlite3_bind_text(stmt, 1, group.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, connectionId.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("setConnectionGroup step: " + errmsg(db_));
}

std::string AppDatabase::getConnectionGroup(const std::string& connectionId) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT group_name FROM connections WHERE id=?1", -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("getConnectionGroup prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, connectionId.c_str(), -1, SQLITE_TRANSIENT);
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW) out = safeText(stmt, 0);
    sqlite3_finalize(stmt);
    return out;
}

void AppDatabase::setSetting(const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    const char* sql =
        "INSERT INTO settings(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("setSetting prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw InternalError("setSetting step: " + errmsg(db_));
}

std::optional<std::string> AppDatabase::getSetting(const std::string& key) {
    std::lock_guard lock(mutex_);
    if (!db_) throw InternalError("AppDatabase not open");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM settings WHERE key = ?1", -1, &stmt, nullptr) != SQLITE_OK) {
        throw InternalError("getSetting prepare: " + errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = safeText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

}
