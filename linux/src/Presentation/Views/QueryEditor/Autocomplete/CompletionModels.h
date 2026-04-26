#pragma once

#include <QString>
#include <optional>
#include <vector>

namespace gridex {

struct TableRef {
    QString name;
    std::optional<QString> alias;
};

enum class CompletionType {
    Keyword,
    Table,
    Column,
    Function,
    Join,
};

struct CompletionItem {
    QString text;
    CompletionType type = CompletionType::Keyword;
    QString detail;       // optional
    QString insertText;
    int score = 0;
    std::vector<int> matchRanges;  // char indices for highlight
};

enum class CompletionTriggerKind {
    None,
    Keyword,
    Table,
    Column,
    Join,
    SelectList,
    AfterIdentifier,
    Function,
    General,
};

struct CompletionTrigger {
    CompletionTriggerKind kind = CompletionTriggerKind::General;

    // Trigger-specific payloads. Unused fields stay empty.
    std::optional<QString> columnTable;          // Column
    std::vector<TableRef>  joinFromTables;       // Join
    std::vector<TableRef>  selectScopeTables;    // SelectList
    QString                afterIdentClause;    // AfterIdentifier
    std::vector<TableRef>  afterIdentScope;      // AfterIdentifier
};

struct CompletionContext {
    CompletionTrigger       trigger;
    QString                 prefix;
    std::vector<TableRef>   scopeTables;
};

}  // namespace gridex
