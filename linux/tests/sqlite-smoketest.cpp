// Smoke test for Phase 2b: SqliteAdapter + AppDatabase basic round-trip.
// Not a test framework — plain asserts. Invoked from CMake via a separate target.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Models/Database/RowValue.h"
#include "Data/Adapters/SQLite/SqliteAdapter.h"
#include "Data/Persistence/AppDatabase.h"

using namespace gridex;

static void testSqliteAdapter() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-smoketest.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    ConnectionConfig cfg;
    cfg.id = "test-id";
    cfg.name = "smoke";
    cfg.databaseType = DatabaseType::SQLite;
    cfg.filePath = tmp.string();

    SqliteAdapter adapter;
    adapter.connect(cfg, std::nullopt);
    assert(adapter.isConnected());

    const auto ver = adapter.serverVersion();
    std::printf("server version: %s\n", ver.c_str());
    assert(ver.rfind("SQLite ", 0) == 0);

    (void)adapter.executeRaw("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)");

    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString("Alice"));
    const auto ins1 = adapter.execute("INSERT INTO users(name) VALUES(?)", params);
    assert(ins1.rowsAffected == 1);

    std::unordered_map<std::string, RowValue> row2;
    row2["name"] = RowValue::makeString("Bob");
    const auto ins2 = adapter.insertRow("users", std::nullopt, row2);
    assert(ins2.rowsAffected == 1);

    const auto sel = adapter.executeRaw("SELECT id, name FROM users ORDER BY id");
    assert(sel.queryType == QueryType::Select);
    assert(sel.rows.size() == 2);
    assert(sel.columns.size() == 2);
    assert(sel.columns[0].name == "id");
    assert(sel.columns[1].name == "name");
    assert(sel.rows[0][1].asString() == "Alice");
    assert(sel.rows[1][1].asString() == "Bob");

    const auto one = adapter.executeRaw("SELECT 1");
    assert(one.rows.size() == 1);
    assert(one.rows[0][0].asInteger() == 1);

    const auto tables = adapter.listTables(std::nullopt);
    assert(tables.size() == 1);
    assert(tables[0].name == "users");

    const auto desc = adapter.describeTable("users", std::nullopt);
    assert(desc.columns.size() == 2);
    assert(desc.columns[0].isPrimaryKey);

    const auto snap = adapter.fullSchemaSnapshot(std::nullopt);
    assert(snap.databaseType == DatabaseType::SQLite);
    assert(snap.totalTableCount() == 1);

    adapter.disconnect();
    assert(!adapter.isConnected());
    std::filesystem::remove(tmp, ec);
    std::printf("SqliteAdapter smoke OK (2 rows round-trip, schema inspection OK)\n");
}

static void testAppDatabase() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-appdb-smoketest.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    AppDatabase app;
    app.open(tmp.string());

    AppDatabase::ConnectionRecord rec;
    rec.id = "conn-1";
    rec.name = "Local Postgres";
    rec.databaseType = "postgresql";
    rec.configJson = R"({"host":"localhost","port":5432})";
    rec.updatedAt = std::chrono::system_clock::now();
    app.upsertConnection(rec);

    const auto list = app.listConnections();
    assert(list.size() == 1);
    assert(list[0].id == "conn-1");
    assert(list[0].name == "Local Postgres");

    app.setSetting("theme", "dark");
    const auto theme = app.getSetting("theme");
    assert(theme && *theme == "dark");

    AppDatabase::HistoryEntry h;
    h.connectionId = "conn-1";
    h.sql = "SELECT NOW()";
    h.executedAt = std::chrono::system_clock::now();
    h.durationMs = 7;
    h.succeeded = true;
    app.appendHistory(h);
    app.appendHistory(h);
    const auto hist = app.listHistory("conn-1", 10);
    assert(hist.size() == 2);
    assert(hist[0].sql == "SELECT NOW()");

    app.deleteConnection("conn-1");
    assert(app.listConnections().empty());

    app.close();
    std::filesystem::remove(tmp, ec);
    std::printf("AppDatabase smoke OK (connection CRUD, settings, history)\n");
}

int main() {
    try {
        testSqliteAdapter();
        testAppDatabase();
        std::printf("ALL PHASE 2B SMOKE TESTS PASSED\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SMOKE FAILED: %s\n", e.what());
        return 1;
    }
}
