//
// MCPServerHost.cpp
//

#include "MCPServerHost.h"

namespace DBModels { namespace MCPServerHost {

namespace {
    std::mutex g_mtx;
    std::unique_ptr<MCPServer> g_server;
}

MCPServer* ensureCreated(const AppSettings& settings,
                          const std::string& version,
                          MCPTransportMode mode)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_server)
        g_server = std::make_unique<MCPServer>(settings, version, mode);
    return g_server.get();
}

MCPServer* instance()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_server.get();
}

void start()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_server) g_server->start();
}

void stop()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_server)
    {
        g_server->stop();
        g_server.reset();
    }
}

}} // namespace
