#pragma once

#include <string>
#include <vector>

#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

// Local Ollama provider.
// POST http://localhost:11434/api/chat  (stream:false)
// No auth required.
class OllamaProvider : public ILLMService {
public:
    // baseUrl defaults to http://localhost:11434
    explicit OllamaProvider(std::string baseUrl = "http://localhost:11434");

    [[nodiscard]] std::string providerName() const override { return "Ollama"; }

    [[nodiscard]] std::string sendMessage(
        const std::vector<LLMMessage>& messages,
        const std::string&             systemPrompt,
        const std::string&             model,
        int                            maxTokens   = 4096,
        double                         temperature = 0.7) override;

    // Fetches installed models from GET /api/tags; returns empty list on error.
    [[nodiscard]] std::vector<LLMModel> availableModels() override;

    // Returns true when the Ollama daemon is reachable (GET /api/tags, HTTP 200).
    [[nodiscard]] bool validateAPIKey() override;

private:
    std::string baseUrl_;
};

}  // namespace gridex
