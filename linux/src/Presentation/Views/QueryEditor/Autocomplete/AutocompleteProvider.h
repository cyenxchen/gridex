#pragma once

#include "Core/Models/Schema/SchemaSnapshot.h"
#include "Presentation/Views/QueryEditor/Autocomplete/CompletionModels.h"

#include <QString>
#include <QStringList>
#include <unordered_map>
#include <vector>

namespace gridex {

// Context-aware SQL autocomplete with FK-based JOIN suggestions, fuzzy
// matching, and smart ranking. Stateless per-call: call updateSchema once
// when the adapter changes, then suggestions() per keystroke.
class AutocompleteProvider {
public:
    // Feed table/column/FK metadata; sorts table names and flattens columns.
    void updateSchema(const std::vector<TableDescription>& tables);

    // Track item text so it ranks higher on future completions.
    void trackUsed(const QString& text);

    // Top-20 suggestions for the given parser output.
    std::vector<CompletionItem> suggestions(const CompletionContext& ctx) const;

    // Exposed so the UI can highlight substring matches using the same
    // scoring used for ranking.
    static std::optional<int> fuzzyScore(const QString& query, const QString& candidate);
    static std::vector<int>   fuzzyMatchRanges(const QString& query, const QString& candidate);

    struct FunctionInfo {
        QString name;
        QString signature;
        QString snippet;
    };
    static const QStringList& sqlKeywords();
    static const std::vector<FunctionInfo>& sqlFunctions();

private:
    std::vector<QString>                              tableNames_;
    std::unordered_map<std::string, std::vector<ColumnInfo>>     columnsByTable_;
    std::unordered_map<std::string, std::vector<ForeignKeyInfo>> foreignKeysByTable_;
    QStringList                                       allColumnNames_;
    std::vector<QString>                              recentlyUsed_;
};

}  // namespace gridex
