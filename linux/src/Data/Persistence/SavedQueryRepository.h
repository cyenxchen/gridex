#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Data/Persistence/AppDatabase.h"

namespace gridex {

class SavedQueryRepository {
public:
    explicit SavedQueryRepository(std::shared_ptr<AppDatabase> db);

    std::vector<AppDatabase::SavedQueryRecord> fetchAll();
    void save(const AppDatabase::SavedQueryRecord& rec);
    void remove(const std::string& id);

private:
    std::shared_ptr<AppDatabase> db_;
};

}
