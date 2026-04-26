#include "Data/Persistence/SavedQueryRepository.h"

#include <utility>

#include "Core/Errors/GridexError.h"

namespace gridex {

SavedQueryRepository::SavedQueryRepository(std::shared_ptr<AppDatabase> db)
    : db_(std::move(db)) {
    if (!db_) throw InternalError("SavedQueryRepository: null AppDatabase");
}

std::vector<AppDatabase::SavedQueryRecord> SavedQueryRepository::fetchAll() {
    return db_->listSavedQueries();
}

void SavedQueryRepository::save(const AppDatabase::SavedQueryRecord& rec) {
    db_->upsertSavedQuery(rec);
}

void SavedQueryRepository::remove(const std::string& id) {
    db_->deleteSavedQuery(id);
}

}
