#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace gridex {

class RowValue;

struct JsonString {
    std::string value;
    bool operator==(const JsonString& other) const noexcept = default;
};

struct UuidString {
    std::string value;
    bool operator==(const UuidString& other) const noexcept = default;
};

using Bytes = std::vector<std::uint8_t>;
using Timestamp = std::chrono::system_clock::time_point;

namespace detail {
struct ArrayBox {
    std::vector<RowValue> items;
};
}

class RowValue {
public:
    using Storage = std::variant<
        std::monostate,             // null
        std::string,                // string
        std::int64_t,               // integer
        double,                     // double
        bool,                       // boolean
        Timestamp,                  // date
        Bytes,                      // data
        JsonString,                 // json
        UuidString,                 // uuid
        std::shared_ptr<detail::ArrayBox>  // array (boxed for recursion)
    >;

    RowValue() = default;

    // Factories (keep construction intent explicit)
    static RowValue makeNull()                              { return RowValue{}; }
    static RowValue makeString(std::string v)               { return RowValue{Storage{std::move(v)}}; }
    static RowValue makeInteger(std::int64_t v)             { return RowValue{Storage{v}}; }
    static RowValue makeDouble(double v)                    { return RowValue{Storage{v}}; }
    static RowValue makeBoolean(bool v)                     { return RowValue{Storage{v}}; }
    static RowValue makeDate(Timestamp v)                   { return RowValue{Storage{v}}; }
    static RowValue makeData(Bytes v)                       { return RowValue{Storage{std::move(v)}}; }
    static RowValue makeJson(std::string v)                 { return RowValue{Storage{JsonString{std::move(v)}}}; }
    static RowValue makeUuid(std::string v)                 { return RowValue{Storage{UuidString{std::move(v)}}}; }
    static RowValue makeArray(std::vector<RowValue> items) {
        auto box = std::make_shared<detail::ArrayBox>();
        box->items = std::move(items);
        return RowValue{Storage{std::move(box)}};
    }

    const Storage& storage() const noexcept { return storage_; }

    [[nodiscard]] bool isNull()    const noexcept { return std::holds_alternative<std::monostate>(storage_); }
    [[nodiscard]] bool isString()  const noexcept { return std::holds_alternative<std::string>(storage_); }
    [[nodiscard]] bool isInteger() const noexcept { return std::holds_alternative<std::int64_t>(storage_); }
    [[nodiscard]] bool isDouble()  const noexcept { return std::holds_alternative<double>(storage_); }
    [[nodiscard]] bool isBoolean() const noexcept { return std::holds_alternative<bool>(storage_); }
    [[nodiscard]] bool isDate()    const noexcept { return std::holds_alternative<Timestamp>(storage_); }
    [[nodiscard]] bool isData()    const noexcept { return std::holds_alternative<Bytes>(storage_); }
    [[nodiscard]] bool isJson()    const noexcept { return std::holds_alternative<JsonString>(storage_); }
    [[nodiscard]] bool isUuid()    const noexcept { return std::holds_alternative<UuidString>(storage_); }
    [[nodiscard]] bool isArray()   const noexcept { return std::holds_alternative<std::shared_ptr<detail::ArrayBox>>(storage_); }

    [[nodiscard]] bool isNumeric() const noexcept { return isInteger() || isDouble(); }

    // Typed accessors. Caller must check via is*() first or use try* below.
    [[nodiscard]] const std::string& asString()   const { return std::get<std::string>(storage_); }
    [[nodiscard]] std::int64_t       asInteger()  const { return std::get<std::int64_t>(storage_); }
    [[nodiscard]] double             asDouble()   const { return std::get<double>(storage_); }
    [[nodiscard]] bool               asBoolean()  const { return std::get<bool>(storage_); }
    [[nodiscard]] Timestamp          asDate()     const { return std::get<Timestamp>(storage_); }
    [[nodiscard]] const Bytes&       asData()     const { return std::get<Bytes>(storage_); }
    [[nodiscard]] const std::string& asJson()     const { return std::get<JsonString>(storage_).value; }
    [[nodiscard]] const std::string& asUuid()     const { return std::get<UuidString>(storage_).value; }
    [[nodiscard]] const std::vector<RowValue>& asArray() const {
        return std::get<std::shared_ptr<detail::ArrayBox>>(storage_)->items;
    }

    [[nodiscard]] std::optional<std::string> tryStringValue() const;
    [[nodiscard]] std::optional<std::int64_t> tryIntValue() const;
    [[nodiscard]] std::optional<double> tryDoubleValue() const;

