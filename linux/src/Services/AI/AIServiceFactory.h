#pragma once

#include <memory>
#include <string>

#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

// Creates the concrete ILLMService for a given provider name.
// providerName must be one of: "Anthropic", "OpenAI", "Ollama", "Gemini"
// apiKey is ignored for Ollama.
// baseUrl overrides the provider's default API endpoint (empty = default).
// Throws std::invalid_argument for unknown provider names.
class AIServiceFactory {
public:
    [[nodiscard]] static std::unique_ptr<ILLMService>
    createAIService(const std::string& providerName,
                    const std::string& apiKey,
                    const std::string& baseUrl = {});
};

}  // namespace gridex
