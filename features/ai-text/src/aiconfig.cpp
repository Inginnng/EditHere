#include "aiconfig.h"
#include "i18n.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
// Static-library resources must be referenced explicitly, including in tests.
static void initializeAiResources() {
    static const bool initialized = [] { Q_INIT_RESOURCE(ai_assets); return true; }();
    Q_UNUSED(initialized);
}

namespace h2d {
namespace {
QString tr(const char *message) { return QCoreApplication::translate("h2d", message); }
}
QVector<AiProviderPreset> aiProviderPresets() {
    const auto array = QJsonDocument::fromJson(aiTextResource("provider-presets.json").toUtf8()).array();
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
QString aiSystemPrompt() { return aiTextResource("instructions-prompt.txt"); }
QString aiTextResource(const QString &name) {
    initializeAiResources();
    const auto folder = qEnvironmentVariable("EDITHERE_AI_TEXT_ASSETS");
    if (!folder.isEmpty()) {
        QFile file(QDir(folder).filePath(name));
        if (file.open(QIODevice::ReadOnly)) return QString::fromUtf8(file.readAll());
    }
    QFile file(":/ai/" + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}
} // namespace h2d
