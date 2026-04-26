// Phase 2d integration smoke: PostgresAdapter + MysqlAdapter against docker-compose stacks.
//
// Expected containers (linux/tests/docker-compose.yml):
//   gridex-pg-test    postgres://gridex:gridex@localhost:15432/gridex_test
//   gridex-mysql-test mysql://gridex:gridex@localhost:13306/gridex_test
//
// Override endpoints with env vars:
//   GRIDEX_TEST_PG_HOST / _PORT / _USER / _PASSWORD / _DB
//   GRIDEX_TEST_MYSQL_HOST / _PORT / _USER / _PASSWORD / _DB

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "Core/Enums/DatabaseType.h"
#include "Core/Models/Database/ConnectionConfig.h"
#include "Data/Adapters/AdapterFactory.h"
#include "Domain/UseCases/Connection/TestConnection.h"

using namespace gridex;

namespace {

std::string envOr(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

int envIntOr(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    try { return std::stoi(v); } catch (...) { return fallback; }
}

ConnectionConfig pgConfig() {
    ConnectionConfig c;
    c.id = "pg-it";
    c.name = "IT-PG";
    c.databaseType = DatabaseType::PostgreSQL;
    c.host     = envOr("GRIDEX_TEST_PG_HOST", "localhost");
    c.port     = envIntOr("GRIDEX_TEST_PG_PORT", 15432);
    c.username = envOr("GRIDEX_TEST_PG_USER", "gridex");
    c.database = envOr("GRIDEX_TEST_PG_DB",   "gridex_test");
    return c;
}

ConnectionConfig mysqlConfig() {
    ConnectionConfig c;
    c.id = "mysql-it";
    c.name = "IT-MySQL";
    c.databaseType = DatabaseType::MySQL;
    c.host     = envOr("GRIDEX_TEST_MYSQL_HOST", "127.0.0.1");
    c.port     = envIntOr("GRIDEX_TEST_MYSQL_PORT", 13306);
    c.username = envOr("GRIDEX_TEST_MYSQL_USER", "gridex");
    c.database = envOr("GRIDEX_TEST_MYSQL_DB",   "gridex_test");
    return c;
}

void exercise(IDatabaseAdapter& a, const std::string& label) {
    std::printf("[%s] serverVersion: %s\n", label.c_str(), a.serverVersion().c_str());

    const auto one = a.executeRaw("SELECT 1");
    assert(one.rows.size() == 1 && one.rows[0].size() == 1);
    assert(one.rows[0][0].tryIntValue() == 1);
    std::printf("[%s] SELECT 1 -> %lld\n", label.c_str(),
                static_cast<long long>(one.rows[0][0].tryIntValue().value_or(-1)));

    a.executeRaw("DROP TABLE IF EXISTS gridex_smoke");
    a.executeRaw("CREATE TABLE gridex_smoke (id SERIAL PRIMARY KEY, note TEXT NOT NULL)");

    std::vector<QueryParameter> params;
    params.emplace_back(RowValue::makeString("hello"));
    if (a.databaseType() == DatabaseType::PostgreSQL) {
        a.execute("INSERT INTO gridex_smoke(note) VALUES($1)", params);
    } else {
        a.execute("INSERT INTO gridex_smoke(note) VALUES(?)", params);
    }

    const auto sel = a.executeRaw("SELECT id, note FROM gridex_smoke ORDER BY id");
    assert(sel.rows.size() == 1);
    assert(sel.rows[0][1].tryStringValue().value_or("") == "hello");

    const auto tables = a.listTables(std::nullopt);
    bool sawTable = false;
    for (const auto& t : tables) if (t.name == "gridex_smoke") { sawTable = true; break; }
    assert(sawTable);

    const auto desc = a.describeTable("gridex_smoke", std::nullopt);
    assert(desc.columns.size() == 2);
    bool sawPk = false;
    for (const auto& c : desc.columns) if (c.isPrimaryKey && c.name == "id") { sawPk = true; break; }
    assert(sawPk);

    a.executeRaw("DROP TABLE IF EXISTS gridex_smoke");
    std::printf("[%s] round-trip + schema OK\n", label.c_str());
}

} // namespace

int main() {
    // CREATE TABLE differs between PG (SERIAL) and MySQL (AUTO_INCREMENT).
    // We use dialect-specific DDL per adapter below.
    bool ranAny = false;

    // PostgreSQL
    try {
        auto cfg = pgConfig();
        TestConnectionUseCase tc;
        const auto res = tc.execute(cfg, std::string("gridex"));
        if (!res.success) {
            std::printf("[PG] test skipped (%s)\n", res.errorMessage.value_or("unreachable").c_str());
        } else {
            auto adapter = createAdapter(cfg.databaseType);
            adapter->connect(cfg, std::string("gridex"));
            exercise(*adapter, "PG");
            adapter->disconnect();
            ranAny = true;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[PG] FAILED: %s\n", e.what());
        return 1;
    }

    // MySQL — CREATE TABLE needs AUTO_INCREMENT, not SERIAL. Special-case.
    try {
        auto cfg = mysqlConfig();
        TestConnectionUseCase tc;
        const auto res = tc.execute(cfg, std::string("gridex"));
        if (!res.success) {
            std::printf("[MySQL] test skipped (%s)\n", res.errorMessage.value_or("unreachable").c_str());
        } else {
            auto adapter = createAdapter(cfg.databaseType);
            adapter->connect(cfg, std::string("gridex"));
            std::printf("[MySQL] serverVersion: %s\n", adapter->serverVersion().c_str());

            const auto one = adapter->executeRaw("SELECT 1");
            assert(one.rows.size() == 1);
            assert(one.rows[0][0].tryIntValue() == 1);
            std::printf("[MySQL] SELECT 1 -> %lld\n",
                        static_cast<long long>(one.rows[0][0].tryIntValue().value_or(-1)));

            adapter->executeRaw("DROP TABLE IF EXISTS gridex_smoke");
            adapter->executeRaw("CREATE TABLE gridex_smoke (id INT AUTO_INCREMENT PRIMARY KEY, note TEXT NOT NULL)");

            std::vector<QueryParameter> params;
            params.emplace_back(RowValue::makeString("world"));
            adapter->execute("INSERT INTO gridex_smoke(note) VALUES(?)", params);

            const auto sel = adapter->executeRaw("SELECT id, note FROM gridex_smoke ORDER BY id");
            assert(sel.rows.size() == 1);
            assert(sel.rows[0][1].tryStringValue().value_or("") == "world");

            const auto tables = adapter->listTables(std::nullopt);
            bool sawTable = false;
            for (const auto& t : tables) if (t.name == "gridex_smoke") { sawTable = true; break; }
            assert(sawTable);

            adapter->executeRaw("DROP TABLE IF EXISTS gridex_smoke");
            adapter->disconnect();
            std::printf("[MySQL] round-trip + schema OK\n");
            ranAny = true;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[MySQL] FAILED: %s\n", e.what());
        return 1;
    }

    if (!ranAny) {
        std::printf("SKIPPED: neither PG nor MySQL container reachable. Start them with:\n"
                    "  docker compose -f linux/tests/docker-compose.yml up -d\n");
        return 0;
    }
    std::printf("ALL PHASE 2D SMOKE TESTS PASSED\n");
    return 0;
}
