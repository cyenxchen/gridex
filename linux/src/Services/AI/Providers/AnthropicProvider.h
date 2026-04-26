#pragma once

#include <string>
#include <vector>

#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

// Anthropic Claude provider.
// POST https://api.anthropic.com/v1/messages
// Auth: x-api-key header + anthropic-version: 2023-06-01
class AnthropicProvider : public ILLMService {
public:
    explicit AnthropicProvider(std::string apiKey, std::string baseUrl = {});

    [[nodiscard]] std::string providerName() const override { return "Anthropic"; }

    [[nodiscard]] std::string sendMessage(
        const std::vector<LLMMessage>& messages,
        const std::string&             systemPrompt,
        const std::string&             model,
        int                            maxTokens   = 4096,
        double                         temperature = 0.7) override;

    [[nodiscard]] std::vector<LLMModel> availableModels() override;

    [[nodiscard]] bool validateAPIKey() override;

private:
    std::string apiKey_;
    std::string baseUrl_;  // empty → default https://api.anthropic.com
};

}  // namespace gridex
