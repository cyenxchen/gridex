#include "Domain/UseCases/Connection/TestConnection.h"

#include <chrono>

#include "Core/Errors/GridexError.h"
#include "Data/Adapters/AdapterFactory.h"

namespace gridex {

ConnectionTestResult TestConnectionUseCase::execute(const ConnectionConfig& config,
                                                    const std::optional<std::string>& password) {
    // Single connect/probe/disconnect to avoid 3× round-trip cost the older impl paid.
    ConnectionTestResult result;
    const auto start = std::chrono::steady_clock::now();
    try {
        auto adapter = createAdapter(config.databaseType);
        adapter->connect(config, password);
        result.success = true;
        try { result.serverVersion = adapter->serverVersion(); }
        catch (const GridexError&) { /* version probe is best-effort */ }
        adapter->disconnect();
    } catch (const GridexError& e) {
        result.success = false;
        result.errorMessage = e.what();
    }
    result.latency = std::chrono::steady_clock::now() - start;
    return result;
}

}
