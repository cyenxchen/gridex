#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace gridex {

enum class SSLMode {
    Preferred,
    Disabled,
    Required,
    VerifyCA,
    VerifyIdentity,
};

inline constexpr std::array<SSLMode, 5> kAllSSLModes = {
    SSLMode::Preferred, SSLMode::Disabled, SSLMode::Required,
    SSLMode::VerifyCA,  SSLMode::VerifyIdentity,
};

inline std::string_view rawValue(SSLMode m) {
    switch (m) {
        case SSLMode::Preferred:      return "PREFERRED";
        case SSLMode::Disabled:       return "DISABLED";
        case SSLMode::Required:       return "REQUIRED";
        case SSLMode::VerifyCA:       return "VERIFY_CA";
        case SSLMode::VerifyIdentity: return "VERIFY_IDENTITY";
    }
    return "";
}

inline std::optional<SSLMode> sslModeFromRaw(std::string_view raw) {
    if (raw == "PREFERRED")       return SSLMode::Preferred;
    if (raw == "DISABLED")        return SSLMode::Disabled;
    if (raw == "REQUIRED")        return SSLMode::Required;
    if (raw == "VERIFY_CA")       return SSLMode::VerifyCA;
    if (raw == "VERIFY_IDENTITY") return SSLMode::VerifyIdentity;
    return std::nullopt;
}

inline std::string_view displayName(SSLMode m) {
    switch (m) {
        case SSLMode::Preferred:      return "PREFERRED";
        case SSLMode::Disabled:       return "DISABLED";
        case SSLMode::Required:       return "REQUIRED";
        case SSLMode::VerifyCA:       return "VERIFY CA";
        case SSLMode::VerifyIdentity: return "VERIFY IDENTITY";
    }
    return "";
}

}
