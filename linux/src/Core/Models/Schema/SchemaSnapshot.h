#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SQLDialect.h"

namespace gridex {

enum class TableKind {
    Table,
    View,
    MaterializedView,
    ForeignTable,
};

inline std::string_view rawValue(TableKind k) {
    switch (k) {
        case TableKind::Table:            return "table";
        case TableKind::View:             return "view";
        case TableKind::MaterializedView: return "materializedView";
        case TableKind::ForeignTable:     return "foreignTable";
    }
    return "";
}

enum class ForeignKeyAction {
    Cascade,
    SetNull,
    SetDefault,
    Restrict,
    NoAction,
};

inline std::string_view rawValue(ForeignKeyAction a) {
    switch (a) {
        case ForeignKeyAction::Cascade:    return "CASCADE";
        case ForeignKeyAction::SetNull:    return "SET NULL";
        case ForeignKeyAction::SetDefault: return "SET DEFAULT";
        case ForeignKeyAction::Restrict:   return "RESTRICT";
        case ForeignKeyAction::NoAction:   return "NO ACTION";
    }
    return "";
}

enum class ConstraintType {
    PrimaryKey,
    Unique,
    Check,
    Exclusion,
};

inline std::string_view rawValue(ConstraintType t) {
    switch (t) {
        case ConstraintType::PrimaryKey: return "PRIMARY KEY";
        case ConstraintType::Unique:     return "UNIQUE";
        case ConstraintType::Check:      return "CHECK";
        case ConstraintType::Exclusion:  return "EXCLUSION";
    }
    return "";
}

struct ColumnInfo {
    std::string name;
    std::string dataType;
    bool isNullable = true;
    std::optional<std::string> defaultValue;
    bool isPrimaryKey = false;
    bool isAutoIncrement = false;
    std::optional<std::string> comment;
    int ordinalPosition = 0;
    std::optional<int> characterMaxLength;
    std::optional<std::string> checkConstraint;

    bool operator==(const ColumnInfo&) const noexcept = default;
};

struct IndexInfo {
    std::string name;
    std::vector<std::string> columns;
    bool isUnique = false;
    std::optional<std::string> type;
    std::optional<std::string> tableName;
    std::optional<std::string> condition;
    std::optional<std::string> include;
    std::optional<std::string> comment;

    bool operator==(const IndexInfo&) const noexcept = default;
};

struct ForeignKeyInfo {
    std::optional<std::string> name;
    std::vector<std::string> columns;
    std::string referencedTable;
    std::vector<std::string> referencedColumns;
    ForeignKeyAction onDelete = ForeignKeyAction::NoAction;
    ForeignKeyAction onUpdate = ForeignKeyAction::NoAction;

    [[nodiscard]] std::string column() const {
        std::string out;
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) out += ", ";
            out += columns[i];
        }
        return out;
    }

    [[nodiscard]] std::string referencedColumn() const {
        std::string out;
        for (std::size_t i = 0; i < referencedColumns.size(); ++i) {
            if (i > 0) out += ", ";
            out += referencedColumns[i];
        }
        return out;
    }

    bool operator==(const ForeignKeyInfo&) const noexcept = default;
};

struct ConstraintInfo {
    std::string name;
    ConstraintType type = ConstraintType::Check;
    std::vector<std::string> columns;
    std::optional<std::string> definition;

    bool operator==(const ConstraintInfo&) const noexcept = default;
};

struct TableDescription {
    std::string name;
    std::optional<std::string> schema;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
    std::vector<ForeignKeyInfo> foreignKeys;
    std::vector<ConstraintInfo> constraints;
    std::optional<std::string> comment;
    std::optional<int> estimatedRowCount;

    [[nodiscard]] std::vector<ColumnInfo> primaryKeyColumns() const {
        std::vector<ColumnInfo> out;
        for (const auto& c : columns) if (c.isPrimaryKey) out.push_back(c);
        return out;
    }

    [[nodiscard]] std::string toDDL(SQLDialect dialect) const {
        std::string ddl = "CREATE TABLE " + quoteIdentifier(dialect, name) + " (\n";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            const auto& c = columns[i];
            ddl += "  " + c.name + " " + c.dataType;
            if (!c.isNullable) ddl += " NOT NULL";
            if (c.defaultValue) ddl += " DEFAULT " + *c.defaultValue;
            if (i + 1 < columns.size()) ddl += ",";
            ddl += "\n";
        }
        ddl += ");";
        return ddl;
    }

    bool operator==(const TableDescription&) const noexcept = default;
};

struct TableInfo {
    std::string name;
    std::optional<std::string> schema;
    TableKind type = TableKind::Table;
    std::optional<int> estimatedRowCount;
};

struct ViewInfo {
    std::string name;
    std::optional<std::string> schema;
    std::optional<std::string> definition;
    bool isMaterialized = false;
};

struct FunctionInfo {
    std::string name;
    std::optional<std::string> schema;
    std::string returnType;
    std::optional<std::string> parameters;
    std::optional<std::string> language;
};

struct EnumInfo {
    std::string name;
    std::optional<std::string> schema;
    std::vector<std::string> values;
};

struct SchemaInfo {
    std::string name;
    std::vector<TableDescription> tables;
    std::vector<ViewInfo> views;
    std::vector<FunctionInfo> functions;
    std::vector<EnumInfo> enums;
};

struct SchemaSnapshot {
    std::string databaseName;
    DatabaseType databaseType = DatabaseType::PostgreSQL;
    std::vector<SchemaInfo> schemas;
    std::chrono::system_clock::time_point capturedAt;

    [[nodiscard]] std::vector<TableDescription> allTables() const {
        std::vector<TableDescription> out;
        for (const auto& s : schemas) {
            for (const auto& t : s.tables) out.push_back(t);
        }
        return out;
    }

    [[nodiscard]] std::size_t totalTableCount() const noexcept {
        std::size_t n = 0;
        for (const auto& s : schemas) n += s.tables.size();
        return n;
    }
};

struct ColumnStatistics {
    std::string columnName;
    std::optional<int> distinctCount;
    std::optional<double> nullRatio;
    std::optional<std::vector<std::string>> topValues;
    std::optional<std::string> minValue;
    std::optional<std::string> maxValue;
};

struct QueryStatisticsEntry {
    std::string query;
    int callCount = 0;
    std::chrono::duration<double> totalTime{};
    std::chrono::duration<double> meanTime{};
    std::chrono::duration<double> minTime{};
    std::chrono::duration<double> maxTime{};
};

}
