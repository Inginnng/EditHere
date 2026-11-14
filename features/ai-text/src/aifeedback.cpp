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
#include <limits>
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
    const bool imageSet = !test && aiIsImageSet(input.feedback);
    if (!imageSet && input.images.isEmpty()) { emit failed(tr("没有可用于理解批注的截图。")); return; }
    if (!imageSet && input.images.size() > MaxImages) { emit failed(tr("一次可解析 16 张图片，请分批处理。")); return; }
    if (!input.imageLabels.isEmpty() && input.imageLabels.size() != input.images.size()) {
        emit failed(tr("截图与图像说明数量不一致。")); return;
    }
    if (imageSet) {
        qsizetype count = 0;
        for (const auto n : input.pageImageCounts) {
            if (n < 1 || n > 2) { emit failed(tr("组图批注与截图不匹配。")); return; }
            count += n;
        }
        if (input.pageImageCounts.size() != input.feedback["pages"].toArray().size() || count != input.images.size()) {
            emit failed(tr("组图批注与截图不匹配。")); return;
        }
    }
    QJsonArray facts;
    QString error;
    if (!test && !aiCollectFacts(input.feedback, &facts, &error)) { emit failed(error); return; }
    const QString prompt = test ? "Identify the dominant color in the attached image. Reply with one English color name only."
                                : systemPrompt.trimmed().isEmpty() ? aiSystemPrompt() : systemPrompt;
    if (prompt.isEmpty()) { emit failed(tr("系统提示词为空，无法发送请求。")); return; }

    connection_ = settings; key_ = key; testing_ = test; imageSet_ = imageSet; prompt_ = prompt;
    context_ = test ? QString() : aiInputContext(input);
    pending_.append(input);
    sendNext();
}

namespace {
bool splitImageSet(const AiFeedbackInput &input, QList<AiFeedbackInput> &pending) {
    const auto pages = input.feedback["pages"].toArray();
    if (pages.isEmpty() || (pages.size() == 1 && input.feedback["groupNotes"].toArray().isEmpty())) return false;

    // A page's original and result must stay together. Group notes belong in the
    // final batch only, including when that batch can be sent as text alone.
    const auto middle = (pages.size() + 1) / 2;
    qsizetype imageCount = 0;
    QJsonArray firstPages, lastPages;
    for (qsizetype i = 0; i < pages.size(); ++i) {
        if (i < middle) {
            firstPages.append(pages.at(i));
            imageCount += input.pageImageCounts.at(i);
        } else lastPages.append(pages.at(i));
    }
    auto first = input.feedback, last = input.feedback;
    first["pages"] = firstPages; first.remove("groupNotes");
    last["pages"] = lastPages;
    pending.prepend({last, input.images.sliced(imageCount),
                     input.imageLabels.isEmpty() ? QStringList() : QStringList(input.imageLabels.sliced(imageCount)),
                     input.pageImageCounts.sliced(middle)});
    pending.prepend({first, input.images.first(imageCount),
                     input.imageLabels.isEmpty() ? QStringList() : QStringList(input.imageLabels.first(imageCount)),
                     input.pageImageCounts.first(middle)});
    return true;
}

QByteArray requestBody(const AiConnectionSettings &settings, const QString &prompt, const AiFeedbackInput &input,
                       const QJsonArray &facts, bool test, QString *error) {
    auto metadata = aiFeedbackData(input.feedback).toObject();
    metadata.remove("objects");
    // Supply batch-local context, with actionable objects only in itemsToInterpret.
    if (aiIsImageSet(metadata)) {
        metadata.remove("pageIndex");
        metadata.remove("groupNotes");
    }
    for (const auto &key : {"frames", "pages"}) {
        if (!metadata.contains(key)) continue;
        QJsonArray contexts;
        for (const auto &value : metadata[key].toArray()) {
            auto entry = value.toObject(); auto feedback = entry["feedback"].toObject();
            feedback.remove("objects"); entry["feedback"] = feedback; contexts.append(entry);
        }
        metadata[key] = contexts;
    }
    const QString userText = test ? "What is the dominant color of this image? Reply with one English color name only." :
        QString::fromUtf8(QJsonDocument(QJsonObject{{"feedbackContext", metadata}, {"itemsToInterpret", facts},
                                                   {"additionalUserPreferences", settings.extraInstructions}}).toJson(QJsonDocument::Compact));
    QJsonArray content;
    const bool responses = settings.protocol == "responses";
    const bool anthropic = settings.protocol == "anthropic-messages";
    content.append(QJsonObject{{"type", responses ? "input_text" : "text"}, {"text", userText}});
    qsizetype imageBytes = 0;
    for (qsizetype index = 0; index < input.images.size(); ++index) {
        const auto &image = input.images[index];
        if (image.isNull()) {
            *error = QCoreApplication::translate("h2d::AiFeedbackClient", "截图解码失败，未发送请求。"); return {};
        }
        const int edgeLimit = anthropic ? 8000 : settings.providerId == "deepseek" ? (input.images.size() >= 15 ? 4096 : 8192) : 32767;
        if (image.width() > edgeLimit || image.height() > edgeLimit) {
            *error = QCoreApplication::translate("h2d::AiFeedbackClient", "此接口支持的图片边长为 %1 像素以内，请分区批注。").arg(edgeLimit);
            return {};
        }
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            *error = QCoreApplication::translate("h2d::AiFeedbackClient", "无法编码截图。"); return {};
        }
        if (png.size() > 5 * 1024 * 1024) {
            *error = QCoreApplication::translate("h2d::AiFeedbackClient", "单张图片超过 5 MiB，请分区批注。"); return {};
        }
        imageBytes += ((png.size() + 2) / 3) * 4;
        if (imageBytes > MaxRequestBytes) return {}; // Over budget even before adding text.
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
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}
} // namespace

