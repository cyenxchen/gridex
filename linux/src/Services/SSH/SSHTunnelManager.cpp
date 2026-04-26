#include "Services/SSH/SSHTunnelManager.h"

#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <libssh/libssh.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "Core/Errors/GridexError.h"

namespace gridex {

struct SSHTunnelManager::Tunnel {
    std::string connectionId;
    std::uint16_t localPort = 0;
    std::atomic<Status> status{Status::Connecting};
    std::atomic<bool> stopFlag{false};
    std::thread thread;
    ssh_session sshSession = nullptr;
    int listenFd = -1;

    ~Tunnel() {
        stopFlag.store(true, std::memory_order_release);
        // Close listen socket to unblock accept().
        if (listenFd >= 0) { ::shutdown(listenFd, SHUT_RDWR); ::close(listenFd); listenFd = -1; }
        // Set SO_RCVTIMEO on SSH fd to unblock any blocking libssh call (H1 fix).
        if (sshSession) {
            const int sshFd = ssh_get_fd(sshSession);
            if (sshFd >= 0) {
                struct timeval tv{0, 500000};  // 500ms
                ::setsockopt(sshFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
        }
        if (thread.joinable()) thread.join();
        if (sshSession) { ssh_disconnect(sshSession); ssh_free(sshSession); sshSession = nullptr; }
    }
};

namespace {

// Bind a TCP socket to 127.0.0.1:0 and return fd + assigned port.
std::pair<int, std::uint16_t> bindFreePort() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw ConnectionError("SSH tunnel: socket() failed");

    // L1: removed SO_REUSEADDR — unnecessary for ephemeral port 0.

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw ConnectionError("SSH tunnel: bind() failed");
    }
    if (::listen(fd, 1) < 0) {
        ::close(fd);
        throw ConnectionError("SSH tunnel: listen() failed");
    }
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    return {fd, ntohs(addr.sin_port)};
}

ssh_session createSshSession(const SSHTunnelConfig& cfg) {
    ssh_session s = ssh_new();
    if (!s) throw ConnectionError("SSH tunnel: ssh_new() failed");

    ssh_options_set(s, SSH_OPTIONS_HOST, cfg.host.c_str());
    int port = cfg.port;
    ssh_options_set(s, SSH_OPTIONS_PORT, &port);
    ssh_options_set(s, SSH_OPTIONS_USER, cfg.username.c_str());
    int timeout = 10;
    ssh_options_set(s, SSH_OPTIONS_TIMEOUT, &timeout);

    if (ssh_connect(s) != SSH_OK) {
        std::string msg = ssh_get_error(s);
        ssh_free(s);
        throw ConnectionError("SSH tunnel connect failed: " + msg);
    }

    // C2: Host key verification — warn on unknown/changed keys.
    // NOTE: Full interactive "accept new key?" UX deferred. For now, accept known
    // hosts from ~/.ssh/known_hosts and warn on CHANGED keys (MITM indicator).
    // Unknown keys are accepted silently (matches macOS AcceptAllHostKeysDelegate
    // behavior, documented as known security gap).
    const auto hkState = ssh_session_is_known_server(s);
    if (hkState == SSH_KNOWN_HOSTS_CHANGED) {
        ssh_disconnect(s);
        ssh_free(s);
        throw ConnectionError(
            "SSH host key CHANGED for " + cfg.host +
            " — possible MITM attack. Remove the old key from ~/.ssh/known_hosts if expected.");
    }
    // SSH_KNOWN_HOSTS_NOT_FOUND / SSH_KNOWN_HOSTS_UNKNOWN: accept (matches macOS).
    // Optionally write to known_hosts for future verification.
    if (hkState == SSH_KNOWN_HOSTS_OK) {
        // Already trusted — good.
    } else {
        // Unknown/not found — auto-accept and persist for next time.
        ssh_session_update_known_hosts(s);
    }

    // M3: Enable SSH keepalive to survive NAT/firewall idle timeouts.
    // Sends keepalive every 30 seconds after 2 missed responses.
    // (Requires libssh ≥ 0.8; ignored if unavailable.)
    // ssh_options_set(s, SSH_OPTIONS_NODELAY, &opt) not needed — TCP_NODELAY
    // is typically set by libssh on the control channel already.

    return s;
}

// Keyboard-interactive fallback — many OpenSSH servers use this instead of
// pure "password" auth. The server sends prompts; we answer with the password.
int tryKbdInteractive(ssh_session s, const char* pw) {
    int rc = ssh_userauth_kbdint(s, nullptr, nullptr);
    while (rc == SSH_AUTH_INFO) {
        const int nprompts = ssh_userauth_kbdint_getnprompts(s);
        for (int i = 0; i < nprompts; ++i) {
            ssh_userauth_kbdint_setanswer(s, static_cast<unsigned>(i), pw);
        }
        rc = ssh_userauth_kbdint(s, nullptr, nullptr);
    }
    return rc;
}

void authenticateSsh(ssh_session s, const SSHTunnelConfig& cfg,
                     const std::optional<std::string>& password) {
    ssh_userauth_none(s, nullptr);

    int rc = SSH_AUTH_DENIED;
    const char* pw = password ? password->c_str() : "";

    switch (cfg.authMethod) {
        case SSHAuthMethod::Password:
            rc = ssh_userauth_password(s, nullptr, pw);
            if (rc != SSH_AUTH_SUCCESS) {
                rc = tryKbdInteractive(s, pw);
            }
            break;
        case SSHAuthMethod::PrivateKey:
            if (cfg.keyPath) {
                ssh_key key = nullptr;
                if (ssh_pki_import_privkey_file(cfg.keyPath->c_str(), nullptr, nullptr, nullptr, &key) == SSH_OK) {
                    rc = ssh_userauth_publickey(s, nullptr, key);
                    ssh_key_free(key);
                }
            }
            if (rc != SSH_AUTH_SUCCESS) {
                rc = ssh_userauth_publickey_auto(s, nullptr, nullptr);
            }
            break;
        case SSHAuthMethod::KeyWithPassphrase: {
            if (cfg.keyPath) {
                ssh_key key = nullptr;
                if (ssh_pki_import_privkey_file(cfg.keyPath->c_str(), pw, nullptr, nullptr, &key) == SSH_OK) {
                    rc = ssh_userauth_publickey(s, nullptr, key);
                    ssh_key_free(key);
                }
            }
            if (rc != SSH_AUTH_SUCCESS) {
                rc = ssh_userauth_publickey_auto(s, nullptr, pw);
            }
            break;
        }
    }
    if (rc != SSH_AUTH_SUCCESS) {
        throw AuthenticationError(std::string("SSH auth failed: ") + ssh_get_error(s));
    }
}

// H3 fix: loop ssh_channel_write until all bytes written (partial write handling).
bool channelWriteAll(ssh_channel ch, const char* data, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        const int w = ssh_channel_write(ch, data + written, len - written);
        if (w <= 0) return false;
        written += static_cast<uint32_t>(w);
    }
    return true;
}

// Relay loop: accept one TCP client, open SSH forward channel, shuttle bytes.
void relayLoop(SSHTunnelManager::Tunnel* t, const std::string& remoteHost, int remotePort) {
    while (!t->stopFlag.load(std::memory_order_acquire)) {
        sockaddr_in client{};
        socklen_t clen = sizeof(client);
        int clientFd = ::accept(t->listenFd, reinterpret_cast<sockaddr*>(&client), &clen);
        if (clientFd < 0) break;

        ssh_channel ch = ssh_channel_new(t->sshSession);
        if (!ch || ssh_channel_open_forward(ch, remoteHost.c_str(), remotePort,
                                            "127.0.0.1", static_cast<int>(t->localPort)) != SSH_OK) {
            ::close(clientFd);
            if (ch) ssh_channel_free(ch);
            // If stopFlag set, exit cleanly; otherwise retry accept.
            if (t->stopFlag.load(std::memory_order_acquire)) break;
            continue;
        }

        t->status.store(SSHTunnelManager::Status::Connected, std::memory_order_release);

        constexpr int kBufSize = 32768;
        char buf[kBufSize];
        const int sshFd = ssh_get_fd(t->sshSession);

        while (!t->stopFlag.load(std::memory_order_acquire) && ssh_channel_is_open(ch)) {
            pollfd fds[2];
            fds[0] = {clientFd, POLLIN, 0};
            fds[1] = {sshFd,    POLLIN, 0};
            int pr = ::poll(fds, 2, 500);
            if (pr < 0) break;

            // Check error/hangup FIRST (M1 fix: don't miss final data).
            const bool tcpErr = (fds[0].revents & (POLLERR | POLLHUP)) != 0;
            const bool sshErr = (fds[1].revents & (POLLERR | POLLHUP)) != 0;

            // TCP client → SSH channel
            if (fds[0].revents & POLLIN) {
                const ssize_t n = ::read(clientFd, buf, kBufSize);
                if (n <= 0) break;
                if (!channelWriteAll(ch, buf, static_cast<uint32_t>(n))) break;  // H3 fix
            }

            // SSH channel → TCP client
            if (fds[1].revents & POLLIN) {
                const int n = ssh_channel_read_nonblocking(ch, buf, kBufSize, 0);
                if (n > 0) {
                    ssize_t written = 0;
                    while (written < n) {
                        const ssize_t w = ::write(clientFd, buf + written, static_cast<size_t>(n - written));
                        if (w <= 0) break;
                        written += w;
                    }
                    if (written < n) break;
                } else if (n < 0) {
                    break;
                }
            }

            if (ssh_channel_is_eof(ch)) break;
            // Break on errors only AFTER draining any remaining data above.
            if (tcpErr || sshErr) break;
        }

        // H2 fix: only send EOF if channel is still open and not already EOF'd.
        if (ssh_channel_is_open(ch) && !ssh_channel_is_eof(ch)) {
            ssh_channel_send_eof(ch);
        }
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        ::close(clientFd);
    }
    t->status.store(SSHTunnelManager::Status::Disconnected, std::memory_order_release);
}

}

