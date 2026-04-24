#include "Presentation/Views/QueryEditor/Autocomplete/SqlContextParser.h"

#include <QRegularExpression>
#include <QSet>

namespace gridex {

namespace {

struct Token {
    QString value;
    QString upper;
};

// Punctuation tokens that never count as identifiers for keyword-distance.
const QSet<QString>& punctuationTokens() {
    static const QSet<QString> set = {
        ",", "(", ")", ".", ";", "*", "=", ">", "<", "!"
    };
    return set;
}

// Clause-starting SQL keywords. Used for boundary detection when scanning
// backwards from cursor.
const QSet<QString>& clauseKeywords() {
    static const QSet<QString> set = {
        "SELECT", "FROM", "WHERE", "JOIN", "ON", "AND", "OR", "NOT",
        "ORDER", "GROUP", "BY", "HAVING", "LIMIT", "OFFSET", "UNION",
        "INSERT", "INTO", "VALUES", "UPDATE", "SET", "DELETE",
        "LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "NATURAL",
        "CREATE", "ALTER", "DROP", "TABLE", "INDEX", "VIEW",
        "WITH", "AS", "DISTINCT", "ALL", "EXISTS", "IN", "BETWEEN",
        "LIKE", "ILIKE", "IS", "NULL", "CASE", "WHEN", "THEN", "ELSE", "END",
        "RETURNING", "EXPLAIN", "ANALYZE", "BEGIN", "COMMIT", "ROLLBACK"
    };
    return set;
}

bool isJoinModifier(const QString& upper) {
    return upper == "LEFT" || upper == "RIGHT" || upper == "INNER"
        || upper == "OUTER" || upper == "FULL" || upper == "CROSS"
        || upper == "NATURAL";
}

bool isInsideString(const QString& text) {
    bool inSingle = false;
    for (QChar ch : text) if (ch == '\'') inSingle = !inSingle;
    return inSingle;
}

QString isolateCurrentStatement(const QString& text) {
    const int last = text.lastIndexOf(';');
    if (last < 0) return text;
    return text.mid(last + 1);
}

QString currentWord(const QString& text) {
    int start = text.length();
    while (start > 0) {
        const QChar ch = text.at(start - 1);
        if (ch.isLetterOrNumber() || ch == '_' || ch == '.') {
            --start;
        } else {
            break;
        }
    }
    return text.mid(start);
}

std::optional<QString> prefixBeforeDot(const QString& word) {
    const int dot = word.indexOf('.');
    if (dot < 0) return std::nullopt;
    return word.left(dot);
}

std::vector<Token> tokenize(const QString& text) {
    // Strings and comments: matched but skipped.
    // Words: \w+. Punctuation: single-char ops.
    static const QRegularExpression re(
        QStringLiteral(R"((?:'[^']*')|(?:--[^\n]*)|(?:/\*[\s\S]*?\*/)|(?:\w+)|(?:[.,();=<>!*]))"),
        QRegularExpression::UseUnicodePropertiesOption);

    std::vector<Token> tokens;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString v = m.captured(0);
        if (v.startsWith('\'') || v.startsWith("--") || v.startsWith("/*")) continue;
        tokens.push_back({v, v.toUpper()});
    }
    return tokens;
}

// Resolve alias/table-name lookup within the current statement scope.
std::optional<QString> resolveAlias(const QString& needle,
                                    const std::vector<TableRef>& scope) {
    for (const auto& r : scope) {
        if (r.alias.has_value() && *r.alias == needle) return r.name;
    }
    for (const auto& r : scope) {
        if (r.name == needle) return r.name;
    }
    return needle;
}

QString resolveSchemaPrefix(const std::vector<Token>& tokens, int& i) {
    QString name = tokens[i].value;
    ++i;
    if (i < static_cast<int>(tokens.size()) && tokens[i].value == ".") {
        ++i;
        if (i < static_cast<int>(tokens.size())) {
            name = tokens[i].value;
            ++i;
        }
    }
    return name;
}

std::vector<TableRef> extractAllTables(const std::vector<Token>& tokens) {
    std::vector<TableRef> tables;
    int i = 0;
    const int n = static_cast<int>(tokens.size());

    while (i < n) {
        const QString upper = tokens[i].upper;

        if (upper == "FROM" || upper == "JOIN") {
            ++i;
            if (i >= n) break;
            const QString tableName = resolveSchemaPrefix(tokens, i);

            std::optional<QString> alias;
            if (i < n) {
                const QString& next = tokens[i].upper;
                if (next == "AS") {
                    ++i;
                    if (i < n) { alias = tokens[i].value; ++i; }
                } else if (!clauseKeywords().contains(next)
                           && next != "ON" && next != "," && next != "("
                           && next != ")" && next != ";" && next != "*") {
                    alias = tokens[i].value;
                    ++i;
                }
            }
            tables.push_back({tableName, alias});
            continue;
        }

        if (upper == "UPDATE") {
            ++i;
            if (i < n && !clauseKeywords().contains(tokens[i].upper)) {
                const QString tableName = resolveSchemaPrefix(tokens, i);
                std::optional<QString> alias;
                if (i < n && tokens[i].upper == "AS") {
                    ++i;
                    if (i < n) { alias = tokens[i].value; ++i; }
                } else if (i < n && tokens[i].upper == "SET") {
                    // no alias
                } else if (i < n && !clauseKeywords().contains(tokens[i].upper)) {
                    alias = tokens[i].value;
                    ++i;
                }
                tables.push_back({tableName, alias});
                continue;
            }
        }

        ++i;
    }

    return tables;
}

std::optional<QString> findUpdateTable(const std::vector<Token>& tokens) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].upper == "UPDATE" && i + 1 < tokens.size()) {
            return tokens[i + 1].value;
        }
    }
    return std::nullopt;
}

