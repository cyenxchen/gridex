#pragma once

#include <string>
#include <vector>

namespace gridex {

// Splits a SQL buffer into individual statements on ";" while respecting
// single-quoted strings and -- / /* */ comments. Returns non-empty trimmed
// statements. Dollar-quoting ($$ ... $$) and DELIMITER rewriting are not
// supported; use native backup/restore for complex dumps.
inline std::vector<std::string> splitSqlStatements(const std::string& buf) {
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(256);
    bool inSingle = false, inLineComment = false, inBlockComment = false;

    for (std::size_t i = 0; i < buf.size(); ++i) {
        const char c    = buf[i];
        const char next = (i + 1 < buf.size()) ? buf[i + 1] : '\0';

        if (inLineComment) {
            cur.push_back(c);
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment) {
            cur.push_back(c);
            if (c == '*' && next == '/') { cur.push_back(next); ++i; inBlockComment = false; }
            continue;
        }
        if (inSingle) {
            cur.push_back(c);
            if (c == '\'') {
                if (next == '\'') { cur.push_back(next); ++i; }
                else inSingle = false;
            }
            continue;
        }

        if (c == '-' && next == '-') { cur.push_back(c); cur.push_back(next); ++i; inLineComment = true;  continue; }
        if (c == '/' && next == '*') { cur.push_back(c); cur.push_back(next); ++i; inBlockComment = true; continue; }
        if (c == '\'') { cur.push_back(c); inSingle = true; continue; }

        if (c == ';') {
            auto ws = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
            while (!cur.empty() && ws(cur.front())) cur.erase(cur.begin());
            while (!cur.empty() && ws(cur.back()))  cur.pop_back();
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }

    auto ws = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!cur.empty() && ws(cur.front())) cur.erase(cur.begin());
    while (!cur.empty() && ws(cur.back()))  cur.pop_back();
    if (!cur.empty()) out.push_back(std::move(cur));

    return out;
}

// Heuristic dialect hint shown read-only in the import wizard.
inline std::string detectSqlDialectHint(const std::string& buf) {
    const std::size_t probe = std::min(buf.size(), std::size_t{4096});
    const std::string sample(buf.data(), probe);

    bool hasBacktick   = sample.find('`')           != std::string::npos;
    bool hasDollarQuot = sample.find("$$")          != std::string::npos;
    bool hasAutoIncr   = sample.find("AUTO_INCREMENT") != std::string::npos ||
                         sample.find("auto_increment") != std::string::npos;

    if (hasBacktick || hasAutoIncr) return "MySQL / MariaDB";
    if (hasDollarQuot)              return "PostgreSQL";
    return "Generic SQL";
}

}  // namespace gridex
