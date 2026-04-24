#pragma once

#include <optional>
#include <string_view>

namespace gridex {

enum class SSHAuthMethod {
    Password,
    PrivateKey,
    KeyWithPassphrase,
};

inline std::string_view rawValue(SSHAuthMethod m) {
    switch (m) {
        case SSHAuthMethod::Password:          return "password";
        case SSHAuthMethod::PrivateKey:        return "privateKey";
        case SSHAuthMethod::KeyWithPassphrase: return "keyWithPassphrase";
    }
    return "";
}

inline std::optional<SSHAuthMethod> sshAuthMethodFromRaw(std::string_view raw) {
    if (raw == "password")          return SSHAuthMethod::Password;
    if (raw == "privateKey")        return SSHAuthMethod::PrivateKey;
    if (raw == "keyWithPassphrase") return SSHAuthMethod::KeyWithPassphrase;
    return std::nullopt;
}

}
