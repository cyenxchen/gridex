#include "Presentation/ViewModels/AIChatViewModel.h"

#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include "Core/Errors/GridexError.h"
#include "Data/Keychain/SecretStore.h"
#include "Services/AI/AIServiceFactory.h"

namespace gridex {

AIChatViewModel::AIChatViewModel(SecretStore* secretStore, QObject* parent)
    : QObject(parent), secretStore_(secretStore) {
    // Read persisted default provider/model from the Settings dialog.
    QSettings s;
    const QString savedProvider = s.value("ai/provider").toString();
    if (!savedProvider.isEmpty()) selectedProvider_ = savedProvider;

    const QString savedModel = s.value("ai/model").toString();
    if (!savedModel.isEmpty()) {
        selectedModel_ = savedModel;
        if (savedModel.contains(' ') || savedModel.contains(QRegularExpression("[A-Z]"))) {
            QString normalized = savedModel.toLower();
            normalized.replace(' ', '-');
            selectedModel_ = normalized;
        }
    }
}

// ---- Static helpers ---------------------------------------------------

std::vector<LLMModel> AIChatViewModel::modelsForProvider(const QString& provider) {
    const std::string key = provider.toStdString();
    // Use factory's providers to get their static model lists without an API key.
    try {
        auto svc = AIServiceFactory::createAIService(key, "");
        return svc->availableModels();
    } catch (...) {
        return {};
    }
}

// ---- Mutators ---------------------------------------------------------

void AIChatViewModel::setProvider(const QString& provider) {
    if (provider == selectedProvider_) return;
    selectedProvider_ = provider;
    const auto models = modelsForProvider(provider);
    selectedModel_ = models.empty()
        ? QString{}
        : QString::fromStdString(models.front().id);
}

void AIChatViewModel::setModel(const QString& model) {
    selectedModel_ = model;
}

void AIChatViewModel::saveAPIKey(const QString& key) {
    if (!secretStore_) return;
    try {
        secretStore_->saveAPIKey(selectedProvider_.toStdString(), key.toStdString());
    } catch (const GridexError& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
}

QString AIChatViewModel::loadAPIKey() const {
    if (!secretStore_) return {};
    try {
        const auto val = secretStore_->loadAPIKey(selectedProvider_.toStdString());
        return val ? QString::fromStdString(*val) : QString{};
    } catch (...) {
        return {};
    }
}

void AIChatViewModel::setDatabaseContext(const QString& dbType, const QString& schema) {
    dbType_   = dbType;
    dbSchema_ = schema;
}

void AIChatViewModel::clearHistory() {
    messages_.clear();
    emit messagesChanged();
}

// ---- Core send logic --------------------------------------------------

void AIChatViewModel::sendMessage(const QString& userText) {
    if (userText.trimmed().isEmpty() || isLoading_) return;

    const ChatMessage userMsg = ChatMessage::user(userText.toStdString());
    appendMessage(userMsg);
    setLoading(true);

    // Snapshot state for the background thread — avoid capturing this.
    const std::string provider  = selectedProvider_.toStdString();
    const std::string model     = selectedModel_.toStdString();
    const std::string apiKey    = loadAPIKey().toStdString();
    const std::string sysPrompt = buildSystemPrompt();
    QSettings qs;
    const std::string baseUrl = qs.value(
        QStringLiteral("ai/endpoint/") + selectedProvider_).toString().toStdString();

    // Build LLMMessage history from current messages_ (excluding the one we
    // just appended — it's included below).
    std::vector<LLMMessage> history;
    history.reserve(messages_.size());
    for (const auto& m : messages_) {
        history.push_back({m.role, m.content});
    }

    struct AIResult { std::string text; std::string error; };
    auto* watcher = new QFutureWatcher<AIResult>(this);

    connect(watcher, &QFutureWatcher<AIResult>::finished, this,
        [this, watcher]() {
            setLoading(false);
            const AIResult r = watcher->result();
            if (!r.error.empty()) {
                emit errorOccurred(QString::fromStdString(r.error));
            } else {
                appendMessage(ChatMessage::assistant(r.text));
            }
            watcher->deleteLater();
        });

    const QFuture<AIResult> future = QtConcurrent::run(
        [provider, model, apiKey, baseUrl, sysPrompt, history]() -> AIResult {
            try {
                auto svc = AIServiceFactory::createAIService(provider, apiKey, baseUrl);
                if (!svc) return {"", "Unknown provider: " + provider};
                return {svc->sendMessage(history, sysPrompt, model), ""};
            } catch (const std::exception& e) {
                return {"", std::string(e.what())};
            } catch (...) {
                return {"", "Unknown error contacting AI provider."};
            }
        });

    watcher->setFuture(future);
}

// ---- Private helpers --------------------------------------------------

void AIChatViewModel::appendMessage(const ChatMessage& msg) {
    messages_.push_back(msg);
    emit messagesChanged();
}

void AIChatViewModel::setLoading(bool v) {
    if (isLoading_ == v) return;
    isLoading_ = v;
    emit isLoadingChanged(v);
}

std::string AIChatViewModel::buildSystemPrompt() const {
    std::string prompt =
        "You are a database assistant for Gridex, an AI-native database IDE.";
    if (!dbType_.isEmpty()) {
        prompt += " The connected database type is " + dbType_.toStdString() + ".";
    }
    if (!dbSchema_.isEmpty()) {
        prompt += " Current schema context:\n" + dbSchema_.toStdString();
    }
    prompt += "\nHelp the user write SQL queries. "
              "When suggesting SQL, wrap it in ```sql code blocks.";
    return prompt;
}

}  // namespace gridex
