#include "Data/Persistence/AppConnectionRepository.h"

#include <chrono>
#include <utility>

#include "Core/Errors/GridexError.h"
#include "Core/Models/Database/ConnectionConfigJson.h"

namespace gridex {

AppConnectionRepository::AppConnectionRepository(std::shared_ptr<AppDatabase> db)
    : db_(std::move(db)) {
    if (!db_) throw InternalError("AppConnectionRepository: null AppDatabase");
}

std::vector<ConnectionConfig> AppConnectionRepository::fetchAll() {
    const auto records = db_->listConnections();
    std::vector<ConnectionConfig> out;
    out.reserve(records.size());
    for (const auto& r : records) {
        try { out.push_back(json::decode(r.configJson)); }
        catch (const GridexError&) { /* skip malformed row */ }
    }
    return out;
}

std::optional<ConnectionConfig> AppConnectionRepository::fetchById(const std::string& id) {
    const auto rec = db_->getConnection(id);
    if (!rec) return std::nullopt;
    return json::decode(rec->configJson);
}

std::vector<ConnectionConfig> AppConnectionRepository::fetchByGroup(const std::string& group) {
    auto all = fetchAll();
    std::vector<ConnectionConfig> out;
    for (auto& c : all) {
        if (c.group && *c.group == group) out.push_back(std::move(c));
    }
    return out;
}

void AppConnectionRepository::save(const ConnectionConfig& config) {
    AppDatabase::ConnectionRecord rec;
    rec.id = config.id;
    rec.name = config.name;
    rec.databaseType = std::string(rawValue(config.databaseType));
    rec.configJson = json::encode(config);
    rec.updatedAt = std::chrono::system_clock::now();
    db_->upsertConnection(rec);
}

void AppConnectionRepository::remove(const std::string& id) {
    db_->deleteConnection(id);
}

}
