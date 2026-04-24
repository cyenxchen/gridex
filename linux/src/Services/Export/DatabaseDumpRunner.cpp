#include "Services/Export/DatabaseDumpRunner.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace gridex {

namespace {

struct DumpSpec {
    QString program;
    QStringList args;
    bool stdinRedirect = false;   // Restore path: feed input file via stdin
    bool stdoutRedirect = false;  // Backup path: capture stdout to output file
};

std::optional<DumpSpec> backupSpec(const ConnectionConfig& c, const QString& outPath) {
    DumpSpec s;
    switch (c.databaseType) {
        case DatabaseType::PostgreSQL:
            s.program = "pg_dump";
            s.args << "-h" << QString::fromStdString(c.host.value_or("localhost"))
                   << "-p" << QString::number(c.port.value_or(5432))
                   << "-U" << QString::fromStdString(c.username.value_or(""))
                   << "-f" << outPath
                   << QString::fromStdString(c.database.value_or(""));
            return s;
        case DatabaseType::MySQL:
            s.program = "mysqldump";
            s.args << "-h" << QString::fromStdString(c.host.value_or("localhost"))
                   << "-P" << QString::number(c.port.value_or(3306))
                   << "-u" << QString::fromStdString(c.username.value_or(""))
                   << "--result-file=" + outPath
                   << QString::fromStdString(c.database.value_or(""));
            return s;
        case DatabaseType::SQLite:
            s.program = "sqlite3";
            s.args << QString::fromStdString(c.filePath.value_or("")) << ".dump";
            s.stdoutRedirect = true;
            return s;
        default:
            return std::nullopt;
    }
}

std::optional<DumpSpec> restoreSpec(const ConnectionConfig& c, const QString& inPath) {
    DumpSpec s;
    switch (c.databaseType) {
        case DatabaseType::PostgreSQL:
            s.program = "psql";
            s.args << "-h" << QString::fromStdString(c.host.value_or("localhost"))
                   << "-p" << QString::number(c.port.value_or(5432))
                   << "-U" << QString::fromStdString(c.username.value_or(""))
                   << "-d" << QString::fromStdString(c.database.value_or(""))
                   << "-f" << inPath;
            return s;
        case DatabaseType::MySQL:
            s.program = "mysql";
            s.args << "-h" << QString::fromStdString(c.host.value_or("localhost"))
                   << "-P" << QString::number(c.port.value_or(3306))
                   << "-u" << QString::fromStdString(c.username.value_or(""))
                   << QString::fromStdString(c.database.value_or(""));
            s.stdinRedirect = true;
            return s;
        case DatabaseType::SQLite:
            s.program = "sqlite3";
            s.args << QString::fromStdString(c.filePath.value_or(""));
            s.stdinRedirect = true;
            return s;
        default:
            return std::nullopt;
    }
}

DatabaseDumpRunner::Result runSpec(const DumpSpec& spec,
                                   const ConnectionConfig& cfg,
                                   const std::optional<std::string>& password,
                                   const QString& redirectPath) {
    DatabaseDumpRunner::Result r;
    r.tool = spec.program.toStdString();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (password && !password->empty()) {
        if (cfg.databaseType == DatabaseType::PostgreSQL)
            env.insert("PGPASSWORD", QString::fromStdString(*password));
        else if (cfg.databaseType == DatabaseType::MySQL)
            env.insert("MYSQL_PWD", QString::fromStdString(*password));
    }

    QProcess proc;
    proc.setProcessEnvironment(env);
    if (spec.stdoutRedirect) {
        proc.setStandardOutputFile(redirectPath, QIODevice::Truncate);
    }
    if (spec.stdinRedirect) {
        proc.setStandardInputFile(redirectPath);
    }
    proc.start(spec.program, spec.args);
    if (!proc.waitForStarted(5000)) {
        r.errorOutput = "Could not start '" + r.tool +
                        "'. Ensure it is installed and on PATH.";
        return r;
    }
    proc.waitForFinished(-1);
    r.exitCode    = proc.exitCode();
    r.errorOutput = QString::fromUtf8(proc.readAllStandardError()).toStdString();
    r.success     = (r.exitCode == 0);
    return r;
}

}  // namespace

bool DatabaseDumpRunner::isSupported(DatabaseType dt) {
    return dt == DatabaseType::PostgreSQL
        || dt == DatabaseType::MySQL
        || dt == DatabaseType::SQLite;
}

DatabaseDumpRunner::Result DatabaseDumpRunner::backup(const ConnectionConfig& cfg,
                                                      const std::optional<std::string>& password,
                                                      const std::string& outPath) {
    const auto qOutPath = QString::fromStdString(outPath);
    const auto spec = backupSpec(cfg, qOutPath);
    if (!spec) {
        Result r;
        r.errorOutput = "Backup not supported for this database type.";
        return r;
    }
    return runSpec(*spec, cfg, password, qOutPath);
}

DatabaseDumpRunner::Result DatabaseDumpRunner::restore(const ConnectionConfig& cfg,
                                                       const std::optional<std::string>& password,
                                                       const std::string& inPath) {
    const auto qInPath = QString::fromStdString(inPath);
    const auto spec = restoreSpec(cfg, qInPath);
    if (!spec) {
        Result r;
        r.errorOutput = "Restore not supported for this database type.";
        return r;
    }
    return runSpec(*spec, cfg, password, qInPath);
}

}  // namespace gridex
