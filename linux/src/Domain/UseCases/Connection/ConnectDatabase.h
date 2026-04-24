#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "Core/Models/Database/ConnectionConfig.h"
#include "Core/Models/Schema/SchemaSnapshot.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

class SecretStore;

struct ActiveConnection {
    std::string id;
    ConnectionConfig config;
    std::unique_ptr<IDatabaseAdapter> adapter;
    std::optional<SchemaSnapshot> initialSchema;
    std::chrono::system_clock::time_point connectedAt;
    // Populated when the SecretStore lookup degrades (daemon unavailable, auth race, etc.).
    // Empty when credentials were either provided directly or fetched cleanly.
    std::optional<std::string> degradedReason;
};

// Orchestrates: retrieve password from SecretStore (if needed) → create adapter
// → connect → optionally snapshot schema. Mirrors macos ConnectDatabaseUseCase.
class ConnectDatabaseUseCase {
public:
    explicit ConnectDatabaseUseCase(SecretStore* secretStore = nullptr)
        : secretStore_(secretStore) {}

    // password overrides the one stored in SecretStore when non-empty.
    ActiveConnection execute(const ConnectionConfig& config,
                             const std::optional<std::string>& password = std::nullopt,
                             bool loadInitialSchema = false);

private:
    SecretStore* secretStore_;
};

}