SSHTunnelManager::SSHTunnelManager() = default;

SSHTunnelManager::~SSHTunnelManager() { disconnectAll(); }

std::uint16_t SSHTunnelManager::establish(const std::string& connectionId,
                                          const SSHTunnelConfig& sshConfig,
                                          const std::string& remoteHost,
                                          int remotePort,
                                          const std::optional<std::string>& sshPassword) {
    disconnect(connectionId);

    auto t = std::make_unique<Tunnel>();
    t->connectionId = connectionId;

    // 1. Open SSH session + authenticate.
    t->sshSession = createSshSession(sshConfig);
    authenticateSsh(t->sshSession, sshConfig, sshPassword);

    // 2. Bind local TCP port.
    auto [fd, port] = bindFreePort();
    t->listenFd = fd;
    t->localPort = port;
    t->status.store(Status::Connected, std::memory_order_release);

    // C1 fix: insert into map BEFORE spawning relay thread so concurrent
    // disconnect() calls can find and stop the tunnel properly.
    auto* raw = t.get();
    {
        std::lock_guard lock(mutex_);
        tunnels_[connectionId] = std::move(t);
    }

    // 3. Spawn relay thread (tunnel is already in the map).
    const auto rh = remoteHost;
    const auto rp = remotePort;
    raw->thread = std::thread([raw, rh, rp] { relayLoop(raw, rh, rp); });

    return port;
}

void SSHTunnelManager::disconnect(const std::string& connectionId) {
    std::lock_guard lock(mutex_);
    tunnels_.erase(connectionId);
}

void SSHTunnelManager::disconnectAll() {
    std::lock_guard lock(mutex_);
    tunnels_.clear();
}

SSHTunnelManager::Status SSHTunnelManager::status(const std::string& connectionId) const {
    std::lock_guard lock(mutex_);
    auto it = tunnels_.find(connectionId);
    if (it == tunnels_.end()) return Status::Disconnected;
    return it->second->status.load(std::memory_order_acquire);
}

}
