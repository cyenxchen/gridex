#pragma once

#include <memory>

#include "Core/Enums/DatabaseType.h"
#include "Core/Protocols/Database/IDatabaseAdapter.h"

namespace gridex {

// Creates a concrete IDatabaseAdapter for the given database type.
// Unimplemented adapters (PostgreSQL/MySQL/MSSQL/MongoDB/Redis) throw
// gridex::ConfigurationError until Phase 2d/4 lands.
std::unique_ptr<IDatabaseAdapter> createAdapter(DatabaseType type);

}
