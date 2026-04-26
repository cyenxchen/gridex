#include "Services/AI/Providers/OllamaProvider.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <nlohmann/json.hpp>

#include "Core/Errors/GridexError.h"

namespace gridex {

namespace {

// Synchronous GET — returns body on HTTP 200, empty string otherwise.
// Throws NetworkError on transport failure.
QByteArray qtGet(const QString& url, int timeoutMs = 5000) {
    QNetworkAccessManager nam;
    QNetworkRequest       req{QUrl{url}};

    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const std::string msg = reply->errorString().toStdString();
        reply->deleteLater();
        throw NetworkError("Network error: " + msg);
    }
    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return (statusCode == 200) ? data : QByteArray{};
}

QByteArray qtPost(const QString&                     url,
                  const QList<QPair<QString,QString>>& headers,
                  const QByteArray&                   body) {
    QNetworkAccessManager nam;
    QNetworkRequest       req{QUrl{url}};
    for (const auto& [k, v] : headers) req.setRawHeader(k.toUtf8(), v.toUtf8());

    QNetworkReply* reply = nam.post(req, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const std::string msg = reply->errorString().toStdString();
        reply->deleteLater();
        throw NetworkError("Network error: " + msg);
    }
    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (statusCode < 200 || statusCode >= 300) {
        throw GridexError(ErrorCategory::Network,
            "Ollama HTTP " + std::to_string(statusCode) + ": "
            + data.left(200).toStdString());
    }
    return data;
}

}  // namespace

OllamaProvider::OllamaProvider(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
}

std::string OllamaProvider::sendMessage(
    const std::vector<LLMMessage>& messages,
    const std::string&             systemPrompt,
    const std::string&             model,
    int                            /*maxTokens*/,
    double                         /*temperature*/)
{
    using json = nlohmann::json;

    json msgs = json::array();
    if (!systemPrompt.empty())
        msgs.push_back({{"role", "system"}, {"content", systemPrompt}});
    for (const auto& m : messages)
        msgs.push_back({{"role", LLMMessage::roleString(m.role)}, {"content", m.content}});

    json body;
    body["model"]    = model;
    body["messages"] = msgs;
    body["stream"]   = false;

    const QByteArray raw = qtPost(
        QString::fromStdString(baseUrl_ + "/api/chat"),
        {{"Content-Type", "application/json"}},
        QByteArray::fromStdString(body.dump()));

    const auto resp = json::parse(raw.toStdString());
    if (resp.contains("message") && resp["message"].contains("content"))
        return resp["message"]["content"].get<std::string>();

    throw GridexError(ErrorCategory::Internal,
        "Ollama: unexpected response: " + raw.left(200).toStdString());
}

std::vector<LLMModel> OllamaProvider::availableModels() {
    using json = nlohmann::json;
    try {
        const QByteArray raw = qtGet(QString::fromStdString(baseUrl_ + "/api/tags"));
        if (raw.isEmpty()) return {};
        const auto resp = json::parse(raw.toStdString());
        if (!resp.contains("models") || !resp["models"].is_array()) return {};
        std::vector<LLMModel> models;
        for (const auto& m : resp["models"]) {
            if (!m.contains("name")) continue;
            const std::string id = m["name"].get<std::string>();
            models.push_back({id, id, "Ollama", 128000, false});
        }
        return models;
    } catch (...) {
        return {};
    }
}

bool OllamaProvider::validateAPIKey() {
    try {
        return !qtGet(QString::fromStdString(baseUrl_ + "/api/tags")).isEmpty();
    } catch (...) {
        return false;
    }
}

}  // namespace gridex
