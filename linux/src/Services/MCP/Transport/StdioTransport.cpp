#include "Services/MCP/Transport/StdioTransport.h"

#include <iostream>
#include <string>

namespace gridex::mcp {

StdioTransport::StdioTransport() = default;
StdioTransport::~StdioTransport() { stop(); }

void StdioTransport::setHandler(RequestHandler h) {
    std::lock_guard lk(handlerMu_);
    handler_ = std::move(h);
}

void StdioTransport::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    thread_ = std::thread(&StdioTransport::readLoop, this);
}

void StdioTransport::stop() {
    if (!running_.exchange(false)) return;
    // Note: std::cin is blocking on readLoop; we can't forcibly wake it. The
    // thread will exit when stdin closes (client disconnect). We detach so shutdown
    // isn't blocked. For an in-GUI lifecycle this is fine because the stdio server
    // only runs in --mcp-stdio mode where the process exits anyway.
    if (thread_.joinable()) thread_.detach();
}

void StdioTransport::readLoop() {
    std::string line;
    while (running_.load() && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            auto req = JSONRPCRequest::fromJson(j);
            RequestHandler h;
            {
                std::lock_guard lk(handlerMu_);
                h = handler_;
            }
            if (h) h(req);
        } catch (const std::exception&) {
            auto resp = JSONRPCResponse::err(nullptr, JSONRPCError::parseError());
            send(resp);
        }
    }
    running_ = false;
}

void StdioTransport::send(const JSONRPCResponse& response) {
    std::lock_guard lk(writeMu_);
    std::cout << response.toJson().dump() << '\n';
    std::cout.flush();
}

void StdioTransport::sendNotification(const std::string& method, const nlohmann::json& params) {
    std::lock_guard lk(writeMu_);
    nlohmann::json n = {{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null()) n["params"] = params;
    std::cout << n.dump() << '\n';
    std::cout.flush();
}

}  // namespace gridex::mcp