    [[nodiscard]] std::string description() const;
    [[nodiscard]] std::string displayString() const;

    bool operator==(const RowValue& other) const noexcept;
    bool operator!=(const RowValue& other) const noexcept { return !(*this == other); }

private:
    explicit RowValue(Storage s) : storage_(std::move(s)) {}

    Storage storage_;
};

inline std::string formatTimestampUtc(Timestamp ts) {
    using namespace std::chrono;
    const auto tt = system_clock::to_time_t(ts);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// Standard RFC 4648 base64 encoder — small, dep-free, matches Swift's Data.base64EncodedString().
inline std::string encodeBase64(const Bytes& bytes) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    const auto n = bytes.size();
    out.reserve(((n + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= n) {
        const auto b0 = bytes[i++];
        const auto b1 = bytes[i++];
        const auto b2 = bytes[i++];
        out.push_back(kTable[(b0 >> 2) & 0x3F]);
        out.push_back(kTable[((b0 << 4) | (b1 >> 4)) & 0x3F]);
        out.push_back(kTable[((b1 << 2) | (b2 >> 6)) & 0x3F]);
        out.push_back(kTable[b2 & 0x3F]);
    }
    if (i < n) {
        const auto b0 = bytes[i++];
        const auto b1 = (i < n) ? bytes[i++] : static_cast<std::uint8_t>(0);
        out.push_back(kTable[(b0 >> 2) & 0x3F]);
        out.push_back(kTable[((b0 << 4) | (b1 >> 4)) & 0x3F]);
        if (i == n && bytes.size() % 3 == 2) {
            out.push_back(kTable[(b1 << 2) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

inline std::optional<std::string> RowValue::tryStringValue() const {
    if (isNull()) return std::nullopt;
    if (isString()) return asString();
    if (isInteger()) return std::to_string(asInteger());
    if (isDouble())  return std::to_string(asDouble());
    if (isBoolean()) return asBoolean() ? std::string{"true"} : std::string{"false"};
    if (isDate())    return formatTimestampUtc(asDate());
    if (isJson())    return asJson();
    if (isUuid())    return asUuid();
    if (isData())    return encodeBase64(asData());  // Parity with Swift RowValue.stringValue
    if (isArray()) {
        std::string out;
        bool first = true;
        for (const auto& v : asArray()) {
            if (!first) out += ", ";
            out += v.description();
            first = false;
        }
        return out;
    }
    return std::nullopt;
}

inline std::optional<std::int64_t> RowValue::tryIntValue() const {
    if (isInteger()) return asInteger();
    if (isDouble())  return static_cast<std::int64_t>(asDouble());
    if (isBoolean()) return asBoolean() ? 1 : 0;
    if (isString()) {
        try { return static_cast<std::int64_t>(std::stoll(asString())); }
        catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

inline std::optional<double> RowValue::tryDoubleValue() const {
    if (isDouble())  return asDouble();
    if (isInteger()) return static_cast<double>(asInteger());
    if (isString()) {
        try { return std::stod(asString()); }
        catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

inline std::string RowValue::description() const {
    if (auto s = tryStringValue()) return *s;
    return "NULL";
}

inline std::string RowValue::displayString() const {
    if (isNull()) return "NULL";
    if (isData()) {
        const auto& d = asData();
        if (d.size() > 100) return "(BLOB " + std::to_string(d.size()) + " bytes)";
        return encodeBase64(d);  // Parity with Swift displayString
    }
    if (isJson()) {
        const auto& v = asJson();
        if (v.size() > 300) return v.substr(0, 300) + "\xE2\x80\xA6";
        return v;
    }
    if (isString()) {
        const auto& v = asString();
        if (v.size() > 500) return v.substr(0, 500) + "\xE2\x80\xA6";
        return v;
    }
    if (isArray()) {
        const auto& a = asArray();
        std::string out;
        const std::size_t limit = 20;
        std::size_t i = 0;
        for (; i < a.size() && i < limit; ++i) {
            if (i > 0) out += ", ";
            out += a[i].description();
        }
        if (a.size() > limit) {
            out += "\xE2\x80\xA6 (" + std::to_string(a.size()) + " items)";
        }
        return out;
    }
    return description();
}

inline bool RowValue::operator==(const RowValue& other) const noexcept {
    if (storage_.index() != other.storage_.index()) return false;
    if (isArray()) {
        const auto& lhs = asArray();
        const auto& rhs = other.asArray();
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (!(lhs[i] == rhs[i])) return false;
        }
        return true;
    }
    return storage_ == other.storage_;
}

}
