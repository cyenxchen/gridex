#include "Data/Keychain/SecretStore.h"

#include <libsecret/secret.h>
#include <stdexcept>
#include <string>
#include <utility>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

// Schema used for all Gridex credentials. Stored attributes are:
//   "service" = logical service name (e.g. com.gridex.credentials)
//   "account" = key name (e.g. db.password.<connection-id>)
const SecretSchema* gridexSchema() {
    static const SecretSchema schema = {
        "com.gridex.Secret",
        SECRET_SCHEMA_NONE,
        {
            { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr,   SECRET_SCHEMA_ATTRIBUTE_STRING },
        },
        0, 0, 0, 0, 0, 0, 0, 0
    };
    return &schema;
}

[[noreturn]] void throwFromGError(const std::string& ctx, GError* err) {
    std::string msg = ctx;
    if (err) {
        msg += ": ";
        msg += err->message ? err->message : "unknown libsecret error";
        g_error_free(err);
    } else {
        msg += ": operation returned failure with no detail (is the Secret Service daemon running?)";
    }
    throw InternalError(msg);
}

}

SecretStore::SecretStore() : serviceName_("com.gridex.credentials") {}

SecretStore::SecretStore(std::string serviceName) : serviceName_(std::move(serviceName)) {}

bool SecretStore::isAvailable() {
    // Probe by doing a harmless lookup on a reserved key. We don't cache across
    // process restarts; a single in-process static would mask a user switching
    // keyring daemons, which is rare but possible.
    GError* err = nullptr;
    gchar* value = secret_password_lookup_sync(
        gridexSchema(), nullptr, &err,
        "service", serviceName_.c_str(),
        "account", "__gridex_probe__",
        nullptr);
    if (err) { g_error_free(err); return false; }
    if (value) g_free(value);
    return true;
}

void SecretStore::save(std::string_view key, std::string_view value) {
    const std::string account(key);
    const std::string password(value);
    const std::string label = "Gridex: " + account;

    GError* err = nullptr;
    const gboolean ok = secret_password_store_sync(
        gridexSchema(),
        SECRET_COLLECTION_DEFAULT,
        label.c_str(),
        password.c_str(),
        nullptr,
        &err,
        "service", serviceName_.c_str(),
        "account", account.c_str(),
        nullptr);
    if (!ok || err) throwFromGError("SecretStore::save", err);
}

std::optional<std::string> SecretStore::load(std::string_view key) {
    const std::string account(key);
    GError* err = nullptr;
    gchar* raw = secret_password_lookup_sync(
        gridexSchema(), nullptr, &err,
        "service", serviceName_.c_str(),
        "account", account.c_str(),
        nullptr);
    if (err) throwFromGError("SecretStore::load", err);
    if (!raw) return std::nullopt;

    std::string out(raw);
    secret_password_free(raw);
    return out;
}

void SecretStore::remove(std::string_view key) {
    const std::string account(key);
    GError* err = nullptr;
    secret_password_clear_sync(
        gridexSchema(), nullptr, &err,
        "service", serviceName_.c_str(),
        "account", account.c_str(),
        nullptr);
    if (err) throwFromGError("SecretStore::remove", err);
}

}
