#include "Services/Export/ExportService.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include "Core/Models/Database/RowValue.h"
#include "Core/Models/Query/QueryResult.h"

namespace gridex {

namespace {

// RFC 4180: wrap field in double-quotes and escape inner double-quotes as "".
std::string csvEscape(const std::string& field) {
    bool needsQuoting = false;
    for (char c : field) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needsQuoting = true;
            break;
        }
    }
    if (!needsQuoting) return field;

    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');  // double the quote
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// Minimal JSON string encoder: escapes control chars, backslash, double-quote.
std::string jsonEscapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Unicode escape for other control characters
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

// SQL single-quote escape: double any single-quote inside the literal.
std::string sqlQuoteValue(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string rowValueToString(const RowValue& v) {
    if (v.isNull()) return "";
    if (const auto s = v.tryStringValue()) return *s;
    return "";
}

// Determine if a RowValue should be written as a bare number in SQL.
bool isNumericValue(const RowValue& v) {
    return v.isInteger() || v.isDouble();
}

bool isBoolValue(const RowValue& v) {
    return v.isBoolean();
}

void openFile(std::ofstream& f, const std::string& path) {
    f.open(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error("ExportService: cannot open file for writing: " + path);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// CSV (RFC 4180)
// ---------------------------------------------------------------------------

void ExportService::exportToCsv(const QueryResult& result,
                                 const std::string& filePath) {
    std::ofstream f;
    openFile(f, filePath);

    // Header row
    for (std::size_t c = 0; c < result.columns.size(); ++c) {
        if (c > 0) f << ',';
        f << csvEscape(result.columns[c].name);
    }
    f << "\r\n";

    // Data rows
    for (const auto& row : result.rows) {
        for (std::size_t c = 0; c < result.columns.size(); ++c) {
            if (c > 0) f << ',';
            const std::string val = (c < row.size())
                ? rowValueToString(row[c]) : "";
            f << csvEscape(val);
        }
        f << "\r\n";
    }

    f.flush();
    if (f.fail()) {
        throw std::runtime_error("ExportService: write error on CSV file: " + filePath);
    }
}

// ---------------------------------------------------------------------------
// JSON  —  array of objects
// ---------------------------------------------------------------------------

void ExportService::exportToJson(const QueryResult& result,
                                  const std::string& filePath) {
    std::ofstream f;
    openFile(f, filePath);

    f << "[\n";
    for (std::size_t r = 0; r < result.rows.size(); ++r) {
        const auto& row = result.rows[r];
        f << "  {";
        for (std::size_t c = 0; c < result.columns.size(); ++c) {
            if (c > 0) f << ", ";
            f << jsonEscapeString(result.columns[c].name) << ": ";

            if (c >= row.size() || row[c].isNull()) {
                f << "null";
            } else if (isNumericValue(row[c])) {
                f << rowValueToString(row[c]);
            } else if (isBoolValue(row[c])) {
                f << (row[c].asBoolean() ? "true" : "false");
            } else {
                // Raw JSON values are emitted without extra quoting.
                if (row[c].isJson()) {
                    f << row[c].asJson();
                } else {
                    f << jsonEscapeString(rowValueToString(row[c]));
                }
            }
        }
        f << "}";
        if (r + 1 < result.rows.size()) f << ",";
        f << "\n";
    }
    f << "]\n";

    f.flush();
    if (f.fail()) {
        throw std::runtime_error("ExportService: write error on JSON file: " + filePath);
    }
}

// ---------------------------------------------------------------------------
// SQL  —  INSERT INTO tableName (col1, col2, ...) VALUES (...), (...);
// ---------------------------------------------------------------------------

void ExportService::exportToSql(const QueryResult& result,
                                 const std::string& tableName,
                                 const std::string& filePath) {
    if (result.rows.empty()) {
        // Write an empty file — no rows to export.
        std::ofstream f;
        openFile(f, filePath);
        return;
    }

    std::ofstream f;
    openFile(f, filePath);

    // Build column list once
    std::string colList;
    for (std::size_t c = 0; c < result.columns.size(); ++c) {
        if (c > 0) colList += ", ";
        colList += result.columns[c].name;
    }

    // Emit one INSERT per row (portable; avoids multi-row INSERT size limits).
    const std::string prefix =
        "INSERT INTO " + tableName + " (" + colList + ") VALUES (";

    for (const auto& row : result.rows) {
        f << prefix;
        for (std::size_t c = 0; c < result.columns.size(); ++c) {
            if (c > 0) f << ", ";
            if (c >= row.size() || row[c].isNull()) {
                f << "NULL";
            } else if (isNumericValue(row[c])) {
                f << rowValueToString(row[c]);
            } else if (isBoolValue(row[c])) {
                f << (row[c].asBoolean() ? "1" : "0");
            } else {
                f << sqlQuoteValue(rowValueToString(row[c]));
            }
        }
        f << ");\n";
    }

    f.flush();
    if (f.fail()) {
        throw std::runtime_error("ExportService: write error on SQL file: " + filePath);
    }
}

}  // namespace gridex
