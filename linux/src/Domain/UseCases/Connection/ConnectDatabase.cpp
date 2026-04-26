#include "Domain/UseCases/Connection/ConnectDatabase.h"

#include <utility>

#include <cstdio>

#include "Core/Errors/GridexError.h"
#include "Core/Protocols/Database/ISchemaInspectable.h"
#include "Data/Adapters/AdapterFactory.h"
#include "Data/Keychain/SecretStore.h"

namespace gridex {

ActiveConnection ConnectDatabaseUseCase::execute(const ConnectionConfig& config,
                                                 const std::optional<std::string>& password,
                                                 bool loadInitialSchema) {
    std::optional<std::string> pw = password;
    std::optional<std::string> degradedReason;
    if (!pw && secretStore_) {
        try {
            pw = secretStore_->loadPassword(config.id);
        } catch (const std::exception& e) {
            degradedReason = std::string("SecretStore lookup failed: ") + e.what();
            std::fprintf(stderr, "[ConnectDatabase] %s\n", degradedReason->c_str());
        }
    }

    auto adapter = createAdapter(config.databaseType);
    adapter->connect(config, pw);

    ActiveConnection conn;
    conn.id = config.id;
    conn.config = config;
    conn.connectedAt = std::chrono::system_clock::now();
    conn.degradedReason = std::move(degradedReason);

    if (loadInitialSchema) {
        // Only SQLite currently implements ISchemaInspectable; for others skip.
        if (auto* inspectable = dynamic_cast<ISchemaInspectable*>(adapter.get())) {
            try { conn.initialSchema = inspectable->fullSchemaSnapshot(std::nullopt); }
            catch (const GridexError&) { /* schema load best-effort */ }
        }
    }

    conn.adapter = std::move(adapter);
    return conn;
}

}
