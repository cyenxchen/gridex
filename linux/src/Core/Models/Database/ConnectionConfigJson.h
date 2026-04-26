#pragma once

// Minimal hand-rolled JSON serializer/deserializer for ConnectionConfig.
// Kept narrow on purpose — when nlohmann/json lands in Phase 2d+ we can swap
// this for a general-purpose library.

#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Core/Enums/ColorTag.h"
#include "Core/Enums/DatabaseType.h"
#include "Core/Enums/SSHAuthMethod.h"
#include "Core/Errors/GridexError.h"
#include "Core/Models/Database/ConnectionConfig.h"

namespace gridex::json {

inline std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

inline std::string encodeSSHConfig(const SSHTunnelConfig& s) {
    std::string out = "{";
    out += "\"host\":" + escape(s.host) + ",";
    out += "\"port\":" + std::to_string(s.port) + ",";
    out += "\"username\":" + escape(s.username) + ",";
    out += "\"authMethod\":" + escape(rawValue(s.authMethod));
    if (s.keyPath) out += ",\"keyPath\":" + escape(*s.keyPath);
    out += "}";
    return out;
}

inline std::string encode(const ConnectionConfig& c) {
    std::string out = "{";
    out += "\"id\":" + escape(c.id) + ",";
    out += "\"name\":" + escape(c.name) + ",";
    out += "\"databaseType\":" + escape(rawValue(c.databaseType)) + ",";
    out += "\"sslEnabled\":";
    out += c.sslEnabled ? "true" : "false";
    if (c.host)     out += ",\"host\":" + escape(*c.host);
    if (c.port)     out += ",\"port\":" + std::to_string(*c.port);
    if (c.database) out += ",\"database\":" + escape(*c.database);
    if (c.username) out += ",\"username\":" + escape(*c.username);
    if (c.colorTag) out += ",\"colorTag\":" + escape(rawValue(*c.colorTag));
    if (c.group)    out += ",\"group\":" + escape(*c.group);
    if (c.filePath) out += ",\"filePath\":" + escape(*c.filePath);
    if (c.sshConfig) out += ",\"sshConfig\":" + encodeSSHConfig(*c.sshConfig);
    out += "}";
    return out;
}

// ---- Minimal parser ----

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s), i_(0) {}

    void skipWs() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }

    void expect(char c) {
        skipWs();
        if (i_ >= s_.size() || s_[i_] != c) {
            throw SerializationError("JSON: expected '" + std::string(1, c) + "' at pos " + std::to_string(i_));
        }
        ++i_;
    }

    bool peek(char c) {
        skipWs();
        return i_ < s_.size() && s_[i_] == c;
    }

    std::string readString() {
        expect('"');
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c == '\\' && i_ < s_.size()) {
                char e = s_[i_++];
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u': {
                        if (i_ + 4 > s_.size()) throw SerializationError("JSON: bad \\u");
                        const auto hex = std::string(s_.substr(i_, 4));
                        i_ += 4;
                        const auto code = static_cast<int>(std::stoul(hex, nullptr, 16));
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: throw SerializationError("JSON: bad escape");
                }
            } else {
                out.push_back(c);
            }
        }
        expect('"');
        return out;
    }

    bool readBool() {
        skipWs();
        if (s_.compare(i_, 4, "true") == 0)  { i_ += 4; return true; }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return false; }
        throw SerializationError("JSON: expected bool at pos " + std::to_string(i_));
    }

    int readInt() {
        skipWs();
        std::size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        if (start == i_) throw SerializationError("JSON: expected number at pos " + std::to_string(start));
        try {
            return std::stoi(std::string(s_.substr(start, i_ - start)));
        } catch (const std::exception&) {
            throw SerializationError("JSON: integer overflow at pos " + std::to_string(start));
        }
    }

    void skipValue() {
        skipWs();
        if (i_ >= s_.size()) return;
        char c = s_[i_];
        if (c == '"') { (void)readString(); return; }
        if (c == '{') { skipObject(); return; }
        if (c == '[') { skipArray(); return; }
        if (c == 't' || c == 'f') { (void)readBool(); return; }
        if (c == 'n') { if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return; } }
        // number
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) ||
                                      s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' ||
                                      s_[i_] == '+' || s_[i_] == '-')) ++i_;
            return;
        }
        throw SerializationError("JSON: unknown value at pos " + std::to_string(i_));
    }

    void skipObject() {
        expect('{');
        skipWs();
        if (peek('}')) { ++i_; return; }
        while (true) {
            (void)readString();
            expect(':');
            skipValue();
            skipWs();
            if (peek(',')) { ++i_; continue; }
            break;
        }
        expect('}');
    }

    void skipArray() {
        expect('[');
        skipWs();
        if (peek(']')) { ++i_; return; }
        while (true) {
            skipValue();
            skipWs();
            if (peek(',')) { ++i_; continue; }
            break;
        }
        expect(']');
    }

    bool done() { skipWs(); return i_ >= s_.size(); }

private:
    std::string_view s_;
    std::size_t i_;
};

inline SSHTunnelConfig decodeSSHConfig(Parser& p) {
    SSHTunnelConfig s;
    p.expect('{');
    while (true) {
        p.skipWs();
        if (p.peek('}')) break;
        const auto key = p.readString();
        p.expect(':');
        if (key == "host")            s.host = p.readString();
        else if (key == "port")       s.port = p.readInt();
        else if (key == "username")   s.username = p.readString();
        else if (key == "authMethod") {
            const auto raw = p.readString();
            s.authMethod = sshAuthMethodFromRaw(raw).value_or(SSHAuthMethod::Password);
        }
        else if (key == "keyPath")    s.keyPath = p.readString();
        else                          p.skipValue();
        p.skipWs();
        if (p.peek(',')) { p.expect(','); continue; }
        break;
    }
    p.expect('}');
    return s;
}

inline ConnectionConfig decode(std::string_view s) {
    Parser p(s);
    ConnectionConfig c;
    p.expect('{');
    while (true) {
        p.skipWs();
        if (p.peek('}')) break;
        const auto key = p.readString();
        p.expect(':');
        if      (key == "id")            c.id = p.readString();
        else if (key == "name")          c.name = p.readString();
        else if (key == "databaseType") {
            const auto raw = p.readString();
            const auto dt = databaseTypeFromRaw(raw);
            if (!dt) throw SerializationError("Unknown databaseType: " + raw);
            c.databaseType = *dt;
        }
        else if (key == "sslEnabled")    c.sslEnabled = p.readBool();
        else if (key == "host")          c.host = p.readString();
        else if (key == "port")          c.port = p.readInt();
        else if (key == "database")      c.database = p.readString();
        else if (key == "username")      c.username = p.readString();
        else if (key == "colorTag") {
            const auto raw = p.readString();
            c.colorTag = colorTagFromRaw(raw);
        }
        else if (key == "group")         c.group = p.readString();
        else if (key == "filePath")      c.filePath = p.readString();
        else if (key == "sshConfig")     c.sshConfig = decodeSSHConfig(p);
        else                             p.skipValue();
        p.skipWs();
        if (p.peek(',')) { p.expect(','); continue; }
        break;
    }
    p.expect('}');
    return c;
}

}
