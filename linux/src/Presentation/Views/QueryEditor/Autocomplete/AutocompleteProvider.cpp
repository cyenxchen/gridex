#include "Presentation/Views/QueryEditor/Autocomplete/AutocompleteProvider.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <unordered_map>

namespace gridex {

namespace {

const QSet<QString>& primaryKeywords() {
    static const QSet<QString> set = {
        "SELECT", "FROM", "WHERE", "JOIN", "LEFT JOIN", "RIGHT JOIN", "INNER JOIN",
        "ORDER BY", "GROUP BY", "INSERT INTO", "UPDATE", "DELETE", "SET",
        "AND", "OR", "ON", "AS", "DISTINCT", "LIMIT", "HAVING",
        "CREATE TABLE", "ALTER TABLE", "DROP TABLE", "WITH"
    };
    return set;
}

int typeRank(CompletionType t) {
    switch (t) {
        case CompletionType::Keyword:  return 0;
        case CompletionType::Table:    return 1;
        case CompletionType::Column:   return 2;
        case CompletionType::Function: return 3;
        case CompletionType::Join:     return 4;
    }
    return 0;
}

QString aliasOrName(const TableRef& r) {
    return r.alias.value_or(r.name);
}

// Pick a short alias (1-3 chars) not already used in the scope.
QString suggestAlias(const QString& table, const std::vector<TableRef>& existing) {
    QSet<QString> used;
    for (const auto& r : existing) used.insert(aliasOrName(r).toLower());
    for (int len = 1; len <= 3; ++len) {
        const QString cand = table.left(len).toLower();
        if (!used.contains(cand)) return cand;
    }
    return table.toLower();
}

}  // namespace

// --------------------------------------------------------------------
// Static keyword / function tables
// --------------------------------------------------------------------

const QStringList& AutocompleteProvider::sqlKeywords() {
    static const QStringList kws = {
        "SELECT", "FROM", "WHERE", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER",
        "FULL", "CROSS", "ON", "AND", "OR", "NOT", "IN", "EXISTS", "BETWEEN",
        "LIKE", "ILIKE", "IS", "NULL", "AS", "ORDER", "BY", "GROUP", "HAVING",
        "LIMIT", "OFFSET", "UNION", "ALL", "DISTINCT", "INSERT", "INTO", "VALUES",
        "UPDATE", "SET", "DELETE", "CREATE", "TABLE", "ALTER", "DROP", "INDEX",
        "VIEW", "IF", "THEN", "ELSE", "END", "CASE", "WHEN", "WITH", "RECURSIVE",
        "RETURNING", "EXPLAIN", "ANALYZE", "BEGIN", "COMMIT", "ROLLBACK",
        "LEFT JOIN", "RIGHT JOIN", "INNER JOIN", "FULL JOIN", "CROSS JOIN",
        "ORDER BY", "GROUP BY", "INSERT INTO", "CREATE TABLE", "ALTER TABLE",
        "DROP TABLE", "NOT NULL", "PRIMARY KEY", "FOREIGN KEY", "REFERENCES"
    };
    return kws;
}

const std::vector<AutocompleteProvider::FunctionInfo>& AutocompleteProvider::sqlFunctions() {
    static const std::vector<FunctionInfo> fns = {
        {"COUNT", "COUNT(expr) -> int", "COUNT()"},
        {"SUM", "SUM(expr) -> numeric", "SUM()"},
        {"AVG", "AVG(expr) -> numeric", "AVG()"},
        {"MIN", "MIN(expr) -> value", "MIN()"},
        {"MAX", "MAX(expr) -> value", "MAX()"},
        {"COALESCE", "COALESCE(val, ...) -> value", "COALESCE(, )"},
        {"NULLIF", "NULLIF(a, b) -> value", "NULLIF(, )"},
        {"CAST", "CAST(expr AS type)", "CAST( AS )"},
        {"CONCAT", "CONCAT(str, ...) -> text", "CONCAT(, )"},
        {"LENGTH", "LENGTH(str) -> int", "LENGTH()"},
        {"UPPER", "UPPER(str) -> text", "UPPER()"},
        {"LOWER", "LOWER(str) -> text", "LOWER()"},
        {"TRIM", "TRIM(str) -> text", "TRIM()"},
        {"SUBSTRING", "SUBSTRING(str, pos, len)", "SUBSTRING(, , )"},
        {"REPLACE", "REPLACE(str, from, to)", "REPLACE(, , )"},
        {"NOW", "NOW() -> timestamp", "NOW()"},
        {"CURRENT_TIMESTAMP", "CURRENT_TIMESTAMP", "CURRENT_TIMESTAMP"},
        {"EXTRACT", "EXTRACT(field FROM source)", "EXTRACT( FROM )"},
        {"DATE_TRUNC", "DATE_TRUNC(field, source)", "DATE_TRUNC(, )"},
        {"ROW_NUMBER", "ROW_NUMBER() OVER (...)", "ROW_NUMBER() OVER ()"},
        {"RANK", "RANK() OVER (...)", "RANK() OVER ()"},
        {"DENSE_RANK", "DENSE_RANK() OVER (...)", "DENSE_RANK() OVER ()"},
        {"LAG", "LAG(expr, offset, default)", "LAG(, 1)"},
        {"LEAD", "LEAD(expr, offset, default)", "LEAD(, 1)"},
        {"STRING_AGG", "STRING_AGG(expr, delimiter)", "STRING_AGG(, ',')"},
        {"ARRAY_AGG", "ARRAY_AGG(expr) -> array", "ARRAY_AGG()"},
        {"JSON_AGG", "JSON_AGG(expr) -> json", "JSON_AGG()"},
        {"JSONB_AGG", "JSONB_AGG(expr) -> jsonb", "JSONB_AGG()"},
        {"ROUND", "ROUND(num, decimals)", "ROUND(, 2)"},
        {"CEIL", "CEIL(num) -> int", "CEIL()"},
        {"FLOOR", "FLOOR(num) -> int", "FLOOR()"},
        {"ABS", "ABS(num) -> num", "ABS()"},
        {"GREATEST", "GREATEST(val, ...) -> value", "GREATEST(, )"},
        {"LEAST", "LEAST(val, ...) -> value", "LEAST(, )"},
        {"EXISTS", "EXISTS(subquery) -> bool", "EXISTS ()"},
        {"IN", "expr IN (values)", "IN ()"},
        {"BETWEEN", "expr BETWEEN a AND b", "BETWEEN  AND "},
        {"LIKE", "expr LIKE pattern", "LIKE '%%'"},
        {"ILIKE", "expr ILIKE pattern", "ILIKE '%%'"},
        {"GEN_RANDOM_UUID", "GEN_RANDOM_UUID() -> uuid", "GEN_RANDOM_UUID()"},
        {"TO_CHAR", "TO_CHAR(val, format)", "TO_CHAR(, '')"},
        {"TO_DATE", "TO_DATE(str, format)", "TO_DATE(, '')"},
    };
    return fns;
}

// --------------------------------------------------------------------
// Schema
// --------------------------------------------------------------------

void AutocompleteProvider::updateSchema(const std::vector<TableDescription>& tables) {
    tableNames_.clear();
    columnsByTable_.clear();
    foreignKeysByTable_.clear();
    allColumnNames_.clear();

    QSet<QString> allCols;
    for (const auto& td : tables) {
        tableNames_.emplace_back(QString::fromStdString(td.name));
        columnsByTable_[td.name] = td.columns;
        foreignKeysByTable_[td.name] = td.foreignKeys;
        for (const auto& c : td.columns) allCols.insert(QString::fromStdString(c.name));
    }
    std::sort(tableNames_.begin(), tableNames_.end());
    allColumnNames_ = allCols.values();
    std::sort(allColumnNames_.begin(), allColumnNames_.end());
}

void AutocompleteProvider::trackUsed(const QString& text) {
    recentlyUsed_.erase(std::remove(recentlyUsed_.begin(), recentlyUsed_.end(), text),
                        recentlyUsed_.end());
    recentlyUsed_.insert(recentlyUsed_.begin(), text);
    if (recentlyUsed_.size() > 50) recentlyUsed_.pop_back();
}

// --------------------------------------------------------------------
// Fuzzy
// --------------------------------------------------------------------

std::optional<int> AutocompleteProvider::fuzzyScore(const QString& query,
                                                    const QString& candidate) {
    if (query.isEmpty()) return 0;
    const QString q = query.toLower();
    const QString c = candidate.toLower();

    if (q == c) return 1000;
    if (c.startsWith(q)) {
        const int lengthPenalty = std::max(0, static_cast<int>(candidate.size() - query.size()));
        return 500 - lengthPenalty;
    }
    // Word-boundary prefix
    const QStringList words = c.split(QRegularExpression(R"([_\. ])"), Qt::SkipEmptyParts);
    for (const auto& w : words) {
        if (w.startsWith(q)) {
            return 200 - std::max(0, static_cast<int>(candidate.size() - query.size()));
        }
    }
    // Subsequence fuzzy
    int qi = 0;
    int score = 0;
    int consecutive = 0;
    int lastMatchIndex = -2;
    for (int ci = 0; ci < c.size(); ++ci) {
        if (qi >= q.size()) break;
        if (c[ci] == q[qi]) {
            ++score;
            if (ci == lastMatchIndex + 1) {
                ++consecutive;
                score += consecutive;
            } else {
                consecutive = 0;
            }
            if (ci > 0) {
                const QChar prev = c[ci - 1];
                if (prev == '_' || prev == '.') score += 3;
            }
            lastMatchIndex = ci;
            ++qi;
        }
    }
    if (qi == q.size()) return score;
    return std::nullopt;
}

std::vector<int> AutocompleteProvider::fuzzyMatchRanges(const QString& query,
                                                        const QString& candidate) {
    std::vector<int> ranges;
    const QString q = query.toLower();
    const QString c = candidate.toLower();
    int qi = 0;
    for (int ci = 0; ci < c.size(); ++ci) {
        if (qi >= q.size()) break;
        if (c[ci] == q[qi]) { ranges.push_back(ci); ++qi; }
    }
    return ranges;
}

// --------------------------------------------------------------------
// Suggestions
// --------------------------------------------------------------------

static std::vector<CompletionItem> keywordSuggestions(const QString& prefix) {
    std::vector<CompletionItem> items;
    for (const auto& kw : AutocompleteProvider::sqlKeywords()) {
        const auto s = AutocompleteProvider::fuzzyScore(prefix, kw);
        if (!s) continue;
        int boost = primaryKeywords().contains(kw) ? 100 : 0;
        items.push_back({kw, CompletionType::Keyword, QString{}, kw, *s + boost,
                         AutocompleteProvider::fuzzyMatchRanges(prefix, kw)});
    }
    return items;
}

static std::vector<CompletionItem> tableSuggestions(const QString& prefix,
                                                    const std::vector<QString>& tableNames,
                                                    const std::unordered_map<std::string, std::vector<ColumnInfo>>& columnsByTable) {
    std::vector<CompletionItem> items;
    for (const auto& name : tableNames) {
        if (!prefix.isEmpty() && !AutocompleteProvider::fuzzyScore(prefix, name)) continue;
        const int score = prefix.isEmpty() ? 0
                        : AutocompleteProvider::fuzzyScore(prefix, name).value_or(0);
        int cols = 0;
        auto it = columnsByTable.find(name.toStdString());
        if (it != columnsByTable.end()) cols = static_cast<int>(it->second.size());
        items.push_back({
            name, CompletionType::Table,
            QString::number(cols) + " cols",
            name, score,
            prefix.isEmpty() ? std::vector<int>{}
                             : AutocompleteProvider::fuzzyMatchRanges(prefix, name)
        });
    }
    return items;
}

static std::vector<CompletionItem> columnSuggestions(
        const std::optional<QString>& table,
        const QString& prefix,
        const std::vector<TableRef>& scope,
        const std::unordered_map<std::string, std::vector<ColumnInfo>>& columnsByTable) {
    std::vector<CompletionItem> items;

    auto pushColumn = [&](const ColumnInfo& col, const QString& qualifier, int baseScore) {
        const QString cname = QString::fromStdString(col.name);
        if (!prefix.isEmpty() && !AutocompleteProvider::fuzzyScore(prefix, cname)) return;
        int score = prefix.isEmpty() ? baseScore
                  : AutocompleteProvider::fuzzyScore(prefix, cname).value_or(0);
        if (col.isPrimaryKey) score += 5;
        const QString detail = QString::fromStdString(col.dataType) + " · " + qualifier;
        items.push_back({
            cname, CompletionType::Column, detail, cname, score,
            prefix.isEmpty() ? std::vector<int>{}
                             : AutocompleteProvider::fuzzyMatchRanges(prefix, cname)
        });
    };

    // Columns from specific table (highest priority)
    if (table) {
        auto it = columnsByTable.find(table->toStdString());
        if (it != columnsByTable.end()) {
            for (const auto& c : it->second) pushColumn(c, *table, 10);
        }
    } else {
        // Scope-wide columns with qualifier = alias or table name
        QSet<QString> scopeNames;
        for (const auto& r : scope) scopeNames.insert(r.name);
        for (const auto& r : scope) {
            auto it = columnsByTable.find(r.name.toStdString());
            if (it == columnsByTable.end()) continue;
            const QString qualifier = aliasOrName(r);
            for (const auto& c : it->second) {
                const QString cname = QString::fromStdString(c.name);
                if (!prefix.isEmpty() && !AutocompleteProvider::fuzzyScore(prefix, cname)) continue;
                int score = prefix.isEmpty() ? 8
                          : AutocompleteProvider::fuzzyScore(prefix, cname).value_or(0);
                if (c.isPrimaryKey) score += 5;
                if (scopeNames.contains(r.name)) score += 3;
                items.push_back({
                    cname, CompletionType::Column,
                    QString::fromStdString(c.dataType) + " · " + qualifier,
                    cname, score,
                    prefix.isEmpty() ? std::vector<int>{}
                                     : AutocompleteProvider::fuzzyMatchRanges(prefix, cname)
                });
            }
        }
    }

    // Dedupe by column name, keeping highest score.
    std::unordered_map<std::string, int> seen;
    std::vector<CompletionItem> deduped;
    for (const auto& it : items) {
        const std::string key = it.text.toLower().toStdString();
        auto existing = seen.find(key);
        if (existing == seen.end() || existing->second < it.score) {
            seen[key] = it.score;
            deduped.push_back(it);
        }
    }
    return deduped;
}

static std::vector<CompletionItem> functionSuggestions(const QString& prefix) {
    std::vector<CompletionItem> items;
    for (const auto& fn : AutocompleteProvider::sqlFunctions()) {
        if (!prefix.isEmpty() && !AutocompleteProvider::fuzzyScore(prefix, fn.name)) continue;
        int score = prefix.isEmpty() ? 0
                  : (AutocompleteProvider::fuzzyScore(prefix, fn.name).value_or(0) - 10);
        items.push_back({
            fn.name, CompletionType::Function, fn.signature, fn.snippet, score,
            prefix.isEmpty() ? std::vector<int>{}
                             : AutocompleteProvider::fuzzyMatchRanges(prefix, fn.name)
        });
    }
    return items;
}

static std::vector<CompletionItem> joinSuggestions(
        const std::vector<TableRef>& fromTables,
        const QString& prefix,
        const std::unordered_map<std::string, std::vector<ForeignKeyInfo>>& fksByTable,
        const std::vector<QString>& tableNames,
        const std::unordered_map<std::string, std::vector<ColumnInfo>>& columnsByTable) {
    std::vector<CompletionItem> items;

    auto tryMatch = [&](const QString& insertText, const QString& needle) -> bool {
        if (prefix.isEmpty()) return true;
        if (AutocompleteProvider::fuzzyScore(prefix, insertText)) return true;
        if (AutocompleteProvider::fuzzyScore(prefix, needle)) return true;
        return false;
    };

    for (const auto& tref : fromTables) {
        const QString alias = aliasOrName(tref);

        // Forward FKs from this table
        auto it = fksByTable.find(tref.name.toStdString());
        if (it != fksByTable.end()) {
            for (const auto& fk : it->second) {
                const QString refTable = QString::fromStdString(fk.referencedTable);
                const QString refAlias = suggestAlias(refTable, fromTables);
                QStringList onParts;
                const std::size_t n = std::min(fk.columns.size(), fk.referencedColumns.size());
                for (std::size_t i = 0; i < n; ++i) {
                    onParts << QString("%1.%2 = %3.%4")
                        .arg(alias, QString::fromStdString(fk.columns[i]),
                             refAlias, QString::fromStdString(fk.referencedColumns[i]));
                }
                const QString onClause = onParts.join(" AND ");
                const QString text = QString("JOIN %1 %2 ON %3").arg(refTable, refAlias, onClause);
                if (!tryMatch(text, refTable)) continue;
                QStringList cols;
                for (const auto& c : fk.columns) cols << QString::fromStdString(c);
                items.push_back({text, CompletionType::Join, "FK: " + cols.join(","),
                                 text, 20, {}});
            }
        }

        // Reverse FKs: other tables referencing this one
        for (const auto& [otherTableStd, fks] : fksByTable) {
            const QString otherTable = QString::fromStdString(otherTableStd);
            if (otherTable == tref.name) continue;
            for (const auto& fk : fks) {
                if (QString::fromStdString(fk.referencedTable) != tref.name) continue;
                const QString refAlias = suggestAlias(otherTable, fromTables);
                QStringList onParts;
                const std::size_t n = std::min(fk.columns.size(), fk.referencedColumns.size());
                for (std::size_t i = 0; i < n; ++i) {
                    onParts << QString("%1.%2 = %3.%4")
                        .arg(alias, QString::fromStdString(fk.referencedColumns[i]),
                             refAlias, QString::fromStdString(fk.columns[i]));
                }
                const QString text = QString("JOIN %1 %2 ON %3")
                    .arg(otherTable, refAlias, onParts.join(" AND "));
                if (!tryMatch(text, otherTable)) continue;
                QStringList cols;
                for (const auto& c : fk.columns) cols << QString::fromStdString(c);
                items.push_back({text, CompletionType::Join,
                                 "FK: " + otherTable + "." + cols.join(","),
                                 text, 18, {}});
            }
        }
    }

    // Fallback: plain JOIN <table>
    if (items.empty()) {
        const auto tables = tableSuggestions(prefix, tableNames, columnsByTable);
        for (const auto& t : tables) {
            items.push_back({
                "JOIN " + t.text, CompletionType::Join, t.detail,
                "JOIN " + t.insertText, t.score, {}
            });
        }
    }
    return items;
}

static std::vector<CompletionItem> selectListSuggestions(
        const QString& prefix,
        const std::vector<TableRef>& scope,
        const std::unordered_map<std::string, std::vector<ColumnInfo>>& columnsByTable) {
    std::vector<CompletionItem> items;

    // Star
    if (prefix.isEmpty() || QString("*").startsWith(prefix)) {
        items.push_back({"*", CompletionType::Keyword, "All columns", "*", 20, {}});
    }
    // Functions
    auto fns = functionSuggestions(prefix);
    for (auto& f : fns) { f.score += 5; items.push_back(std::move(f)); }

    // Scope columns
    auto cols = columnSuggestions(std::nullopt, prefix, scope, columnsByTable);
    items.insert(items.end(), cols.begin(), cols.end());

    // DISTINCT
    auto dscore = AutocompleteProvider::fuzzyScore(prefix, "DISTINCT");
    if (prefix.isEmpty() || dscore) {
        items.push_back({
            "DISTINCT", CompletionType::Keyword, QString{}, "DISTINCT",
            prefix.isEmpty() ? 8 : dscore.value_or(0),
            prefix.isEmpty() ? std::vector<int>{}
                             : AutocompleteProvider::fuzzyMatchRanges(prefix, "DISTINCT")
        });
    }
    return items;
}

static std::vector<CompletionItem> afterIdentifierSuggestions(const QString& clause,
                                                              const QString& prefix) {
    QStringList next;
    if (clause == "SELECT") {
        next = {"FROM"};
    } else if (clause == "FROM") {
        next = {"WHERE", "JOIN", "LEFT JOIN", "RIGHT JOIN", "INNER JOIN",
                "FULL JOIN", "CROSS JOIN", "ORDER BY", "GROUP BY",
                "LIMIT", "OFFSET", "UNION", "RETURNING"};
    } else if (clause == "JOIN") {
        next = {"ON", "WHERE", "JOIN", "LEFT JOIN", "RIGHT JOIN",
                "ORDER BY", "GROUP BY", "LIMIT"};
    } else if (clause == "WHERE") {
        next = {"AND", "OR", "ORDER BY", "GROUP BY", "LIMIT", "UNION", "RETURNING"};
    } else if (clause == "ORDER BY" || clause == "GROUP BY") {
        next = {"LIMIT", "OFFSET", "ASC", "DESC"};
    } else if (clause == "HAVING") {
        next = {"ORDER BY", "LIMIT"};
    } else if (clause == "INSERT") {
        next = {"VALUES", "SELECT", "RETURNING"};
    } else {
        next = {"SELECT", "FROM", "WHERE", "JOIN", "ORDER BY",
                "GROUP BY", "LIMIT", "HAVING", "AND", "OR"};
    }
    std::vector<CompletionItem> items;
    for (const auto& kw : next) {
        auto s = AutocompleteProvider::fuzzyScore(prefix, kw);
        if (!prefix.isEmpty() && !s) continue;
        int score = prefix.isEmpty() ? 15 : s.value_or(0) + 10;
        items.push_back({
            kw, CompletionType::Keyword, QString{}, kw, score,
            prefix.isEmpty() ? std::vector<int>{}
                             : AutocompleteProvider::fuzzyMatchRanges(prefix, kw)
        });
    }
    return items;
}

std::vector<CompletionItem> AutocompleteProvider::suggestions(
        const CompletionContext& ctx) const {
    std::vector<CompletionItem> items;
    const auto& prefix = ctx.prefix;
    const auto& trig = ctx.trigger;

    switch (trig.kind) {
        case CompletionTriggerKind::None:
            return {};
        case CompletionTriggerKind::Keyword:
            items = keywordSuggestions(prefix);
            break;
        case CompletionTriggerKind::Table:
            items = tableSuggestions(prefix, tableNames_, columnsByTable_);
            break;
        case CompletionTriggerKind::Column:
            items = columnSuggestions(trig.columnTable, prefix, ctx.scopeTables,
                                      columnsByTable_);
            break;
        case CompletionTriggerKind::Join:
            items = joinSuggestions(trig.joinFromTables, prefix,
                                    foreignKeysByTable_, tableNames_, columnsByTable_);
            break;
        case CompletionTriggerKind::SelectList:
            items = selectListSuggestions(prefix, trig.selectScopeTables, columnsByTable_);
            break;
        case CompletionTriggerKind::AfterIdentifier:
            items = afterIdentifierSuggestions(trig.afterIdentClause, prefix);
            break;
        case CompletionTriggerKind::Function:
            items = functionSuggestions(prefix);
            break;
        case CompletionTriggerKind::General: {
            if (prefix.isEmpty()) return {};
            auto a = keywordSuggestions(prefix);
            auto b = tableSuggestions(prefix, tableNames_, columnsByTable_);
            auto c = functionSuggestions(prefix);
            items.insert(items.end(), a.begin(), a.end());
            items.insert(items.end(), b.begin(), b.end());
            items.insert(items.end(), c.begin(), c.end());
            // All-columns search
            for (const auto& [tnameStd, cols] : columnsByTable_) {
                const QString tname = QString::fromStdString(tnameStd);
                for (const auto& col : cols) {
                    const QString cname = QString::fromStdString(col.name);
                    auto s = AutocompleteProvider::fuzzyScore(prefix, cname);
                    if (!s) continue;
                    items.push_back({
                        cname, CompletionType::Column,
                        QString::fromStdString(col.dataType) + " · " + tname,
                        cname, *s + (col.isPrimaryKey ? 3 : 0),
                        AutocompleteProvider::fuzzyMatchRanges(prefix, cname)
                    });
                }
            }
            break;
        }
    }

    // Recently-used boost
    for (auto& it : items) {
        auto pos = std::find(recentlyUsed_.begin(), recentlyUsed_.end(), it.text);
        if (pos != recentlyUsed_.end()) {
            const int idx = static_cast<int>(pos - recentlyUsed_.begin());
            it.score += 30 - idx;
        }
    }

    // Dedup by (text lower, type)
    std::unordered_map<std::string, int> seenScore;
    std::vector<CompletionItem> deduped;
    for (const auto& it : items) {
        const std::string key = (it.text.toLower() + "|" +
            QString::number(static_cast<int>(it.type))).toStdString();
        auto ex = seenScore.find(key);
        if (ex == seenScore.end()) {
            seenScore[key] = it.score;
            deduped.push_back(it);
        } else if (it.score > ex->second) {
            ex->second = it.score;
            for (auto& d : deduped) {
                const std::string dkey = (d.text.toLower() + "|" +
                    QString::number(static_cast<int>(d.type))).toStdString();
                if (dkey == key) { d = it; break; }
            }
        }
    }

    // Sort: score desc, then type priority, then alpha
    std::sort(deduped.begin(), deduped.end(),
        [](const CompletionItem& a, const CompletionItem& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.type != b.type) return typeRank(a.type) < typeRank(b.type);
            return a.text < b.text;
        });

    if (deduped.size() > 20) deduped.resize(20);
    return deduped;
}

}  // namespace gridex
