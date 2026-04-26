#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "Services/MCP/Protocol.h"

namespace gridex::mcp {

// Reads line-delimited JSON-RPC 2.0 from stdin, invokes a delegate on each request,
// writes responses to stdout. Blocking read on a dedicated thread.
class StdioTransport {
public:
    using RequestHandler = std::function<void(const JSONRPCRequest&)>;

    StdioTransport();
    ~StdioTransport();

    void setHandler(RequestHandler h);

    void start();
    void stop();

    void send(const JSONRPCResponse& response);
    void sendNotification(const std::string& method, const nlohmann::json& params);

private:
    void readLoop();

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex writeMu_;
    std::mutex handlerMu_;
    RequestHandler handler_;
};

}  // namespace gridex::mcp