std::optional<QString> findInsertTable(const std::vector<Token>& tokens) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].upper == "INTO" && i + 1 < tokens.size()) {
            return tokens[i + 1].value;
        }
    }
    return std::nullopt;
}

// Detect `INSERT INTO table (` — returns table name if matched.
std::optional<QString> findInsertIntoParenContext(const std::vector<Token>& tokens) {
    const int count = static_cast<int>(tokens.size());
    if (count < 3) return std::nullopt;

    const int beforeParen = count - 2;
    const int beforeTable = count - 3;

    if (beforeTable >= 0 && tokens[beforeTable].upper == "INTO") {
        return tokens[beforeParen].value;
    }
    // INSERT INTO schema.table (
    if (count >= 5) {
        const int dotPos  = count - 3;
        const int intoPos = count - 5;
        if (dotPos >= 0 && tokens[dotPos].value == "."
            && intoPos >= 0 && tokens[intoPos].upper == "INTO") {
            return tokens[beforeParen].value;
        }
    }
    return std::nullopt;
}

QString findCurrentClause(const std::vector<Token>& tokens) {
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        const QString& u = tokens[i].upper;
        if (u == "SELECT" || u == "FROM" || u == "WHERE" || u == "HAVING" || u == "SET") {
            return u;
        }
        if (u == "JOIN") return "JOIN";
        if (u == "ORDER") {
            if (i + 1 < static_cast<int>(tokens.size()) && tokens[i + 1].upper == "BY") {
                return "ORDER BY";
            }
            return u;
        }
        if (u == "GROUP") {
            if (i + 1 < static_cast<int>(tokens.size()) && tokens[i + 1].upper == "BY") {
                return "GROUP BY";
            }
            return u;
        }
        if (u == "INSERT" || u == "INTO") return "INSERT";
    }
    return "";
}

// Find the nearest clause keyword when scanning backwards; also count the
// number of identifier tokens between that keyword and the cursor.
std::optional<std::pair<QString, int>> findPreviousKeywordWithDistance(
        const std::vector<Token>& tokens) {
    int identifierCount = 0;
    for (int i = static_cast<int>(tokens.size()) - 1; i >= 0; --i) {
        const QString& u = tokens[i].upper;
        if (clauseKeywords().contains(u)) {
            return std::make_pair(u, identifierCount);
        }
        if (!punctuationTokens().contains(tokens[i].value)) {
            ++identifierCount;
        }
    }
    return std::nullopt;
}

