#pragma once

#include <string>
#include <vector>

#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

// OpenAI GPT provider.
// POST https://api.openai.com/v1/chat/completions
// Auth: Authorization: Bearer <key>
class OpenAIProvider : public ILLMService {
public:
    explicit OpenAIProvider(std::string apiKey, std::string baseUrl = {});

    [[nodiscard]] std::string providerName() const override { return "OpenAI"; }

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
    std::string baseUrl_;
};

}  // namespace gridex
