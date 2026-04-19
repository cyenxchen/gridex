#pragma once
//
// MCPServerHost.h
// Gridex
//
// Process-wide singleton that owns the MCPServer instance. Created
// lazily at first start(), kept alive until app exit. Lets
// MainWindow, MCPPage, HomePage, and the CLI entry point share the
// same server without passing it through every constructor.
//
// Thread-safe: start/stop take an internal mutex.

#include <memory>
#include <mutex>
#include "MCPServer.h"

namespace DBModels { namespace MCPServerHost {

// Ensures a server exists (creating on demand with the supplied
// settings + mode) and returns a non-owning pointer. Safe to call
// repeatedly — subsequent calls just return the existing instance.
MCPServer* ensureCreated(const AppSettings& settings,
                         const std::string& version,
                         MCPTransportMode mode);

// Returns the shared server, or nullptr if it has never been created.
MCPServer* instance();

// Starts the shared server (idempotent).
void start();

// Stops + destroys the shared server. Called from app shutdown.
void stop();

}} // namespace