// Build the actual CompletionTrigger payload from raw tokens + prefix.
CompletionTrigger determineTrigger(const std::vector<Token>& tokens,
                                   const QString& prefix,
                                   const std::vector<TableRef>& scope) {
    CompletionTrigger trig;

    // Dot notation: alias.column
    if (prefix.contains('.')) {
        const auto before = prefixBeforeDot(prefix);
        if (before) {
            trig.kind = CompletionTriggerKind::Column;
            trig.columnTable = resolveAlias(*before, scope);
            return trig;
        }
    }

    if (tokens.empty()) {
        trig.kind = CompletionTriggerKind::General;
        return trig;
    }

    // Strip the current in-progress word from the effective token list so
    // keyword-detection operates on the last committed identifier.
    std::vector<Token> effective = tokens;
    if (!prefix.isEmpty() && !effective.empty()
        && effective.back().value.toLower() == prefix.toLower()) {
        effective.pop_back();
    }

    if (effective.empty()) {
        trig.kind = prefix.isEmpty() ? CompletionTriggerKind::None
                                     : CompletionTriggerKind::General;
        return trig;
    }

    const Token& last = effective.back();
    const QString u = last.upper;

    // === Keyword-immediate triggers ===
    if (u == "JOIN") {
        trig.kind = CompletionTriggerKind::Join;
        trig.joinFromTables = scope;
        return trig;
    }
    if (isJoinModifier(u)) {
        trig.kind = CompletionTriggerKind::Keyword;
        return trig;
    }
    if (u == "FROM" || u == "INTO" || u == "TABLE" || u == "UPDATE") {
        trig.kind = CompletionTriggerKind::Table;
        return trig;
    }
    if (u == "SELECT" || u == "DISTINCT") {
        trig.kind = CompletionTriggerKind::SelectList;
        trig.selectScopeTables = scope;
        return trig;
    }
    if (u == "WHERE" || u == "AND" || u == "OR" || u == "ON"
        || u == "HAVING" || u == "BY") {
        trig.kind = CompletionTriggerKind::Column;
        return trig;
    }
    if (u == "SET") {
        trig.kind = CompletionTriggerKind::Column;
        trig.columnTable = findUpdateTable(effective);
        return trig;
    }
    if (last.value == "*") {
        trig.kind = CompletionTriggerKind::AfterIdentifier;
        trig.afterIdentClause = "SELECT";
        trig.afterIdentScope = scope;
        return trig;
    }

    if (last.value == ",") {
        const QString clause = findCurrentClause(effective);
        if (clause == "SELECT") {
            trig.kind = CompletionTriggerKind::SelectList;
            trig.selectScopeTables = scope;
        } else if (clause == "FROM" || clause == "JOIN") {
            trig.kind = CompletionTriggerKind::Table;
        } else if (clause == "ORDER BY" || clause == "GROUP BY") {
            trig.kind = CompletionTriggerKind::Column;
        } else if (clause == "INSERT") {
            trig.kind = CompletionTriggerKind::Column;
            trig.columnTable = findInsertTable(effective);
        } else {
            trig.kind = CompletionTriggerKind::Column;
        }
        return trig;
    }

    if (last.value == "(") {
        // INSERT INTO t (
        if (auto t = findInsertIntoParenContext(effective)) {
            trig.kind = CompletionTriggerKind::Column;
            trig.columnTable = t;
            return trig;
        }
        // Function args: if previous is a known function name, suggest columns.
        // (We don't have the function list here — delegate to generic Column.)
        trig.kind = CompletionTriggerKind::SelectList;
        trig.selectScopeTables = scope;
        return trig;
    }

    if (last.value == "=" || last.value == ">" || last.value == "<" || last.value == "!") {
        trig.kind = CompletionTriggerKind::Column;
        return trig;
    }

    // === Non-keyword last token (identifier, alias, table, column) ===
    if (prefix.isEmpty()) {
        trig.kind = CompletionTriggerKind::AfterIdentifier;
        trig.afterIdentClause = findCurrentClause(effective);
        trig.afterIdentScope = scope;
        return trig;
    }

    // === Prefix being typed ===
    if (auto kwDist = findPreviousKeywordWithDistance(effective)) {
        const QString kw = kwDist->first;
        const int distance = kwDist->second;

        if (kw == "FROM" || kw == "INTO" || kw == "TABLE") {
            trig.kind = (distance == 0) ? CompletionTriggerKind::Table
                                        : CompletionTriggerKind::Keyword;
            return trig;
        }
        if (kw == "UPDATE") {
            trig.kind = (distance == 0) ? CompletionTriggerKind::Table
                                        : CompletionTriggerKind::Keyword;
            return trig;
        }
        if (kw == "JOIN") {
            if (distance == 0) {
                trig.kind = CompletionTriggerKind::Join;
                trig.joinFromTables = scope;
            } else {
                trig.kind = CompletionTriggerKind::Keyword;
            }
            return trig;
        }
        if (kw == "SELECT" || kw == "DISTINCT") {
            trig.kind = CompletionTriggerKind::SelectList;
            trig.selectScopeTables = scope;
            return trig;
        }
        if (kw == "WHERE" || kw == "AND" || kw == "OR" || kw == "ON"
            || kw == "HAVING" || kw == "BY") {
            if (distance >= 2) {
                trig.kind = CompletionTriggerKind::Keyword;
            } else {
                trig.kind = CompletionTriggerKind::Column;
            }
            return trig;
        }
        if (kw == "SET") {
            if (distance == 0) {
                trig.kind = CompletionTriggerKind::Column;
                trig.columnTable = findUpdateTable(effective);
            } else {
                trig.kind = CompletionTriggerKind::Keyword;
            }
            return trig;
        }
    }

    trig.kind = CompletionTriggerKind::General;
    return trig;
}

}  // namespace

CompletionContext SqlContextParser::parse(const QString& sql, int cursorOffset) const {
    const int safe = std::clamp<int>(cursorOffset, 0, static_cast<int>(sql.size()));
    const QString fullText = sql.left(safe);

    CompletionContext ctx;

    // Cursor inside a string literal → no completions.
    if (isInsideString(fullText)) {
        ctx.trigger.kind = CompletionTriggerKind::None;
        return ctx;
    }

    const QString statement = isolateCurrentStatement(fullText);
    ctx.prefix = currentWord(statement);

    const auto tokens = tokenize(statement);
    ctx.scopeTables = extractAllTables(tokens);
    ctx.trigger = determineTrigger(tokens, ctx.prefix, ctx.scopeTables);
    return ctx;
}

}  // namespace gridex