void AiFeedbackClient::sendNext() {
    QByteArray bytes;
    for (;;) {
        const auto input = pending_.takeFirst();
        // DeepSeek has a stricter edge limit at 15 images, so keep set batches below it.
        const auto maxImages = imageSet_ && connection_.providerId == "deepseek" ? 14 : MaxImages;
        if (input.images.size() > maxImages) {
            if (imageSet_ && splitImageSet(input, pending_)) continue;
            fail(tr("一次可解析 16 张图片，请分批处理。")); return;
        }
        QString error;
        facts_ = {};
        if (!testing_ && !aiCollectFacts(input.feedback, &facts_, &error, renderedCount_)) { fail(error); return; }
        bytes = requestBody(connection_, prompt_, input, facts_, testing_, &error);
        if (!error.isEmpty()) { fail(error); return; }
        if (!bytes.isEmpty() && bytes.size() <= MaxRequestBytes) break;
        if (!imageSet_ || !splitImageSet(input, pending_)) {
            fail(tr("本次图片总量超过 32 MiB，请分批解析。")); return;
        }
        bytes.clear();
    }
    response_.clear(); terminalError_.clear(); cancelled_ = false;
    QNetworkRequest request(endpointOverride_.isEmpty() ? endpoint(connection_) : endpointOverride_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "EditHere/" EDITHERE_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(connection_.timeoutSeconds * 1000);
    if (connection_.protocol == "anthropic-messages") { request.setRawHeader("x-api-key", key_.toUtf8()); request.setRawHeader("anthropic-version", "2023-06-01"); }
    else request.setRawHeader("Authorization", "Bearer " + key_.toUtf8());
    reply_ = network_.post(request, bytes);
    reply_->setReadBufferSize(64 * 1024);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!reply_) return;
        response_ += reply_->readAll();
        if (response_.size() > MaxResponseBytes) { terminalError_ = tr("API 回复超过 2 MiB，已停止接收。"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &AiFeedbackClient::finish);
    timeout_.start(connection_.timeoutSeconds * 1000);
}
void AiFeedbackClient::cancel() {
    if (!reply_) return;
    cancelled_ = true; reply_->abort();
}
void AiFeedbackClient::fail(const QString &message) {
    const auto safe = safeMessage(message, key_);
    resetRun();
    emit failed(safe);
}
void AiFeedbackClient::resetRun() {
    key_.fill(QChar('\0')); key_.clear();
    facts_ = {}; context_.clear(); prompt_.clear(); response_.clear(); pending_.clear(); results_.clear();
    renderedCount_ = 0; inputTokens_ = 0; outputTokens_ = 0; usageKnown_ = true; cancelled_ = false;
}
void AiFeedbackClient::finish() {
    if (!reply_) return;
    timeout_.stop();
    auto reply = reply_.data(); reply_.clear();
    if (reply->isOpen()) response_ += reply->readAll();
    reply->deleteLater();
    if (cancelled_) {
        resetRun();
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
        text = aiFormatResult(content, facts_, results_.isEmpty() ? context_ : QString(), &error, renderedCount_, imageSet_);
        if (!error.isEmpty()) { fail(error); return; }
    }
    const auto usage = object["usage"].toObject();
    const auto inputCount = usage.contains("input_tokens") ? usage["input_tokens"] : usage["prompt_tokens"];
    const auto outputCount = usage.contains("output_tokens") ? usage["output_tokens"] : usage["completion_tokens"];
    const auto inputTokens = inputCount.toInteger(-1), outputTokens = outputCount.toInteger(-1);
    // Don't show a partial token count if any batch omitted usage.
    const auto maxTokens = std::numeric_limits<qint64>::max();
    usageKnown_ = usageKnown_ && inputCount.isDouble() && outputCount.isDouble() && inputTokens >= 0 && outputTokens >= 0
                  && inputTokens <= maxTokens - inputTokens_ && outputTokens <= maxTokens - outputTokens_;
    if (usageKnown_) { inputTokens_ += inputTokens; outputTokens_ += outputTokens; }
    renderedCount_ += facts_.size();
    results_.append(text);
    if (!pending_.isEmpty()) { sendNext(); return; }

    const auto result = results_.join("\n\n");
    const auto totalInput = inputTokens_, totalOutput = outputTokens_;
    const bool usageKnown = usageKnown_;
    resetRun();
    if (usageKnown) emit usageAvailable(totalInput, totalOutput);
    emit completed(result);
}
} // namespace h2d
