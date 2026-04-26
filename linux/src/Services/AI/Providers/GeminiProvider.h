#pragma once

#include <string>
#include <vector>

#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

// Google Gemini provider.
// POST https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key=<key>
// Uses native Gemini REST format (not the OpenAI-compatible endpoint).
class GeminiProvider : public ILLMService {
public:
    explicit GeminiProvider(std::string apiKey, std::string baseUrl = {});

    [[nodiscard]] std::string providerName() const override { return "Gemini"; }

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
    std::string baseUrl_;  // default: https://generativelanguage.googleapis.com/v1beta
};

}  // namespace gridex
