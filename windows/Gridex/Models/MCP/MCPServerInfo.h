#pragma once
//
// MCPServerInfo.h
// Gridex
//
// Server advertisement returned in `initialize` handshake. Mirrors
// MCPServerInfo.gridex() in macos/Core/Models/MCP/MCPProtocol.swift.
// The `protocolVersion` string is dictated by the MCP spec (see
// https://spec.modelcontextprotocol.io) — do NOT bump without
// first validating that Claude Desktop / Cursor / etc still accept
// the new value.

#include <string>
#include <nlohmann/json.hpp>

namespace DBModels
{
    struct MCPServerInfo
    {
        std::string name = "gridex";
        std::string version;
        std::string protocolVersion = "2024-11-05";

        static MCPServerInfo gridex(const std::string& appVersion)
        {
            MCPServerInfo s;
            s.version = appVersion;
            return s;
        }
    };

    // Capabilities advertised in `initialize` result. Matches
    // macOS handleInitialize() — exposes tools + resources +
    // prompts + logging capability flags.
    inline nlohmann::json mcpDefaultCapabilities()
    {
        return {
            {"tools",     {{"listChanged", true}}},
            {"resources", {{"subscribe", true}, {"listChanged", true}}},
            {"prompts",   {{"listChanged", true}}},
            {"logging",   nlohmann::json::object()}
        };
    }
}
