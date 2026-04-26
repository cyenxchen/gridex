#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <vector>

namespace gridex {

// Generic SQL syntax highlighter. Covers standard SQL keywords, strings,
// numbers, single-line (--) and block (/* */) comments, and common functions.
// Dialect-specific keywords can be added per-adapter later.
class SqlHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit SqlHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> rules_;

    QTextCharFormat keywordFmt_;
    QTextCharFormat functionFmt_;
    QTextCharFormat stringFmt_;
    QTextCharFormat numberFmt_;
    QTextCharFormat commentFmt_;
    QTextCharFormat typeFmt_;

    QRegularExpression blockCommentStart_;
    QRegularExpression blockCommentEnd_;
};

}
