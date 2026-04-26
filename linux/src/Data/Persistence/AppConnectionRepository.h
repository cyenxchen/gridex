#pragma once

#include <memory>

#include "Data/Persistence/AppDatabase.h"
#include "Domain/Repositories/IConnectionRepository.h"

namespace gridex {

// IConnectionRepository backed by AppDatabase + hand-rolled JSON serializer.
// Secrets are NOT stored here — use SecretStore keyed by ConnectionConfig::id.
class AppConnectionRepository final : public IConnectionRepository {
public:
    explicit AppConnectionRepository(std::shared_ptr<AppDatabase> db);

    std::vector<ConnectionConfig> fetchAll() override;
    std::optional<ConnectionConfig> fetchById(const std::string& id) override;
    std::vector<ConnectionConfig> fetchByGroup(const std::string& group) override;
    void save(const ConnectionConfig& config) override;
    void remove(const std::string& id) override;

private:
    std::shared_ptr<AppDatabase> db_;
};

}
