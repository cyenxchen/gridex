#pragma once

#include <string>
#include <vector>

#include "Core/Models/AI/AIModels.h"

namespace gridex {

// Pure-virtual interface for LLM providers.
// Mirrors macos/Core/Protocols/AI/LLMService.swift.
// Phase 5 uses synchronous (blocking) HTTP calls; streaming via QThread
// is deferred to a later phase.
class ILLMService {
public:
    virtual ~ILLMService() = default;

    // Human-readable provider label ("Anthropic", "OpenAI", etc.)
    [[nodiscard]] virtual std::string providerName() const = 0;

    // Send a conversation and return the full assistant response.
    // Throws gridex::NetworkError on transport failure.
    // Throws gridex::GridexError on API error (bad key, rate limit, etc.).
    [[nodiscard]] virtual std::string sendMessage(
        const std::vector<LLMMessage>& messages,
        const std::string&             systemPrompt,
        const std::string&             model,
        int                            maxTokens   = 4096,
        double                         temperature = 0.7) = 0;

    // Return hardcoded or fetched model list for this provider.
    [[nodiscard]] virtual std::vector<LLMModel> availableModels() = 0;

    // Light validation — returns true when key looks usable.
    // Implementations may make a cheap test request; Ollama checks reachability.
    [[nodiscard]] virtual bool validateAPIKey() = 0;
};

}  // namespace gridex
