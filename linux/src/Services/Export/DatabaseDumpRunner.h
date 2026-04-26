#pragma once

#include <optional>
#include <string>

#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex {

// Blocking wrapper around pg_dump / mysqldump / sqlite3 (.dump) and their
// restore counterparts. Invoked from UI handlers; no threading here — the
// caller shows a modal spinner or accepts the freeze for now.
//
// Password is passed via environment variable (PGPASSWORD / MYSQL_PWD) so it
// never appears in `ps` output.
class DatabaseDumpRunner {
public:
    struct Result {
        bool        success = false;
        int         exitCode = 0;
        std::string errorOutput;  // program's stderr when success=false
        std::string tool;         // "pg_dump" / "mysqldump" / ...
    };

    // Backup: writes SQL dump to outPath. SQLite redirects sqlite3 stdout.
    [[nodiscard]] static Result backup(const ConnectionConfig& cfg,
                                       const std::optional<std::string>& password,
                                       const std::string& outPath);

    // Restore: reads inPath and pipes into psql/mysql/sqlite3.
    [[nodiscard]] static Result restore(const ConnectionConfig& cfg,
                                        const std::optional<std::string>& password,
                                        const std::string& inPath);

    // Returns true if the database type is supported by dump tooling.
    [[nodiscard]] static bool isSupported(DatabaseType dt);
};

}  // namespace gridex
