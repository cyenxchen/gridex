#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex {

// Mirrors macos ConnectionRepository protocol. Persists ConnectionConfig records
// (sans secrets — those live in SecretStore).
class IConnectionRepository {
public:
    virtual ~IConnectionRepository() = default;

    virtual std::vector<ConnectionConfig> fetchAll() = 0;
    virtual std::optional<ConnectionConfig> fetchById(const std::string& id) = 0;
    virtual std::vector<ConnectionConfig> fetchByGroup(const std::string& group) = 0;
    virtual void save(const ConnectionConfig& config) = 0;
    virtual void remove(const std::string& id) = 0;
};

}
