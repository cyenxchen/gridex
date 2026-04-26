#include "Presentation/Views/QueryEditor/SqlHighlighter.h"

#include <QColor>

namespace gridex {

SqlHighlighter::SqlHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    // Keyword format — blue bold.
    keywordFmt_.setForeground(QColor(56, 118, 211));
    keywordFmt_.setFontWeight(QFont::Bold);

    // Function format — purple.
    functionFmt_.setForeground(QColor(136, 57, 195));

    // Type format — teal.
    typeFmt_.setForeground(QColor(0, 128, 128));

    // String format — green.
    stringFmt_.setForeground(QColor(80, 161, 79));

    // Number format — orange.
    numberFmt_.setForeground(QColor(206, 125, 30));

    // Comment format — gray italic.
    commentFmt_.setForeground(QColor(140, 140, 140));
    commentFmt_.setFontItalic(true);

    // SQL keywords (case insensitive).
    const QStringList keywords = {
        "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "IN", "BETWEEN", "LIKE",
        "IS", "NULL", "AS", "ON", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER",
        "FULL", "CROSS", "NATURAL", "USING", "ORDER", "BY", "GROUP", "HAVING",
        "LIMIT", "OFFSET", "UNION", "ALL", "INTERSECT", "EXCEPT", "INSERT",
        "INTO", "VALUES", "UPDATE", "SET", "DELETE", "CREATE", "TABLE", "ALTER",
        "DROP", "INDEX", "VIEW", "DATABASE", "SCHEMA", "IF", "EXISTS", "CASCADE",
        "RESTRICT", "ADD", "COLUMN", "RENAME", "TO", "PRIMARY", "KEY", "FOREIGN",
        "REFERENCES", "UNIQUE", "CHECK", "DEFAULT", "CONSTRAINT", "BEGIN",
        "COMMIT", "ROLLBACK", "TRANSACTION", "SAVEPOINT", "GRANT", "REVOKE",
        "WITH", "RECURSIVE", "CASE", "WHEN", "THEN", "ELSE", "END", "DISTINCT",
        "ASC", "DESC", "NULLS", "FIRST", "LAST", "FETCH", "NEXT", "ROWS",
        "ONLY", "TRUE", "FALSE", "RETURNING", "EXPLAIN", "ANALYZE", "TRUNCATE",
        "TEMP", "TEMPORARY", "REPLACE", "CONFLICT", "DO", "NOTHING",
    };
    for (const auto& kw : keywords) {
        rules_.push_back({
            QRegularExpression("\\b" + kw + "\\b", QRegularExpression::CaseInsensitiveOption),
            keywordFmt_
        });
    }

    // SQL types.
    const QStringList types = {
        "INT", "INTEGER", "BIGINT", "SMALLINT", "TINYINT", "SERIAL", "BIGSERIAL",
        "TEXT", "VARCHAR", "CHAR", "CHARACTER", "BOOLEAN", "BOOL", "FLOAT",
        "DOUBLE", "DECIMAL", "NUMERIC", "REAL", "DATE", "TIME", "TIMESTAMP",
        "TIMESTAMPTZ", "INTERVAL", "UUID", "JSON", "JSONB", "BYTEA", "BLOB",
        "CLOB", "XML", "ARRAY", "MONEY", "BIT", "VARYING",
    };
    for (const auto& t : types) {
        rules_.push_back({
            QRegularExpression("\\b" + t + "\\b", QRegularExpression::CaseInsensitiveOption),
            typeFmt_
        });
    }

    // Common functions.
    const QStringList functions = {
        "COUNT", "SUM", "AVG", "MIN", "MAX", "COALESCE", "NULLIF", "CAST",
        "EXTRACT", "NOW", "CURRENT_TIMESTAMP", "CURRENT_DATE", "CURRENT_TIME",
        "LOWER", "UPPER", "LENGTH", "TRIM", "SUBSTRING", "REPLACE", "CONCAT",
        "ABS", "ROUND", "FLOOR", "CEIL", "CEILING", "RANDOM", "GREATEST",
        "LEAST", "ROW_NUMBER", "RANK", "DENSE_RANK", "LAG", "LEAD", "OVER",
        "PARTITION", "STRING_AGG", "ARRAY_AGG", "JSON_AGG", "JSONB_AGG",
        "FORMAT", "TO_CHAR", "TO_DATE", "TO_TIMESTAMP", "TO_NUMBER",
        "PG_TOTAL_RELATION_SIZE", "VERSION", "DATABASE", "SCHEMA",
    };
    for (const auto& fn : functions) {
        rules_.push_back({
            QRegularExpression("\\b" + fn + "\\s*(?=\\()", QRegularExpression::CaseInsensitiveOption),
            functionFmt_
        });
    }

    // Numbers.
    rules_.push_back({
        QRegularExpression("\\b\\d+\\.?\\d*([eE][+-]?\\d+)?\\b"),
        numberFmt_
    });

    // Single-quoted strings.
    rules_.push_back({
        QRegularExpression("'[^']*'"),
        stringFmt_
    });

    // Double-quoted identifiers (highlight distinctly with string fmt).
    rules_.push_back({
        QRegularExpression("\"[^\"]*\""),
        stringFmt_
    });

    // Single-line comments (-- ...)
    rules_.push_back({
        QRegularExpression("--[^\n]*"),
        commentFmt_
    });

    // Block comments handled specially via highlightBlock state.
    blockCommentStart_ = QRegularExpression("/\\*");
    blockCommentEnd_   = QRegularExpression("\\*/");
}

void SqlHighlighter::highlightBlock(const QString& text) {
    // Apply pattern-based rules first.
    for (const auto& rule : rules_) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(static_cast<int>(match.capturedStart()),
                      static_cast<int>(match.capturedLength()),
                      rule.format);
        }
    }

    // Block comments spanning multiple lines. State encodes whether we're inside /* */.
    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1) {
        auto m = blockCommentStart_.match(text);
        startIndex = m.hasMatch() ? static_cast<int>(m.capturedStart()) : -1;
    }
    while (startIndex >= 0) {
        auto endMatch = blockCommentEnd_.match(text, startIndex + 2);
        int endIndex = endMatch.hasMatch() ? static_cast<int>(endMatch.capturedStart()) : -1;
        int length;
        if (endIndex < 0) {
            setCurrentBlockState(1);
            length = text.length() - startIndex;
        } else {
            length = endIndex - startIndex + static_cast<int>(endMatch.capturedLength());
        }
        setFormat(startIndex, length, commentFmt_);
        auto nextMatch = blockCommentStart_.match(text, startIndex + length);
        startIndex = nextMatch.hasMatch() ? static_cast<int>(nextMatch.capturedStart()) : -1;
    }
}

}
