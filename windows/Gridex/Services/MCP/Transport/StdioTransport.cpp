//
// StdioTransport.cpp
//
// Reading:
//   stdin is assumed to carry one JSON object per line (MCP
//   newline-delimited framing). Empty lines are skipped. Malformed
//   JSON triggers a parseError response with id=null.
//
// Writing:
//   A single mutex serializes stdout writes; flush after every
//   line so the client sees responses immediately.
//
// Threading:
//   Reader runs on a dedicated std::thread. The caller's
//   RequestHandler is invoked synchronously from that thread —
//   long-running handlers should dispatch to a worker pool to
//   avoid stalling the read loop.

#include "StdioTransport.h"
#include <iostream>
#include <string>

namespace DBModels
{
    StdioTransport::~StdioTransport()
    {
        stop();
        if (reader_.joinable()) reader_.join();
    }

    void StdioTransport::start()
    {
        if (running_.exchange(true)) return;
        reader_ = std::thread([this]() { readLoop(); });
    }

    void StdioTransport::stop()
    {
        running_.store(false);
    }

    void StdioTransport::readLoop()
    {
        std::string line;
        while (running_.load() && std::getline(std::cin, line))
        {
            if (line.empty()) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            try
            {
                auto j = nlohmann::json::parse(line);
                JSONRPCRequest req;
                from_json(j, req);
                if (handler_) handler_(req);
            }
            catch (const std::exception&)
            {
                // Parse error → reply with null id + -32700 per spec.
                auto resp = JSONRPCResponse::fail(nullptr, JSONRPCError::parseError());
                send(resp);
            }
        }
        running_.store(false);
    }

    void StdioTransport::send(const JSONRPCResponse& response)
    {
        nlohmann::json j;
        to_json(j, response);
        const std::string line = j.dump() + "\n";

        std::lock_guard<std::mutex> lk(writeMtx_);
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        std::cout.flush();
    }

    void StdioTransport::sendNotification(const std::string& method,
                                           const nlohmann::json& params)
    {
        JSONRPCRequest notif;
        notif.method = method;
        notif.params = params;
        // notifications have no `id`.
        nlohmann::json j;
        to_json(j, notif);
        const std::string line = j.dump() + "\n";

        std::lock_guard<std::mutex> lk(writeMtx_);
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        std::cout.flush();
    }
}
