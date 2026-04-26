#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex {

struct ConnectionTestResult {
    bool success = false;
    std::optional<std::string> serverVersion;
    std::chrono::duration<double> latency{};
    std::optional<std::string> errorMessage;
};

// Orchestrates a non-destructive connection probe: creates an adapter, calls
// testConnection+serverVersion, returns latency. Throws on unexpected errors.
class TestConnectionUseCase {
public:
    ConnectionTestResult execute(const ConnectionConfig& config,
                                 const std::optional<std::string>& password);
};

}
