// Phase 2c smoke: SecretStore + JSON + ConnectionRepository + TestConnection UseCase.

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Models/Database/ConnectionConfigJson.h"
#include "Data/Keychain/SecretStore.h"
#include "Data/Persistence/AppConnectionRepository.h"
#include "Data/Persistence/AppDatabase.h"
#include "Domain/UseCases/Connection/ConnectDatabase.h"
#include "Domain/UseCases/Connection/DeleteConnection.h"
#include "Domain/UseCases/Connection/TestConnection.h"

using namespace gridex;

static ConnectionConfig makeSqliteConfig(const std::string& path, const std::string& id = "conn-sqlite-1") {
    ConnectionConfig c;
    c.id = id;
    c.name = "Local SQLite";
    c.databaseType = DatabaseType::SQLite;
    c.filePath = path;
    c.sslEnabled = false;
    return c;
}

static void testJsonRoundTrip() {
    auto cfg = makeSqliteConfig("/tmp/foo.db");
    cfg.colorTag = ColorTag::Green;
    cfg.group = "Dev";
    cfg.username = "alice";
    cfg.sshConfig = SSHTunnelConfig{"bastion", 2222, "op", SSHAuthMethod::PrivateKey, std::string{"/tmp/id"}};

    const auto encoded = json::encode(cfg);
    const auto decoded = json::decode(encoded);
    assert(decoded.id == cfg.id);
    assert(decoded.databaseType == DatabaseType::SQLite);
    assert(decoded.colorTag && *decoded.colorTag == ColorTag::Green);
    assert(decoded.filePath && *decoded.filePath == "/tmp/foo.db");
    assert(decoded.sshConfig && decoded.sshConfig->port == 2222);
    assert(decoded.sshConfig->authMethod == SSHAuthMethod::PrivateKey);
    std::printf("JSON round-trip OK (%zu bytes)\n", encoded.size());
}

static void testRepository() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-repo-smoke.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    auto db = std::make_shared<AppDatabase>();
    db->open(tmp.string());

    AppConnectionRepository repo(db);
    auto cfg = makeSqliteConfig("/tmp/bar.db");
    cfg.group = "Prod";

    repo.save(cfg);
    const auto all = repo.fetchAll();
    assert(all.size() == 1);
    assert(all[0].id == cfg.id);
    assert(all[0].databaseType == DatabaseType::SQLite);

    const auto byId = repo.fetchById(cfg.id);
    assert(byId && byId->name == "Local SQLite");

    const auto byGroup = repo.fetchByGroup("Prod");
    assert(byGroup.size() == 1);

    repo.remove(cfg.id);
    assert(repo.fetchAll().empty());

    std::filesystem::remove(tmp, ec);
    std::printf("Repository CRUD round-trip OK\n");
}

static void testTestConnectionUseCase() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-tc-smoke.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    // Pre-create the file so open+testConnection succeed
    { FILE* f = std::fopen(tmp.c_str(), "w"); if (f) std::fclose(f); }

    auto cfg = makeSqliteConfig(tmp.string());
    TestConnectionUseCase uc;
    const auto res = uc.execute(cfg, std::nullopt);
    assert(res.success);
    assert(res.serverVersion && res.serverVersion->rfind("SQLite ", 0) == 0);
    assert(res.latency.count() >= 0.0);

    std::filesystem::remove(tmp, ec);
    std::printf("TestConnection UseCase OK (version: %s)\n", res.serverVersion->c_str());
}

static void testConnectDatabaseUseCase() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-connect-smoke.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    auto cfg = makeSqliteConfig(tmp.string());
    ConnectDatabaseUseCase uc;
    auto active = uc.execute(cfg, std::nullopt, /*loadInitialSchema=*/true);
    assert(active.adapter);
    assert(active.adapter->isConnected());
    const auto ver = active.adapter->serverVersion();
    assert(ver.rfind("SQLite ", 0) == 0);
    active.adapter->disconnect();

    std::filesystem::remove(tmp, ec);
    std::printf("ConnectDatabase UseCase OK\n");
}

static void testSecretStore() {
    SecretStore store("com.gridex.smoketest");
    if (!store.isAvailable()) {
        std::printf("SecretStore SKIPPED (no Secret Service — headless/WSL?)\n");
        return;
    }

    const std::string_view key = "phase2c.round-trip";
    store.save(key, "hunter2");
    const auto loaded = store.load(key);
    assert(loaded && *loaded == "hunter2");

    store.save(key, "hunter3"); // overwrite
    assert(*store.load(key) == "hunter3");

    store.remove(key);
    assert(!store.load(key).has_value());
    std::printf("SecretStore round-trip OK (store/load/overwrite/remove)\n");
}

static void testDeleteConnectionUseCase() {
    const auto tmp = std::filesystem::temp_directory_path() / "gridex-delete-smoke.sqlite";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    auto db = std::make_shared<AppDatabase>();
    db->open(tmp.string());
    AppConnectionRepository repo(db);

    auto cfg = makeSqliteConfig("/tmp/del.db", "conn-to-delete");
    repo.save(cfg);
    assert(repo.fetchAll().size() == 1);

    SecretStore secrets("com.gridex.smoketest");
    DeleteConnectionUseCase uc(&repo, &secrets);
    uc.execute(cfg.id);
    assert(repo.fetchAll().empty());

    std::filesystem::remove(tmp, ec);
    std::printf("DeleteConnection UseCase OK\n");
}

int main() {
    try {
        testJsonRoundTrip();
        testRepository();
        testTestConnectionUseCase();
        testConnectDatabaseUseCase();
        testSecretStore();
        testDeleteConnectionUseCase();
        std::printf("ALL PHASE 2C SMOKE TESTS PASSED\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SMOKE FAILED: %s\n", e.what());
        return 1;
    }
}
