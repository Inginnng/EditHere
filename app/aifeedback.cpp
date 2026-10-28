#include "aifeedback.h"
#include "i18n.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <functional>

// Static-library resources must be referenced explicitly, including in tests.
static void initializeAiResources() {
    static const bool initialized = [] { Q_INIT_RESOURCE(ai_assets); return true; }();
    Q_UNUSED(initialized);
}

namespace h2d {
namespace {
constexpr qsizetype MaxResponseBytes = 2 * 1024 * 1024;
constexpr qsizetype MaxRequestBytes = 32 * 1024 * 1024;
constexpr qsizetype MaxImages = 16; // Original plus result for up to eight frames.
constexpr qsizetype MaxObjects = 100;
// Match lupdate's namespace context for tr() calls in the free helpers below.
QString tr(const char *message) { return QCoreApplication::translate("h2d", message); }
QString number(const QJsonValue &value) {
    if (!value.isDouble()) return QStringLiteral("—");
    // Preserve JSON integers and legacy fractional coordinates without a
    // second conversion through a lower-precision display format.
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
QString rectangle(const QJsonObject &rect) {
    return QString("(%1, %2) – (%3, %4)").arg(number(rect["x1"]), number(rect["y1"]),
                                             number(rect["x2"]), number(rect["y2"]));
}
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
QJsonValue stripped(const QJsonValue &value) {
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item : value.toArray()) result.append(stripped(item));
        return result;
    }
    if (!value.isObject()) return value;
    auto object = value.toObject();
    // Screenshot bytes are supplied only through the explicit image list.
    for (const auto &key : {"dataUrl", "pngBase64", "imageData", "previewDataUrl"}) object.remove(key);
    if (object["image"].isString()) object["image"] = QJsonObject{{"bytes", "provided separately"}};
    for (auto it = object.begin(); it != object.end(); ++it) it.value() = stripped(it.value());
    return object;
}
bool collectFacts(const QJsonObject &feedback, QJsonArray *facts, QString *error) {
    std::function<bool(const QJsonArray &, const QJsonObject &, const QString &)> collect;
    collect = [&](const QJsonArray &objects, const QJsonObject &frame, const QString &parent) {
        for (const auto &value : objects) {
            if (!value.isObject()) { *error = tr("批注对象格式无效。"); return false; }
            auto object = value.toObject();
            const auto children = object.take("children").toArray();
            if (facts->size() >= MaxObjects) { *error = tr("一次最多整理 100 个批注对象，请分批处理。"); return false; }
            const auto originalId = object["id"].toString();
            const auto id = QString("item-%1").arg(facts->size() + 1);
            object["objectId"] = originalId;
            object["id"] = id;
            if (!parent.isEmpty()) object["parentItemId"] = parent;
            for (auto it = frame.begin(); it != frame.end(); ++it) object[it.key()] = it.value();
            facts->append(object);
            if (!collect(children, frame, id)) return false;
        }
        return true;
    };
    if (feedback["schemaVersion"].toString().startsWith("video-feedback-")) {
        const auto frames = feedback["frames"].toArray();
        if (frames.size() > 8) { *error = tr("一次最多整理 8 个视频帧，请分批处理；未发送任何图片。"); return false; }
        for (const auto &value : frames) {
            const auto frame = value.toObject();
            const QJsonObject context{{"frameId", frame["id"]}, {"timestampMs", frame["timestampMs"]},
                                      {"timestampUs", frame["timestampUs"]}};
            if (!collect(frame["feedback"].toObject()["objects"].toArray(), context, {})) return false;
        }
    } else if (!collect(feedback["objects"].toArray(), {}, {})) return false;
    if (facts->isEmpty()) { *error = tr("请先添加批注或调整组件，再生成修改意见。"); return false; }
    return true;
}
QString exactFacts(const QJsonObject &fact) {
    QStringList lines;
    if (fact.contains("frameId"))
        lines << tr("视频帧：%1；时间戳 %2 ms / %3 µs")
                     .arg(fact["frameId"].toString(), number(fact["timestampMs"]), number(fact["timestampUs"]));
    if (!fact["parentItemId"].toString().isEmpty()) lines << tr("所属对象：%1").arg(fact["parentItemId"].toString());
    if (fact["kind"] == "global") lines << tr("范围：全局");
    if (fact["source"].isObject()) lines << tr("框选区域：%1").arg(rectangle(fact["source"].toObject()));
    if (fact["point"].isObject()) {
        const auto point = fact["point"].toObject();
        lines << tr("标注点：(%1, %2)").arg(number(point["x"]), number(point["y"]));
    }
    const auto movement = fact.contains("movement") ? fact["movement"].toObject() : fact["result"].toObject();
    if (!movement.isEmpty()) {
        lines << tr("目标区域：%1；位移 dx=%2，dy=%3")
                     .arg(rectangle(movement["rect"].toObject()), number(movement["dx"]), number(movement["dy"]));
        if (movement.contains("scaleX")) lines << tr("水平缩放：%1 倍").arg(number(movement["scaleX"]));
        if (movement.contains("scaleY")) lines << tr("垂直缩放：%1 倍").arg(number(movement["scaleY"]));
    } else if (!fact["movements"].toArray().isEmpty()) {
        const auto movements = fact["movements"].toArray();
        for (int i = 0; i < movements.size(); ++i)
            lines << tr("移动目标 %1：%2").arg(i + 1).arg(rectangle(movements[i].toObject()["to"].toObject()));
    }
    for (const auto &annotation : fact["annotations"].toArray()) {
        if (annotation.isString()) lines << tr("用户原话：%1").arg(annotation.toString());
        else {
            const auto object = annotation.toObject();
            lines << tr("批注 #%1 原话：%2").arg(number(object["number"]), object["text"].toString());
        }
    }
    return lines.join('\n');
}
QString safeMessage(QString message, const QString &key) {
    if (!key.isEmpty()) message.replace(key, "[REDACTED]");
    message.replace(QRegularExpression("(?:sk-|key-)[A-Za-z0-9_-]{8,}"), "[REDACTED]");
    message.replace(QRegularExpression("data:image/[^\\s\"]+"), "[image]");
    return message.left(700);
}
QString inputContext(const AiFeedbackInput &input) {
    QStringList lines;
    const auto appendCapture = [&](const QJsonObject &capture, const QString &prefix = {}) {
        for (const auto &field : {QString("page"), QString("fileName"), QString("appName"), QString("windowTitle")}) {
            const auto value = capture[field].toString();
            if (value.isEmpty()) continue;
            const auto label = field == "page" ? tr("页面") : field == "fileName" ? tr("文件") :
                               field == "appName" ? tr("应用") : tr("窗口");
            lines << prefix + label + QStringLiteral(": ") + value;
        }
    };
    const auto appendImageMetadata = [&](const QJsonObject &feedback, const QString &prefix) {
        const auto image = feedback["image"].toObject();
        const auto dpr = image["devicePixelRatio"];
        // Imported images commonly have unknown DPR. QImage's default 1 must
        // not be substituted for missing capture metadata.
        lines << prefix + tr("设备像素比（DPR）：%1").arg(dpr.isDouble() ? number(dpr) : tr("未知"));
    };
    appendCapture(input.feedback["capture"].toObject());
    if (input.feedback["schemaVersion"].toString().startsWith("video-feedback-")) {
        for (const auto &value : input.feedback["frames"].toArray()) {
            const auto frame = value.toObject();
            const auto prefix = tr("视频帧 %1：").arg(frame["id"].toString());
            const auto feedback = frame["feedback"].toObject();
            appendCapture(feedback["capture"].toObject(), prefix);
            appendImageMetadata(feedback, prefix);
        }
    } else appendImageMetadata(input.feedback, {});
    for (qsizetype i = 0; i < input.images.size(); ++i) {
        const auto label = input.imageLabels.isEmpty() ? QString() : input.imageLabels[i];
        lines << tr("图像 %1：%2；尺寸 %3 × %4 图像像素")
                     .arg(i + 1).arg(label).arg(input.images[i].width()).arg(input.images[i].height());
    }
    return lines.join('\n');
}
QString resultText(QString content, const QJsonArray &facts, const QString &context, QString *error) {
    content = content.trimmed();
    if (content.startsWith("```")) {
        const auto newline = content.indexOf('\n');
        if (newline >= 0 && content.endsWith("```")) content = content.mid(newline + 1, content.size() - newline - 4).trimmed();
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(content.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() || !document.object()["items"].isArray()) {
        *error = tr("模型没有返回可校验的修改清单，请重试或更换模型；原始批注已保留。"); return {};
    }
    const auto items = document.object()["items"].toArray();
    QMap<QString, QJsonObject> byId;
    QSet<QString> expected;
    for (const auto &value : facts) expected.insert(value.toObject()["id"].toString());
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto id = item["id"].toString();
        if (!expected.contains(id) || byId.contains(id) || !item["target"].isString() ||
            !item["instruction"].isString() || item["target"].toString().trimmed().isEmpty() ||
            item["instruction"].toString().trimmed().isEmpty() || item["target"].toString().size() > 2000 ||
            item["instruction"].toString().size() > 10000 ||
            (item.contains("uncertainty") && (!item["uncertainty"].isString() || item["uncertainty"].toString().size() > 2000))) {
            *error = tr("模型返回了重复、未知或无效的批注条目，请重试；未生成不完整结果。"); return {};
        }
        byId[id] = item;
    }
    if (byId.size() != expected.size()) {
        *error = tr("模型遗漏了部分批注，请重试或分批处理；未生成不完整结果。"); return {};
    }
    QStringList sections;
    sections << tr("修改意见（AI 整理）") <<
        tr("坐标单位为截图图像像素，原点在左上角，不能直接当作 CSS 像素。普通批注对应调整后画面；带 movement 的对象 source 对应原图，目标区域对应调整后画面。以下数值及用户原话由原始批注直接保留。");
    if (!context.isEmpty()) sections << context;
    for (const auto &value : facts) {
        const auto fact = value.toObject();
        const auto item = byId.value(fact["id"].toString());
        auto title = fact["id"].toString();
        if (!fact["objectId"].toString().isEmpty()) title += " / " + fact["objectId"].toString();
        QString section = QString("%1. %2\n%3\n%4")
                              .arg(title, item["target"].toString().trimmed(), item["instruction"].toString().trimmed(), exactFacts(fact));
        if (!item["uncertainty"].toString().trimmed().isEmpty())
            section += '\n' + tr("待核对：%1").arg(item["uncertainty"].toString().trimmed());
        sections << section;
    }
    return sections.join("\n\n");
}
QString textFromResponse(const QJsonObject &response, const QString &protocol, QString *error) {
    QStringList texts;
    if (protocol == "responses") {
        if (response["status"].toString() != "completed") {
            *error = tr("模型响应尚未完成或已被截断，请增加输出额度或分批处理。"); return {};
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
            *error = reason == "refusal" ? tr("模型拒绝了本次请求。") : tr("模型响应未完整结束，请分批处理。"); return {};
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
            *error = tr("模型响应被截断或未正常结束，请分批处理。"); return {};
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

QVector<AiProviderPreset> aiProviderPresets() {
    initializeAiResources();
    QFile file(":/ai/provider-presets.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto array = QJsonDocument::fromJson(file.readAll()).array();
    QVector<AiProviderPreset> result;
    for (const auto &value : array) {
        const auto object = value.toObject();
        const auto name = !isChineseInterface() && !object["nameEn"].toString().isEmpty() ? object["nameEn"].toString() : object["name"].toString();
        const auto note = !isChineseInterface() && !object["noteEn"].toString().isEmpty() ? object["noteEn"].toString() : object["note"].toString();
        result.append({object["id"].toString(), name, object["baseUrl"].toString(),
                       object["model"].toString(), object["protocol"].toString(), object["docsUrl"].toString(), note});
    }
    return result;
}
AiConnectionSettings defaultAiConnection(const QString &providerId) {
    AiConnectionSettings result;
    result.providerId = providerId;
    for (const auto &preset : aiProviderPresets()) if (preset.id == providerId) {
        result.baseUrl = preset.baseUrl; result.model = preset.model; result.protocol = preset.protocol; break;
    }
    return result;
}
QString validateAiConnection(const AiConnectionSettings &settings, bool requireModel) {
    const auto text = settings.baseUrl.trimmed();
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        text.contains(QRegularExpression("[\\x{0000}-\\x{0020}\\x{007f}]")))
        return tr("请输入有效的 API 基础地址，不能包含账号、密码、查询参数或片段。");
    const auto host = url.host().toLower();
    if (url.scheme() != "https" && !(url.scheme() == "http" && (host == "127.0.0.1" || host == "::1" || host == "localhost")))
        return tr("API 地址必须使用 HTTPS；仅本机服务允许 HTTP。");
    if (settings.protocol != "responses" && settings.protocol != "chat-completions" && settings.protocol != "anthropic-messages")
        return tr("请选择有效的 API 协议。");
    const auto path = url.path();
    const auto urlSuffix = path.endsWith('/') ? path.left(path.size() - 1) : path;
    for (const auto &suffix : {QString("/responses"), QString("/chat/completions"), QString("/messages")}) {
        const auto expected = settings.protocol == "responses" ? "/responses" : settings.protocol == "chat-completions" ? "/chat/completions" : "/messages";
        if (urlSuffix.endsWith(suffix) && suffix != expected) return tr("API 地址的接口路径与所选协议不一致。");
    }
    if (requireModel && (settings.model.trimmed().isEmpty() || settings.model.size() > 200 ||
                         settings.model.contains(QRegularExpression("[\\x{0000}-\\x{001f}\\x{007f}]"))))
        return tr("请输入有效的模型名称。");
    if (settings.timeoutSeconds < 10 || settings.timeoutSeconds > 600) return tr("请求超时应在 10 到 600 秒之间。");
    if (settings.extraInstructions.size() > 10000) return tr("补充要求最多 10000 个字符。");
    return {};
}
QString aiSystemPrompt() {
    initializeAiResources();
    QFile file(":/ai/instructions-prompt.txt");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}
AiFeedbackClient::AiFeedbackClient(QObject *parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (!reply_) return;
        terminalError_ = tr("请求超时，未自动重试；原始批注已保留。");
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
void AiFeedbackClient::generate(const AiConnectionSettings &settings, const QString &key, const AiFeedbackInput &input) {
    start(settings, key, input, false);
}
void AiFeedbackClient::testConnection(const AiConnectionSettings &settings, const QString &key) {
    AiFeedbackInput input;
    QImage image(128, 128, QImage::Format_RGB32); image.fill(QColor(38, 106, 218));
    input.images.append(image); input.imageLabels.append("Connection test image; no user screenshot.");
    start(settings, key, input, true);
}
void AiFeedbackClient::start(const AiConnectionSettings &settings, const QString &key, const AiFeedbackInput &input, bool test) {
    if (isRunning()) { emit failed(tr("已有请求正在进行，请等待或取消。")); return; }
    const auto validation = validateAiConnection(settings);
    if (!validation.isEmpty()) { emit failed(validation); return; }
    if (key.trimmed().isEmpty() || key.contains(QRegularExpression("[\\x{0000}-\\x{0020}\\x{007f}]"))) {
        emit failed(tr("请填写有效的 API Key。")); return;
    }
    if (input.images.isEmpty()) { emit failed(tr("没有可用于理解批注的截图。")); return; }
    if (input.images.size() > MaxImages) { emit failed(tr("一次最多发送 16 张图片，请分批处理；未发送任何图片。")); return; }
    if (!input.imageLabels.isEmpty() && input.imageLabels.size() != input.images.size()) {
        emit failed(tr("截图与图像说明数量不一致。")); return;
    }
    facts_ = {};
    QString error;
    if (!test && !collectFacts(input.feedback, &facts_, &error)) { emit failed(error); return; }
    QString prompt = test ? "Identify the dominant color in the attached image. Reply with one English color name only." : aiSystemPrompt();
    if (prompt.isEmpty()) { emit failed(tr("内置提示词缺失，无法发送请求。")); return; }
    auto metadata = stripped(input.feedback).toObject();
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
            emit failed(tr("所选接口的单张截图边长最多 %1 像素，请分区批注；不会自动缩小图片。").arg(edgeLimit)); return;
        }
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) { emit failed(tr("无法编码截图。")); return; }
        if (png.size() > 5 * 1024 * 1024) { emit failed(tr("单张截图超过 5 MiB，请分区批注；不会自动降低截图清晰度。")); return; }
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
    if (bytes.size() > MaxRequestBytes) { emit failed(tr("请求超过 32 MiB，请分批处理；未发送任何图片。")); return; }
    connection_ = settings; key_ = key; testing_ = test; context_ = test ? QString() : inputContext(input);
    response_.clear(); terminalError_.clear();
    QNetworkRequest request(endpoint(settings));
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
    terminalError_ = tr("已取消请求；原始批注已保留。"); reply_->abort();
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
    if (!terminalError_.isEmpty()) { fail(terminalError_); return; }
    if (response_.size() > MaxResponseBytes) { fail(tr("API 回复超过 2 MiB，已停止接收。")); return; }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) { fail(tr("API 返回了重定向。为避免泄露密钥，请直接填写最终 API 地址。")); return; }
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
        fail(tr("模型回复包含凭据或图片编码，已停止生成；请核对所用 API 服务。")); return;
    }
    QString text;
    if (testing_) {
        if (content.remove(QRegularExpression("[\\s.!。！]" )).compare("BLUE", Qt::CaseInsensitive) != 0) {
            fail(tr("接口已响应，但没有通过图像输入测试，请核对模型是否支持识图。")); return;
        }
        text = tr("连接成功，模型已接受测试图片。");
    } else {
        text = resultText(content, facts_, context_, &error);
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
