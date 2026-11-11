#include "aifeedback.h"
#include "aifeedbackformat.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
namespace h2d {
namespace {
constexpr qsizetype MaxResponseBytes = 2 * 1024 * 1024;
constexpr qsizetype MaxRequestBytes = 32 * 1024 * 1024;
constexpr qsizetype MaxImages = 16;
QString tr(const char *message) { return QCoreApplication::translate("h2d", message); }
QUrl endpoint(const AiConnectionSettings &settings) {
    auto url = QUrl(settings.baseUrl.trimmed(), QUrl::StrictMode);
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    const QString suffix = settings.protocol == "responses" ? "/responses" :
                           settings.protocol == "anthropic-messages" ? "/messages" : "/chat/completions";
    if (!path.endsWith(suffix)) {
        if (path.isEmpty() && (url.host().compare("api.openai.com", Qt::CaseInsensitive) == 0 ||
                              url.host().compare("api.anthropic.com", Qt::CaseInsensitive) == 0))
            path = "/v1";
        path += suffix;
    }
    url.setPath(path);
    return url;
}
QString safeMessage(QString message, const QString &key) {
    if (!key.isEmpty()) message.replace(key, "[REDACTED]");
    message.replace(QRegularExpression("(?:sk-|key-)[A-Za-z0-9_-]{8,}"), "[REDACTED]");
    message.replace(QRegularExpression("data:image/[^\\s\"]+"), "[image]");
    return message.left(700);
}
QString textFromResponse(const QJsonObject &response, const QString &protocol, QString *error) {
    QStringList texts;
    if (protocol == "responses") {
        if (response["status"].toString() != "completed") {
            *error = tr("回复未完成，请重试或分批解析。"); return {};
        }
        for (const auto &value : response["output"].toArray()) {
            const auto output = value.toObject();
            for (const auto &partValue : output["content"].toArray()) {
                const auto part = partValue.toObject();
                if (part["type"] == "refusal") { *error = tr("模型拒绝了本次请求。"); return {}; }
                if (part["type"] == "output_text") texts << part["text"].toString();
            }
        }
    } else if (protocol == "anthropic-messages") {
        const auto reason = response["stop_reason"].toString();
        if (reason != "end_turn" && reason != "stop_sequence") {
            *error = reason == "refusal" ? tr("模型拒绝了本次请求。") : tr("回复未完成，请重试或分批解析。"); return {};
        }
        for (const auto &value : response["content"].toArray()) {
            const auto part = value.toObject();
            if (part["type"] == "text") texts << part["text"].toString();
        }
    } else {
        const auto choices = response["choices"].toArray();
        if (choices.isEmpty()) { *error = tr("API 没有返回模型回复。"); return {}; }
        const auto choice = choices.first().toObject();
        if (choice["finish_reason"].toString() != "stop") {
            *error = tr("回复未完成，请重试或分批解析。"); return {};
        }
        const auto message = choice["message"].toObject();
        if (!message["refusal"].toString().isEmpty()) { *error = tr("模型拒绝了本次请求。"); return {}; }
        if (message["content"].isString()) texts << message["content"].toString();
        else for (const auto &value : message["content"].toArray()) {
            const auto part = value.toObject();
            if (part["type"] == "text") texts << part["text"].toString();
        }
    }
    const auto text = texts.join('\n').trimmed();
    if (text.isEmpty()) *error = tr("模型返回了空回复。");
    return text;
}
} // namespace

AiFeedbackClient::AiFeedbackClient(QObject *parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!reply_) return;
        terminalError_ = tr("请求超时，请重试或调整超时时间。");
        reply_->abort();
    });
}
AiFeedbackClient::~AiFeedbackClient() {
    timeout_.stop();
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
    }
    key_.fill(QChar('\0'));
}
void AiFeedbackClient::generate(const AiConnectionSettings &settings, const QString &key, const AiFeedbackInput &input,
                                const QString &systemPrompt) {
    start(settings, key, input, false, systemPrompt);
}
void AiFeedbackClient::testConnection(const AiConnectionSettings &settings, const QString &key) {
    AiFeedbackInput input;
    QImage image(128, 128, QImage::Format_RGB32); image.fill(QColor(38, 106, 218));
    input.images.append(image); input.imageLabels.append("Connection test image; no user screenshot.");
    start(settings, key, input, true);
}
void AiFeedbackClient::start(const AiConnectionSettings &settings, const QString &key, const AiFeedbackInput &input, bool test,
                             const QString &systemPrompt) {
    if (isRunning()) { emit failed(tr("已有请求正在进行，请等待或取消。")); return; }
    const auto validation = validateAiConnection(settings);
    if (!validation.isEmpty()) { emit failed(validation); return; }
    if (key.trimmed().isEmpty() || key.contains(QRegularExpression("[\\x{0000}-\\x{0020}\\x{007f}]"))) {
        emit failed(tr("请填写有效的 API Key。")); return;
    }
    if (input.images.isEmpty()) { emit failed(tr("没有可用于理解批注的截图。")); return; }
    if (input.images.size() > MaxImages) { emit failed(tr("一次可解析 16 张图片，请分批处理。")); return; }
    if (!input.imageLabels.isEmpty() && input.imageLabels.size() != input.images.size()) {
        emit failed(tr("截图与图像说明数量不一致。")); return;
    }
    facts_ = {};
    QString error;
    if (!test && !aiCollectFacts(input.feedback, &facts_, &error)) { emit failed(error); return; }
    QString prompt = test ? "Identify the dominant color in the attached image. Reply with one English color name only."
                          : systemPrompt.trimmed().isEmpty() ? aiSystemPrompt() : systemPrompt;
    if (prompt.isEmpty()) { emit failed(tr("系统提示词为空，无法发送请求。")); return; }
    auto metadata = aiFeedbackData(input.feedback).toObject();
    metadata.remove("objects");
    // Frame-local facts were flattened above. Keep only frame metadata here so
    // video root indices and nested objects are not presented twice.
    if (metadata.contains("frames")) {
        QJsonArray frames;
        for (const auto &value : metadata["frames"].toArray()) {
            auto frame = value.toObject(); auto feedback = frame["feedback"].toObject();
            feedback.remove("objects"); frame["feedback"] = feedback; frames.append(frame);
        }
        metadata["frames"] = frames;
    }
    const QString userText = test ? "What is the dominant color of this image? Reply with one English color name only." :
        QString::fromUtf8(QJsonDocument(QJsonObject{{"feedbackContext", metadata}, {"itemsToInterpret", facts_},
                                                   {"additionalUserPreferences", settings.extraInstructions}}).toJson(QJsonDocument::Compact));
    QJsonArray content;
    const bool responses = settings.protocol == "responses";
    const bool anthropic = settings.protocol == "anthropic-messages";
    content.append(QJsonObject{{"type", responses ? "input_text" : "text"}, {"text", userText}});
    for (qsizetype index = 0; index < input.images.size(); ++index) {
        const auto &image = input.images[index];
        if (image.isNull()) { emit failed(tr("截图解码失败，未发送请求。")); return; }
        const int edgeLimit = anthropic ? 8000 : settings.providerId == "deepseek" ? (input.images.size() >= 15 ? 4096 : 8192) : 32767;
        if (image.width() > edgeLimit || image.height() > edgeLimit) {
            emit failed(tr("此接口支持的图片边长为 %1 像素以内，请分区批注。").arg(edgeLimit)); return;
        }
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) { emit failed(tr("无法编码截图。")); return; }
        if (png.size() > 5 * 1024 * 1024) { emit failed(tr("单张图片超过 5 MiB，请分区批注。")); return; }
        const auto label = input.imageLabels.isEmpty() ? QString("Image %1").arg(index + 1) : input.imageLabels[index];
        content.append(QJsonObject{{"type", responses ? "input_text" : "text"},
                                   {"text", QString("Image %1: %2; dimensions %3 × %4 image pixels.")
                                                .arg(index + 1).arg(label).arg(image.width()).arg(image.height())}});
        const auto data = QString::fromLatin1(png.toBase64());
        if (anthropic) content.append(QJsonObject{{"type", "image"}, {"source", QJsonObject{{"type", "base64"}, {"media_type", "image/png"}, {"data", data}}}});
        else if (responses) content.append(QJsonObject{{"type", "input_image"}, {"image_url", "data:image/png;base64," + data}, {"detail", "high"}});
        else content.append(QJsonObject{{"type", "image_url"}, {"image_url", QJsonObject{{"url", "data:image/png;base64," + data}, {"detail", "high"}}}});
    }
    QJsonObject body{{"model", settings.model.trimmed()}, {"stream", false}};
    const int maxOutput = test ? 2048 : 16000;
    if (responses) {
        body["instructions"] = prompt;
        body["input"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}};
        body["max_output_tokens"] = maxOutput;
        body["store"] = false;
    } else if (anthropic) {
        body["system"] = prompt;
        body["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}};
        body["max_tokens"] = maxOutput;
    } else {
        body["messages"] = QJsonArray{QJsonObject{{"role", "system"}, {"content", prompt}}, QJsonObject{{"role", "user"}, {"content", content}}};
        body[settings.providerId.startsWith("kimi-") ? "max_completion_tokens" : "max_tokens"] = maxOutput;
    }
    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxRequestBytes) { emit failed(tr("本次图片总量超过 32 MiB，请分批解析。")); return; }
    connection_ = settings; key_ = key; testing_ = test; context_ = test ? QString() : aiInputContext(input);
    response_.clear(); terminalError_.clear(); cancelled_ = false;
    QNetworkRequest request(endpointOverride_.isEmpty() ? endpoint(settings) : endpointOverride_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "EditHere/" EDITHERE_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(settings.timeoutSeconds * 1000);
    if (anthropic) { request.setRawHeader("x-api-key", key.toUtf8()); request.setRawHeader("anthropic-version", "2023-06-01"); }
    else request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    reply_ = network_.post(request, bytes);
    reply_->setReadBufferSize(64 * 1024);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!reply_) return;
        response_ += reply_->readAll();
        if (response_.size() > MaxResponseBytes) { terminalError_ = tr("API 回复超过 2 MiB，已停止接收。"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &AiFeedbackClient::finish);
    timeout_.start(settings.timeoutSeconds * 1000);
}
void AiFeedbackClient::cancel() {
    if (!reply_) return;
    cancelled_ = true; reply_->abort();
}
void AiFeedbackClient::fail(const QString &message) {
    const auto safe = safeMessage(message, key_);
    key_.fill(QChar('\0')); key_.clear(); facts_ = {}; context_.clear(); response_.clear();
    emit failed(safe);
}
void AiFeedbackClient::finish() {
    if (!reply_) return;
    timeout_.stop();
    auto reply = reply_.data(); reply_.clear();
    if (reply->isOpen()) response_ += reply->readAll();
    reply->deleteLater();
    if (cancelled_) {
        key_.fill(QChar('\0')); key_.clear(); facts_ = {}; context_.clear(); response_.clear();
        cancelled_ = false;
        emit cancelled();
        return;
    }
    if (!terminalError_.isEmpty()) { fail(terminalError_); return; }
    if (response_.size() > MaxResponseBytes) { fail(tr("API 回复超过 2 MiB，已停止接收。")); return; }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) { fail(tr("请在设置中填写重定向后的 API 地址。")); return; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(response_, &parse); response_.clear();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        auto detail = document.object()["error"].toObject()["message"].toString();
        if (detail.isEmpty()) detail = reply->errorString();
        fail(tr("API 请求失败（HTTP %1）：%2").arg(status).arg(detail)); return;
    }
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { fail(tr("API 返回了无效的 JSON 回复。")); return; }
    const auto object = document.object();
    if (object.contains("error") && !object["error"].isNull()) { fail(tr("API 返回错误：%1").arg(object["error"].toObject()["message"].toString())); return; }
    QString error;
    auto content = textFromResponse(object, connection_.protocol, &error);
    if (!error.isEmpty()) { fail(error); return; }
    if (content.contains(key_) || content.contains("data:image/", Qt::CaseInsensitive)) {
        fail(tr("回复中含有密钥或图片编码，请检查 API 服务。")); return;
    }
    QString text;
    if (testing_) {
        if (content.remove(QRegularExpression("[\\s.!。！]" )).compare("BLUE", Qt::CaseInsensitive) != 0) {
            fail(tr("接口已响应，但没有通过图像输入测试，请核对模型是否支持识图。")); return;
        }
        text = tr("连接成功，模型已接受测试图片。");
    } else {
        text = aiFormatResult(content, facts_, context_, &error);
        if (!error.isEmpty()) { fail(error); return; }
    }
    const auto usage = object["usage"].toObject();
    const auto inputCount = usage.contains("input_tokens") ? usage["input_tokens"] : usage["prompt_tokens"];
    const auto outputCount = usage.contains("output_tokens") ? usage["output_tokens"] : usage["completion_tokens"];
    const auto inputTokens = inputCount.toInteger(-1), outputTokens = outputCount.toInteger(-1);
    key_.fill(QChar('\0')); key_.clear(); facts_ = {}; context_.clear();
    // An omitted/non-numeric usage field is unavailable, never zero usage.
    if (inputCount.isDouble() && outputCount.isDouble() && inputTokens >= 0 && outputTokens >= 0)
        emit usageAvailable(inputTokens, outputTokens);
    emit completed(text);
}
} // namespace h2d
