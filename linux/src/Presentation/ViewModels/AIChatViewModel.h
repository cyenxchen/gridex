#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "Core/Models/AI/AIModels.h"
#include "Core/Protocols/AI/ILLMService.h"

namespace gridex {

class SecretStore;

// ViewModel for the AI chat panel.
// Owns the conversation history and dispatches sendMessage() calls onto a
// worker QThread to keep the UI responsive (sync HTTP calls block ~1-30s).
class AIChatViewModel : public QObject {
    Q_OBJECT

public:
    explicit AIChatViewModel(SecretStore* secretStore, QObject* parent = nullptr);

    // ---- Accessors ----
    [[nodiscard]] const std::vector<ChatMessage>& messages() const { return messages_; }
    [[nodiscard]] QString  selectedProvider() const { return selectedProvider_; }
    [[nodiscard]] QString  selectedModel()    const { return selectedModel_; }
    [[nodiscard]] bool     isLoading()        const { return isLoading_; }

    // Return hardcoded model list for the given provider (no API round-trip).
    [[nodiscard]] static std::vector<LLMModel> modelsForProvider(const QString& provider);

    // Called from view when user changes provider combo.
    void setProvider(const QString& provider);
    void setModel(const QString& model);

    // Persist / retrieve API key for the current provider via SecretStore.
    void   saveAPIKey(const QString& key);
    QString loadAPIKey() const;

    // Database context injected by WorkspaceView so the system prompt is useful.
    void setDatabaseContext(const QString& dbType, const QString& schema);

    // Clear conversation history.
    void clearHistory();

public slots:
    // Send a user message; runs provider call on background thread.
    void sendMessage(const QString& userText);

signals:
    void messagesChanged();
    void isLoadingChanged(bool loading);
    void errorOccurred(const QString& message);

private:
    void appendMessage(const ChatMessage& msg);
    void setLoading(bool v);
    [[nodiscard]] std::string buildSystemPrompt() const;

    SecretStore* secretStore_ = nullptr;

    std::vector<ChatMessage> messages_;
    QString selectedProvider_ = QStringLiteral("Anthropic");
    QString selectedModel_;
    bool    isLoading_   = false;

    // Database context for system prompt.
    QString dbType_;
    QString dbSchema_;
};

}  // namespace gridex
