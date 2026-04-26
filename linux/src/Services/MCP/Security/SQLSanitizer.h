#pragma once

// Strips comments and single-quoted string literals from SQL so syntactic checks
// (e.g. "contains WHERE?") aren't fooled by payloads in comments or literals.
// NOT for execution — security-side normalizer only.

#include <string>

namespace gridex::mcp {

inline std::string stripCommentsAndStrings(const std::string& sql) {
    std::string out;
    out.reserve(sql.size());
    const std::size_t n = sql.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = sql[i];
        // -- line comment
        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
            while (i < n && sql[i] != '\n') ++i;
            continue;
        }
        // /* block comment */
        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(sql[i] == '*' && sql[i + 1] == '/')) ++i;
            if (i + 1 < n) i += 2;
            continue;
        }
        // 'literal' with '' escape
        if (c == '\'') {
            ++i;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

}  // namespace gridex::mcp
