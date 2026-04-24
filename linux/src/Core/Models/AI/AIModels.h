#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace gridex {

// ---- LLMMessage -------------------------------------------------------

struct LLMMessage {
    enum class Role { User, Assistant, System };

    Role        role;
    std::string content;

    static std::string roleString(Role r) {
        switch (r) {
            case Role::User:      return "user";
            case Role::Assistant: return "assistant";
            case Role::System:    return "system";
        }
        return "user";
    }
};

// ---- LLMModel ---------------------------------------------------------

struct LLMModel {
    std::string id;
    std::string name;
    std::string provider;
    int         contextWindow     = 128000;
    bool        supportsStreaming = true;
};

// ---- ChatMessage ------------------------------------------------------
// Represents a single turn in the UI chat history.

struct ChatMessage {
    std::string                                         id;        // UUID string
    LLMMessage::Role                                    role;
    std::string                                         content;
    std::chrono::system_clock::time_point               timestamp;

    // Factory helpers
    static ChatMessage user(const std::string& text);
    static ChatMessage assistant(const std::string& text);
};

inline ChatMessage ChatMessage::user(const std::string& text) {
    ChatMessage m;
    m.role      = LLMMessage::Role::User;
    m.content   = text;
    m.timestamp = std::chrono::system_clock::now();
    return m;
}

inline ChatMessage ChatMessage::assistant(const std::string& text) {
    ChatMessage m;
    m.role      = LLMMessage::Role::Assistant;
    m.content   = text;
    m.timestamp = std::chrono::system_clock::now();
    return m;
}

}  // namespace gridex
