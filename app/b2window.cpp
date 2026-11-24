#include "b2window.h"
#include "agentprotocol.h"
#include "aifeedbacksnapshot.h"
#include "autostart.h"
#include "detector.h"
#include "diagnostics.h"
#include "i18n.h"
#include "ui.h"
#include "webview2host.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace h2d {

namespace {

QString themeModeToString(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light: return "light";
    case ThemeMode::Dark: return "dark";
    case ThemeMode::System: return "system";
    }
    return "light";
}

ThemeMode stringToThemeMode(const QString &value) {
    if (value == "dark") return ThemeMode::Dark;
    if (value == "light") return ThemeMode::Light;
    return ThemeMode::System;
}

QString languageModeToString(LanguageMode lang) {
    switch (lang) {
    case LanguageMode::English: return "en";
    case LanguageMode::TraditionalChinese: return "zh_TW";
    case LanguageMode::SimplifiedChinese: return "zh_CN";
    case LanguageMode::System:
    default: return "system";
    }
}

LanguageMode stringToLanguageMode(const QString &value, LanguageMode fallback) {
    if (value == "en" || value == "English") return LanguageMode::English;
    if (value == "zh_TW" || value == QString::fromUtf8("繁體中文")) return LanguageMode::TraditionalChinese;
    if (value == "zh_CN" || value == QString::fromUtf8("简体中文")) return LanguageMode::SimplifiedChinese;
    if (value == "system" || value == QString::fromUtf8("跟随系统")) return LanguageMode::System;
    return fallback;
}

QString ocrLanguageModeToString(OcrLanguageMode mode) {
    switch (mode) {
    case OcrLanguageMode::SimplifiedChinese: return "zh_CN";
    case OcrLanguageMode::English: return "en";
    case OcrLanguageMode::System:
    default: return "system";
    }
}

OcrLanguageMode stringToOcrLanguageMode(const QString &value, OcrLanguageMode fallback) {
    if (value == "zh_CN" || value == QString::fromUtf8("简体中文")) return OcrLanguageMode::SimplifiedChinese;
    if (value == "en" || value == "English") return OcrLanguageMode::English;
    if (value == "system" || value == QString::fromUtf8("跟随系统")) return OcrLanguageMode::System;
    return fallback;
}

QString exportModeToString(ImageExportMode mode) {
    switch (mode) {
    case ImageExportMode::Embedded: return "embedded";
    case ImageExportMode::None: return "none";
    case ImageExportMode::File:
    default: return "file";
    }
}

QJsonObject rectToJson(const QRect &r) {
    return QJsonObject{{"x1", r.left()}, {"y1", r.top()}, {"x2", r.left() + r.width()}, {"y2", r.top() + r.height()}};
}

QString normalizeAiProtocol(const QString &value) {
    if (value == "responses") return "responses";
    if (value == "anthropic" || value == "anthropic-messages") return "anthropic-messages";
    return "chat-completions";
}

QString maskSecretKey(const QString &key) {
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) return {};
    if (trimmed.size() <= 8) return QStringLiteral("••••••••");
    return trimmed.left(3) + QStringLiteral("••••••••") + trimmed.right(4);
}

QString sanitizeProviderId(const QString &raw) {
    QString id = raw.trimmed().toLower();
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
    if (valid.match(id).hasMatch()) return id;
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9-]+")), QStringLiteral("-"));
    id.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (id.isEmpty()) id = QStringLiteral("custom");
    return id.left(64);
}

AiConnectionSettings connectionFromJson(const QJsonObject &obj, const QString &providerId) {
    AiConnectionSettings conn = defaultAiConnection(providerId);
    conn.providerId = providerId;
    if (obj.contains("endpoint") || obj.contains("baseUrl")) {
        const QString ep = obj.contains("endpoint") ? obj.value("endpoint").toString()
                                                    : obj.value("baseUrl").toString();
        conn.baseUrl = ep.trimmed();
    }
    if (obj.contains("model")) {
        conn.model = obj.value("model").toString().trimmed();
    }
    if (obj.contains("protocol")) {
        conn.protocol = normalizeAiProtocol(obj.value("protocol").toString());
    }
    if (obj.contains("note") || obj.contains("extraInstructions")) {
        conn.extraInstructions = obj.contains("note") ? obj.value("note").toString()
                                                      : obj.value("extraInstructions").toString();
    }
    const int to = obj.value("timeout").toVariant().toInt();
    if (to >= 10 && to <= 600) {
        conn.timeoutSeconds = to;
    }
    return conn;
}

} // namespace

B2Window::B2Window(const AppSettings &settings, const QString &settingsFile, QWidget *parent)
    : QMainWindow(parent),
      server_(this),
      webview_(new WebView2Host(this)),
      ocr_(new OcrEngine(this)),
      aiCredentials_(new AiCredentials(this)),
      aiClient_(new AiFeedbackClient(this)),
      settings_(settings),
      settingsFile_(settingsFile) {
    setWindowTitle(tr("EditHere"));
    setWindowIcon(QGuiApplication::windowIcon());
    resize(1360, 860);
    setMinimumSize(820, 580);

    setCentralWidget(webview_);

    connect(webview_, &WebView2Host::messageReceived, this, &B2Window::handleWebMessage);
    connect(webview_, &WebView2Host::failed, this, [this](const QString &reason) {
        diagnostics::write(diagnostics::Level::Warning, "b2.window_webview_failed", reason);
        emit webviewFailed(reason);
    });

    connect(aiCredentials_, &AiCredentials::loaded, this, [this](const QString &providerId, const QString &key) {
        if (!key.isEmpty()) {
            apiKeys_[providerId] = key;
        } else {
            apiKeys_.remove(providerId);
        }
        if (jsReady_) {
            pushEvent("ai.state", buildAiStateJson());
        }
    });
    connect(aiCredentials_, &AiCredentials::saved, this, [this](const QString &) {
        if (jsReady_) pushEvent("ai.state", buildAiStateJson());
    });
    connect(aiCredentials_, &AiCredentials::removed, this, [this](const QString &providerId) {
        apiKeys_.remove(providerId);
        if (jsReady_) pushEvent("ai.state", buildAiStateJson());
    });

    connect(aiClient_, &AiFeedbackClient::usageAvailable, this, [this](qint64 inTok, qint64 outTok) {
        lastInputTokens_ = inTok;
        lastOutputTokens_ = outTok;
    });
    connect(aiClient_, &AiFeedbackClient::completed, this, [this](const QString &text) {
        if (!pendingAiTestId_.isNull() && !pendingAiTestId_.isUndefined()) {
            const QJsonValue id = pendingAiTestId_;
            pendingAiTestId_ = QJsonValue();
            sendRpcResult(id, true, QJsonObject{{"ok", true}, {"reply", text}});
            return;
        }
        if (!pendingAiAnalyzeId_.isNull() && !pendingAiAnalyzeId_.isUndefined()) {
            const QJsonValue id = pendingAiAnalyzeId_;
            pendingAiAnalyzeId_ = QJsonValue();
            sendRpcResult(id, true, QJsonObject{
                {"ok", true},
                {"text", text},
                {"inputTokens", double(lastInputTokens_)},
                {"outputTokens", double(lastOutputTokens_)},
            });
        }
    });
    connect(aiClient_, &AiFeedbackClient::failed, this, [this](const QString &message) {
        if (!pendingAiTestId_.isNull() && !pendingAiTestId_.isUndefined()) {
            const QJsonValue id = pendingAiTestId_;
            pendingAiTestId_ = QJsonValue();
            sendRpcResult(id, false, {}, message);
        }
        if (!pendingAiAnalyzeId_.isNull() && !pendingAiAnalyzeId_.isUndefined()) {
            const QJsonValue id = pendingAiAnalyzeId_;
            pendingAiAnalyzeId_ = QJsonValue();
            sendRpcResult(id, false, {}, message);
        }
    });
    connect(aiClient_, &AiFeedbackClient::cancelled, this, [this]() {
        if (!pendingAiTestId_.isNull() && !pendingAiTestId_.isUndefined()) {
            const QJsonValue id = pendingAiTestId_;
            pendingAiTestId_ = QJsonValue();
            sendRpcResult(id, false, {}, tr("已取消测试连接。"));
        }
        if (!pendingAiAnalyzeId_.isNull() && !pendingAiAnalyzeId_.isUndefined()) {
            const QJsonValue id = pendingAiAnalyzeId_;
            pendingAiAnalyzeId_ = QJsonValue();
            sendRpcResult(id, false, {}, tr("已取消 AI 解析。"));
        }
    });

    preloadAiCredentials();

    updater_ = new UpdateChecker(this);
    connect(updater_, &UpdateChecker::finished, this, [this]() {
        if (pendingUpdateCheckId_.isNull() || pendingUpdateCheckId_.isUndefined()) return;
        const QJsonValue id = pendingUpdateCheckId_;
        pendingUpdateCheckId_ = QJsonValue();
        const auto &res = updater_->lastResult();
        QString statusStr = "failed";
        if (res.status == UpdateChecker::Available) statusStr = "available";
        else if (res.status == UpdateChecker::Current || res.status == UpdateChecker::NewerLocal) statusStr = "upToDate";
        sendRpcResult(id, true, QJsonObject{
            {"status", statusStr},
            {"tagName", res.tagName},
            {"currentVersion", EDITHERE_VERSION},
            {"url", res.url.toString()},
            {"message", res.message},
        });
    });

    QString serverError;
    if (!server_.start(&serverError)) {
        QTimer::singleShot(0, this, [this, serverError]() { emit webviewFailed(serverError); });
        return;
    }

    webview_->initialize();
    webview_->navigate(server_.indexUrl());
}

B2Window::~B2Window() = default;

bool B2Window::isEffectiveDark() const {
    if (hasDarkOverride_) return uiDarkOverride_;
    if (settings_.theme == ThemeMode::Dark) return true;
    if (settings_.theme == ThemeMode::Light) return false;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void B2Window::syncDwmTitleBar(bool dark) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        BOOL val = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &val, sizeof(val));
    }
#else
    Q_UNUSED(dark);
#endif
}

void B2Window::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    syncDwmTitleBar(isEffectiveDark());
}

void B2Window::closeEvent(QCloseEvent *event) {
    if (agentSession_) {
        const auto choice = QMessageBox::question(
            this, tr("结束标注会话"),
            tr("当前正在等待提交批注返回给 AI。\n确定要取消本次会话并返回吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        emit agentSessionCancelled();
    }
    // Hide cleanly to system tray so global shortcuts and agent server stay active.
    hide();
    event->ignore();
}

void B2Window::openCapturedDocument(Document doc, bool replaceExisting) {
    if (doc.image.isNull()) return;
    if (doc.png.isEmpty()) doc.png = encodePng(doc.image);
    if (doc.candidates.isEmpty()) doc.candidates = detectBlocks(doc.image);

    if (replaceExisting || isImageSetProject_) {
        pages_.clear();
        groupNotes_ = QJsonArray();
        server_.clearImages();
        isImageSetProject_ = false;
    }

    const QString pageId = doc.id.isEmpty() ? uniqueId() : doc.id;
    B2Page page;
    page.id = pageId;
    page.name = doc.title.isEmpty() ? tr("屏幕截图 %1").arg(pages_.size() + 1) : doc.title;
    page.routeHint = doc.capture.value("windowTitle").toString();
    if (page.routeHint.isEmpty()) page.routeHint = tr("桌面捕获");
    page.document = std::move(doc);
    page.mediaUrl = server_.registerImage(pageId, page.document.image);

    pages_.append(std::move(page));
    activePageId_ = pageId;
    dirty_ = true;

    pushSessionState();
    show();
    raise();
    activateWindow();
}

void B2Window::openMediaPath(const QString &path) {
    const QFileInfo fi(path);
    if (isImageSetFile(path)) {
        ImageSet set = loadImageSet(path);
        pages_.clear();
        groupNotes_ = QJsonArray();
        server_.clearImages();
        isImageSetProject_ = true;
        setTitle_ = set.name;

        for (int i = 0; i < set.pages.size(); ++i) {
            auto setPage = set.pages[i];
            if (setPage.document.image.isNull()) continue;
            if (setPage.document.candidates.isEmpty()) {
                setPage.document.candidates = detectBlocks(setPage.document.image);
            }
            B2Page page;
            page.id = setPage.id.isEmpty() ? QString("page-%1").arg(i + 1) : setPage.id;
            page.name = setPage.name.isEmpty() ? page.id : setPage.name;
            page.routeHint = QFileInfo(setPage.document.imageFile).fileName();
            if (page.routeHint.isEmpty()) page.routeHint = page.id;
            page.document = std::move(setPage.document);
            page.mediaUrl = server_.registerImage(page.id, page.document.image);
            pages_.append(std::move(page));
        }

        int num = 0;
        for (const auto &v : set.groupNotes) {
            const auto obj = v.toObject();
            groupNotes_.append(QJsonObject{
                {"id", QString("g-%1").arg(++num)},
                {"number", obj.value("number").toInt(num)},
                {"text", obj.value("text").toString()},
            });
        }
        activePageId_ = pages_.isEmpty() ? QString() : pages_.first().id;
    } else {
        Document doc = loadDocument(path);
        if (doc.image.isNull()) throw std::runtime_error(tr("无法读取图片或项目文件").toStdString());
        if (doc.png.isEmpty()) doc.png = encodePng(doc.image);
        if (doc.candidates.isEmpty()) doc.candidates = detectBlocks(doc.image);

        pages_.clear();
        groupNotes_ = QJsonArray();
        server_.clearImages();
        isImageSetProject_ = false;
        setTitle_.clear();

        B2Page page;
        page.id = doc.id.isEmpty() ? uniqueId() : doc.id;
        page.name = doc.title.isEmpty() ? fi.completeBaseName() : doc.title;
        page.routeHint = fi.fileName();
        page.document = std::move(doc);
        page.mediaUrl = server_.registerImage(page.id, page.document.image);
        pages_.append(std::move(page));
        activePageId_ = pages_.first().id;
    }

    dirty_ = false;
    pushSessionState();
    show();
    raise();
    activateWindow();
}

void B2Window::setPageContext(const QString &pageContext) {
    pageContext_ = pageContext;
    if (!pages_.isEmpty() && !pageContext.isEmpty()) {
        for (auto &p : pages_) {
            if (p.routeHint.isEmpty()) p.routeHint = pageContext;
        }
    }
    pushSessionState();
}

void B2Window::setAgentSession(bool active, const QString &outputPath,
                               const FeedbackExportOptions &options) {
    agentSession_ = active;
    agentOutput_ = outputPath;
    agentExportOptions_ = options;
    pushSessionState();
}

void B2Window::setPreferences(const AppSettings &settings) {
    settings_ = settings;
    hasDarkOverride_ = false;
    syncDwmTitleBar(isEffectiveDark());
    pushSessionState();
}

void B2Window::navigateToView(const QString &viewId) {
    if (jsReady_) pushEvent("view.navigate", QJsonObject{{"view", viewId}});
}

void B2Window::triggerOpenDialog() {
    handleOpenFilesDialog(QJsonValue());
}

void B2Window::sendRpcResult(const QJsonValue &id, bool ok, const QJsonValue &data, const QString &error) {
    if (id.isUndefined() || id.isNull()) return;
    QJsonObject msg{{"type", "rpcResult"}, {"id", id}, {"ok", ok}};
    if (ok) msg["data"] = data;
    else msg["error"] = error;
    webview_->postJsonMessage(msg);
}

void B2Window::pushEvent(const QString &eventName, const QJsonObject &data) {
    webview_->postJsonMessage(QJsonObject{{"type", "event"}, {"event", eventName}, {"data", data}});
}

void B2Window::pushSessionState() {
    if (!jsReady_) return;
    pushEvent("session.state", buildSessionStateJson());
}

QJsonArray B2Window::blocksForPage(const B2Page &page) const {
    QJsonArray out;
    const QSize imgSize = page.document.image.size();
    const qint64 fullArea = std::max<qint64>(1, qint64(imgSize.width()) * imgSize.height());

    const auto &cands = page.document.candidates;
    int idx = 0;
    for (const auto &c : cands) {
        const QRect r = c.bounds.intersected(QRect(QPoint(0, 0), imgSize));
        if (r.width() < 14 || r.height() < 14) continue;
        const qint64 a = qint64(r.width()) * r.height();
        if (a * 10 > fullArea * 9) continue; // skip near-full background frame
        const QJsonObject rectObj = rectToJson(r);
        out.append(QJsonObject{
            {"id", QString("cand-%1-%2").arg(page.id).arg(++idx)},
            {"source", rectObj},
            {"dest", rectObj},
        });
        if (idx >= 18) break;
    }
    return out;
}

QJsonArray B2Window::buildPagesJson() const {
    QJsonArray arr;
    for (const auto &page : pages_) {
        QJsonArray annArr;
        int num = 0;
        for (const auto &note : page.document.notes) {
            ++num;
            QJsonObject a;
            a["id"] = note.id;
            a["number"] = num;
            a["kind"] = note.isGlobal ? "global" : (note.isPoint ? "point" : "region");
            a["text"] = note.comment;
            if (!note.isGlobal) {
                if (note.isPoint) {
                    a["point"] = QJsonObject{{"x", note.point.x()}, {"y", note.point.y()}};
                } else {
                    a["rect"] = rectToJson(note.rect);
                }
            }
            const QString ocrText = note.recognizedText.value("text").toString();
            if (!ocrText.isEmpty()) a["ocrtext"] = ocrText;
            annArr.append(a);
        }

        arr.append(QJsonObject{
            {"id", page.id},
            {"name", page.name},
            {"routeHint", page.routeHint},
            {"width", page.document.image.width()},
            {"height", page.document.image.height()},
            {"imageUrl", page.mediaUrl},
            {"isVideo", page.document.source == "video"},
            {"blocks", blocksForPage(page)},
            {"annotations", annArr},
        });
    }
    return arr;
}

QJsonObject B2Window::buildDiagnosticsJson() const {
    const QString dir = diagnostics::directory();
    return QJsonObject{
        {"directory", dir.isEmpty() ? QString() : QDir::toNativeSeparators(dir)},
        {"fileCount", int(diagnostics::logFiles().size())},
        {"lastError", diagnostics::lastError()},
    };
}

QJsonObject B2Window::buildPreferencesJson() const {
    QString startupError;
    const bool registeredLaunch = launchAtLoginEnabled(&startupError);
    const bool effectiveLaunch = startupError.isEmpty() ? registeredLaunch : settings_.launchAtLogin;
    const QString loginNotice = startupError.isEmpty() ? launchAtLoginNotice() : startupError;

    QJsonObject shortcutsObj;
    QJsonArray shortcutDefsArr;
    for (const auto &def : shortcutDefinitions()) {
        shortcutsObj.insert(def.id, settings_.shortcuts.value(def.id).toString(QKeySequence::PortableText));
        shortcutDefsArr.append(QJsonObject{
            {"id", def.id},
            {"label", def.label},
            {"global", def.global},
            {"defaultKey", def.defaultKey.toString(QKeySequence::PortableText)},
        });
    }

    QJsonArray trayActionsArr;
    for (const auto &def : trayActionDefinitions()) {
        trayActionsArr.append(QJsonObject{
            {"id", def.id},
            {"label", def.label},
        });
    }

    return QJsonObject{
        {"theme", themeModeToString(settings_.theme)},
        {"language", languageModeToString(settings_.language)},
        {"ocrLanguage", ocrLanguageModeToString(settings_.ocrLanguage)},
        {"captureOnStartup", settings_.captureOnStartup},
        {"launchAtLogin", effectiveLaunch},
        {"launchAtLoginNotice", loginNotice},
        {"fitImageOnOpen", settings_.fitImageOnOpen},
        {"confirmBeforeDiscard", settings_.confirmBeforeDiscard},
        {"defaultTool", settings_.defaultTool},
        {"traySingleClick", settings_.traySingleClick},
        {"trayDoubleClick", settings_.trayDoubleClick},
        {"trayActions", trayActionsArr},
        {"includeCoordinates", true},
        {"includeOcr", settings_.regionOcrEnabled},
        {"includeRecognizedText", settings_.includeRecognizedText},
        {"includeWindowTitle", settings_.includeWindowTitle},
        {"exportMode", exportModeToString(settings_.imageExportMode)},
        {"copyJsonTextWithImage", settings_.copyJsonTextWithImage},
        {"captureShadow", settings_.captureStyle.shadow},
        {"captureCornerRadius", settings_.captureStyle.cornerRadius},
        {"feedbackDir", settings_.feedbackDir},
        {"scrollAxis", settings_.scrollAxis == Qt::Horizontal ? "horizontal" : "vertical"},
        {"scrollAutoCrop", settings_.scrollAutoCrop},
        {"scrollUltraLong", settings_.scrollUltraLong},
        {"checkUpdatesOnStartup", settings_.checkUpdatesOnStartup},
        {"updateSource", settings_.updateSource},
        {"shortcuts", shortcutsObj},
        {"shortcutDefinitions", shortcutDefsArr},
    };
}

QJsonObject B2Window::buildDoctorSnapshotJson() const {
    const QString wvVer = webview_->runtimeVersion().isEmpty() ? tr("内置运行") : webview_->runtimeVersion();
    const QString cliPath = []() {
        const QDir appDir(QCoreApplication::applicationDirPath());
        QString resolved = QDir::cleanPath(appDir.filePath("cli/unified-mcp.mjs"));
        if (!QFileInfo::exists(resolved)) {
            const QString devPath = QDir::cleanPath(appDir.filePath("../cli/unified-mcp.mjs"));
            if (QFileInfo::exists(devPath)) {
                resolved = devPath;
            }
        }
        return QDir::fromNativeSeparators(resolved);
    }();
    const QJsonObject snippetObj{
        {"mcpServers", QJsonObject{
            {"edithere", QJsonObject{
                {"command", "node"},
                {"args", QJsonArray{cliPath}},
            }},
        }},
    };

    return QJsonObject{
        {"activeRoute", "desktop"},
        {"desktopVersion", QString("%1 (Qt6 + WebView2 %2)").arg(EDITHERE_VERSION, wvVer)},
        {"nodeVersion", "Desktop Native"},
        {"unifiedMcpSnippet", QString::fromUtf8(QJsonDocument(snippetObj).toJson(QJsonDocument::Indented))},
        {"checks", QJsonArray{
            QJsonObject{
                {"id", "core"},
                {"label", tr("原生截图 / HDR / 长截图核心")},
                {"detail", tr("C++20 + Qt6 已就绪（Windows DXGI HDR 与零延迟遮罩已激活）")},
                {"status", "ok"},
            },
            QJsonObject{
                {"id", "bridge"},
                {"label", tr("WebView2 系统容器桥接")},
                {"detail", tr("Microsoft Edge WebView2 %1 已连接（%2）").arg(wvVer, QCoreApplication::applicationFilePath())},
                {"status", "ok"},
            },
            QJsonObject{
                {"id", "ocr"},
                {"label", tr("Windows 本机离线 OCR")},
                {"detail", OcrEngine::supported() ? tr("Windows WinRT OCR 可用") : tr("未检测到系统语言 OCR 引擎")},
                {"status", OcrEngine::supported() ? "ok" : "warn"},
            },
            QJsonObject{
                {"id", "loopback"},
                {"label", tr("本地回环资产与媒体服务")},
                {"detail", tr("127.0.0.1:%1 · 零磁盘复制直传无损 PNG").arg(server_.port())},
                {"status", server_.isRunning() ? "ok" : "warn"},
            },
        }},
    };
}

QJsonObject B2Window::buildSessionStateJson() const {
    return QJsonObject{
        {"mode", "desktop-webview"},
        {"version", EDITHERE_VERSION},
        {"runtimeVersion", webview_->runtimeVersion()},
        {"agentMode", agentSession_},
        {"hasDesktopPages", !pages_.isEmpty()},
        {"activePageId", activePageId_},
        {"pages", buildPagesJson()},
        {"groupNotes", groupNotes_},
        {"preferences", buildPreferencesJson()},
        {"diagnostics", buildDiagnosticsJson()},
        {"doctor", buildDoctorSnapshotJson()},
        {"ai", buildAiStateJson()},
    };
}

void B2Window::preloadAiCredentials() {
    if (!aiCredentials_) return;
    QSet<QString> ids;
    for (const auto &preset : aiProviderPresets()) {
        ids.insert(preset.id);
    }
    for (auto it = settings_.aiProviders.cbegin(); it != settings_.aiProviders.cend(); ++it) {
        ids.insert(it.key());
    }
    for (const QString &id : ids) {
        aiCredentials_->load(id);
    }
}

QJsonObject B2Window::buildAiStateJson() const {
    QJsonArray providersArr;
    QSet<QString> seen;

    const auto toJsonEntry = [&](const QString &id, const QString &presetId, const QString &name,
                                 const AiConnectionSettings &conn) {
        const QString key = apiKeys_.value(id);
        return QJsonObject{
            {"id", id},
            {"presetId", presetId},
            {"name", name},
            {"model", conn.model},
            {"endpoint", conn.baseUrl},
            {"protocol", conn.protocol},
            {"key", ""},
            {"keyPreview", maskSecretKey(key)},
            {"region", "cn"},
            {"timeout", QString::number(conn.timeoutSeconds)},
            {"note", conn.extraInstructions},
            {"saved", !key.isEmpty()},
        };
    };

    for (const auto &preset : aiProviderPresets()) {
        if (preset.id == "custom") continue;
        seen.insert(preset.id);
        AiConnectionSettings conn = settings_.aiProviders.value(preset.id, defaultAiConnection(preset.id));
        if (conn.baseUrl.isEmpty()) conn.baseUrl = preset.baseUrl;
        if (conn.model.isEmpty()) conn.model = preset.model;
        if (conn.protocol.isEmpty()) conn.protocol = preset.protocol;
        providersArr.append(toJsonEntry(preset.id, preset.id, preset.name, conn));
    }

    for (auto it = settings_.aiProviders.cbegin(); it != settings_.aiProviders.cend(); ++it) {
        if (seen.contains(it.key())) continue;
        providersArr.append(toJsonEntry(it.key(), "custom", it.key(), it.value()));
    }

    return QJsonObject{
        {"defaultProviderId", settings_.aiProviderId},
        {"systemPrompt", settings_.aiSystemPrompt},
        {"providers", providersArr},
    };
}

void B2Window::handleWebMessage(const QJsonObject &message) {
    if (message.value("type").toString() != "rpc") return;
    const QJsonValue id = message.value("id");
    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    if (method == "bridge.handshake") {
        jsReady_ = true;
        diagnostics::write(diagnostics::Level::Info, "b2.bridge_handshake",
                           "WebView2 JS bridge connected",
                           {{"pages", pages_.size()}, {"agentMode", agentSession_}});
        sendRpcResult(id, true, buildSessionStateJson());
        return;
    }

    if (method == "capture.start") {
        hide();
        QTimer::singleShot(140, this, [this]() { emit captureRequested(); });
        sendRpcResult(id, true, QJsonObject{{"started", true}});
        return;
    }

    if (method == "file.openDialog") {
        handleOpenFilesDialog(id);
        return;
    }

    if (method == "detect.regions") {
        const QString pageId = params.value("pageId").toString();
        for (auto &page : pages_) {
            if (page.id == pageId) {
                if (page.document.candidates.isEmpty()) {
                    page.document.candidates = detectBlocks(page.document.image);
                }
                sendRpcResult(id, true, QJsonObject{{"blocks", blocksForPage(page)}});
                return;
            }
        }
        sendRpcResult(id, true, QJsonObject{{"blocks", QJsonArray()}});
        return;
    }

    if (method == "ocr.recognizeRect") {
        handleOcrRect(id, params);
        return;
    }

    if (method == "pin.page") {
        const QString b64 = params.value("pngBase64").toString();
        if (!b64.isEmpty()) {
            const QByteArray bytes = QByteArray::fromBase64(b64.toLatin1());
            QImage rendered;
            if (rendered.loadFromData(bytes, "PNG") && !rendered.isNull()) {
                emit pinImageRequested(rendered);
                sendRpcResult(id, true, QJsonObject{{"pinned", true}});
                return;
            }
        }
        const QString pageId = params.value("pageId").toString();
        for (const auto &page : pages_) {
            if (page.id == pageId && !page.document.image.isNull()) {
                emit pinImageRequested(page.document.image);
                sendRpcResult(id, true, QJsonObject{{"pinned", true}});
                return;
            }
        }
        sendRpcResult(id, false, {}, tr("请先截图或导入图片后再贴图。"));
        return;
    }

    if (method == "image.updatePage") {
        handleUpdatePageImage(id, params);
        return;
    }

    if (method == "clipboard.copyImage") {
        handleCopyImage(id, params);
        return;
    }

    if (method == "file.saveImage") {
        handleSaveImageDialog(id, params);
        return;
    }

    if (method == "file.saveProject") {
        handleSaveProjectDialog(id, params);
        return;
    }

    if (method == "clipboard.copyFeedbackBundle") {
        handleCopyFeedbackBundle(id, params);
        return;
    }

    if (method == "clipboard.copyText") {
        const QString text = params.value("text").toString();
        QGuiApplication::clipboard()->setText(text);
        sendRpcResult(id, true, QJsonObject{{"copied", true}});
        return;
    }

    if (method == "file.saveText") {
        handleSaveTextDialog(id, params);
        return;
    }

    if (method == "theme.sync") {
        uiDarkOverride_ = params.value("isDark").toBool();
        hasDarkOverride_ = true;
        const QString modeStr = params.value("theme").toString();
        if (!modeStr.isEmpty()) {
            settings_.theme = stringToThemeMode(modeStr);
        }
        syncDwmTitleBar(uiDarkOverride_);
        sendRpcResult(id, true, QJsonObject{{"dark", uiDarkOverride_}});
        return;
    }

    if (method == "settings.save") {
        handleSaveSettings(id, params);
        return;
    }

    if (method == "settings.resetShortcuts") {
        AppSettings next = settings_;
        next.shortcuts = defaultSettings().shortcuts;
        QString err = settingsApplyHandler_ ? settingsApplyHandler_(next) : validateSettings(next);
        if (err.isEmpty() && !settingsApplyHandler_) saveSettings(next, &err, settingsFile_);
        if (!err.isEmpty()) {
            sendRpcResult(id, false, {}, err);
            return;
        }
        settings_ = next;
        sendRpcResult(id, true, buildPreferencesJson());
        return;
    }

    if (method == "settings.resetDefaults") {
        AppSettings next = defaultSettings();
        next.aiProviderId = settings_.aiProviderId;
        next.aiProviders = settings_.aiProviders;
        next.aiSystemPrompt = settings_.aiSystemPrompt;
        QString err = settingsApplyHandler_ ? settingsApplyHandler_(next) : validateSettings(next);
        if (err.isEmpty() && !settingsApplyHandler_) saveSettings(next, &err, settingsFile_);
        if (!err.isEmpty()) {
            sendRpcResult(id, false, {}, err);
            return;
        }
        settings_ = next;
        hasDarkOverride_ = false;
        syncDwmTitleBar(isEffectiveDark());
        sendRpcResult(id, true, buildPreferencesJson());
        return;
    }

    if (method == "settings.chooseFeedbackDir") {
        const QString picked = QFileDialog::getExistingDirectory(
            this, tr("选择反馈临时目录"),
            settings_.feedbackDir.isEmpty() ? QString() : settings_.feedbackDir);
        if (picked.isEmpty()) {
            sendRpcResult(id, true, QJsonObject{{"cancelled", true}});
            return;
        }
        sendRpcResult(id, true, QJsonObject{{"cancelled", false}, {"path", QDir::toNativeSeparators(picked)}});
        return;
    }

    if (method == "diagnostics.getInfo") {
        sendRpcResult(id, true, buildDiagnosticsJson());
        return;
    }

    if (method == "diagnostics.export") {
        handleDiagnosticsExport(id);
        return;
    }

    if (method == "diagnostics.openFolder") {
        handleDiagnosticsOpenFolder(id);
        return;
    }

    if (method == "updates.check") {
        handleUpdatesCheck(id);
        return;
    }

    if (method == "updates.openReleases") {
        const QString requested = params.value("url").toString().trimmed();
        const QUrl url = requested.startsWith("https://", Qt::CaseInsensitive)
                             ? QUrl(requested)
                             : UpdateChecker::releasesUrl(UpdateChecker::preferredSource(settings_.updateSource));
        const bool opened = QDesktopServices::openUrl(url);
        sendRpcResult(id, opened, QJsonObject{{"opened", opened}},
                      opened ? QString() : tr("无法打开浏览器，请访问：%1").arg(url.toString()));
        return;
    }

    if (method == "shell.openExternal") {
        const QUrl url(params.value("url").toString().trimmed());
        if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
            sendRpcResult(id, false, {}, tr("仅允许打开 HTTPS 链接。"));
            return;
        }
        const bool opened = QDesktopServices::openUrl(url);
        sendRpcResult(id, opened, QJsonObject{{"opened", opened}},
                      opened ? QString() : tr("无法打开浏览器，请访问：%1").arg(url.toString()));
        return;
    }

    if (method == "agent.complete") {
        handleAgentComplete(id, params);
        return;
    }

    if (method == "agent.cancel") {
        agentSession_ = false;
        agentOutput_.clear();
        emit agentSessionCancelled();
        sendRpcResult(id, true, QJsonObject{{"cancelled", true}});
        return;
    }

    if (method == "ai.getState") {
        sendRpcResult(id, true, buildAiStateJson());
        return;
    }

    if (method == "ai.saveProviders") {
        handleAiSaveProviders(id, params);
        return;
    }

    if (method == "ai.testConnection") {
        handleAiTestConnection(id, params);
        return;
    }

    if (method == "ai.analyze") {
        handleAiAnalyze(id, params);
        return;
    }

    if (method == "ai.cancel") {
        if (aiClient_) aiClient_->cancel();
        sendRpcResult(id, true, QJsonObject{{"cancelled", true}});
        return;
    }

    sendRpcResult(id, false, {}, QString("Unsupported RPC method: %1").arg(method));
}

void B2Window::handleOpenFilesDialog(const QJsonValue &rpcId) {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("打开截图或 EditHere 项目"), QString(),
        tr("图片与项目 (*.png *.jpg *.jpeg *.webp *.bmp *.json *.edithere);;所有文件 (*.*)"));
    if (files.isEmpty()) {
        sendRpcResult(rpcId, true, QJsonObject{{"cancelled", true}});
        return;
    }

    try {
        if (files.size() == 1 && (isImageSetFile(files.first()) || files.first().endsWith(".edithere", Qt::CaseInsensitive))) {
            openMediaPath(files.first());
        } else {
            for (const QString &file : files) {
                Document doc = loadDocument(file);
                if (doc.image.isNull()) continue;
                if (doc.png.isEmpty()) doc.png = encodePng(doc.image);
                if (doc.candidates.isEmpty()) doc.candidates = detectBlocks(doc.image);

                B2Page page;
                page.id = doc.id.isEmpty() ? uniqueId() : doc.id;
                const QFileInfo fi(file);
                page.name = doc.title.isEmpty() ? fi.completeBaseName() : doc.title;
                page.routeHint = fi.fileName();
                page.document = std::move(doc);
                page.mediaUrl = server_.registerImage(page.id, page.document.image);
                pages_.append(page);
                activePageId_ = page.id;
            }
            if (pages_.size() > 1) isImageSetProject_ = true;
            dirty_ = true;
            pushSessionState();
        }
        sendRpcResult(rpcId, true, QJsonObject{
            {"opened", pages_.size()},
            {"pages", buildPagesJson()},
            {"activePageId", activePageId_},
        });
    } catch (const std::exception &e) {
        sendRpcResult(rpcId, false, {}, QString::fromUtf8(e.what()));
    }
}

void B2Window::handleSaveTextDialog(const QJsonValue &rpcId, const QJsonObject &params) {
    const QString suggested = params.value("defaultName").toString("edithere-output.txt");
    const QString text = params.value("text").toString();
    const QString dest = QFileDialog::getSaveFileName(this, tr("导出文本文件"), suggested, tr("全部文件 (*.*)"));
    if (dest.isEmpty()) {
        sendRpcResult(rpcId, true, QJsonObject{{"saved", false}});
        return;
    }
    QFile file(dest);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendRpcResult(rpcId, false, {}, file.errorString());
        return;
    }
    file.write(text.toUtf8());
    file.close();
    sendRpcResult(rpcId, true, QJsonObject{{"saved", true}, {"path", dest}});
}

void B2Window::handleOcrRect(const QJsonValue &rpcId, const QJsonObject &params) {
    if (!settings_.regionOcrEnabled || !OcrEngine::supported()) {
        sendRpcResult(rpcId, true, QJsonObject{{"ok", false}, {"text", ""}});
        return;
    }
    const QString pageId = params.value("pageId").toString();
    const QJsonObject r = params.value("rect").toObject();
    const B2Page *targetPage = nullptr;
    for (const auto &p : pages_) {
        if (p.id == pageId) {
            targetPage = &p;
            break;
        }
    }
    if (!targetPage || targetPage->document.image.isNull()) {
        sendRpcResult(rpcId, true, QJsonObject{{"ok", false}, {"text", ""}});
        return;
    }

    const QRect rect = QRect(
        QPoint(r.value("x1").toInt(), r.value("y1").toInt()),
        QPoint(r.value("x2").toInt() - 1, r.value("y2").toInt() - 1)).normalized()
        .intersected(QRect(QPoint(0, 0), targetPage->document.image.size()));
    if (rect.width() < 4 || rect.height() < 4) {
        sendRpcResult(rpcId, true, QJsonObject{{"ok", false}, {"text", ""}});
        return;
    }

    QImage crop = targetPage->document.image.copy(rect);
    const QJsonArray mosaics = params.value("mosaics").toArray();
    if (!mosaics.isEmpty()) {
        QPainter maskPainter(&crop);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(QColor(18, 20, 26));
        for (const auto &mv : mosaics) {
            const QJsonObject mo = mv.toObject();
            const QRect mr = QRect(
                QPoint(mo.value("x1").toInt(), mo.value("y1").toInt()),
                QPoint(mo.value("x2").toInt() - 1, mo.value("y2").toInt() - 1)).normalized();
            const QRect overlap = mr.intersected(rect);
            if (!overlap.isEmpty()) {
                maskPainter.drawRect(overlap.translated(-rect.topLeft()));
            }
        }
    }
    if (ocr_->busy()) ocr_->cancel();
    ocr_->recognize(crop, settings_.ocrLanguage, [this, rpcId](OcrResult result) {
        sendRpcResult(rpcId, true, QJsonObject{
            {"ok", result.ok},
            {"text", result.text().trimmed()},
            {"language", result.engineLanguage},
        });
    });
}

Document B2Window::buildDocumentFromJsPage(const B2Page &basePage, const QJsonObject &jsPage) const {
    Document doc = basePage.document;
    const QString redactedB64 = jsPage.value("redactedPngBase64").toString();
    if (!redactedB64.isEmpty()) {
        const QByteArray bytes = QByteArray::fromBase64(redactedB64.toLatin1());
        QImage redacted;
        if (!bytes.isEmpty() && redacted.loadFromData(bytes, "PNG") && !redacted.isNull()) {
            doc.image = redacted;
            doc.png = bytes;
        }
    }
    doc.notes.clear();
    doc.layout.reset();

    QVector<LayoutSequence> sequences;
    QSet<QString> seenSequenceSources;
    const QSizeF imgBoundsF(doc.image.size());
    const QJsonArray anns = jsPage.value("annotations").toArray();
    for (const auto &val : anns) {
        const QJsonObject ann = val.toObject();
        Note note;
        note.id = ann.value("id").toString();
        if (note.id.isEmpty()) note.id = uniqueId();
        note.comment = ann.value("text").toString();
        const QString ocrText = ann.value("ocrtext").toString();
        if (!ocrText.isEmpty()) note.recognizedText = QJsonObject{{"text", ocrText}};

        const QString kind = ann.value("kind").toString("region");
        if (kind == "global") {
            note.isGlobal = true;
            note.isPoint = false;
        } else if (kind == "point") {
            note.isPoint = true;
            const QJsonObject pt = ann.value("point").toObject();
            note.point = QPoint(pt.value("x").toInt(), pt.value("y").toInt());
        } else {
            note.isPoint = false;
            const QJsonObject r = ann.value("rect").toObject();
            const int x1 = r.value("x1").toInt();
            const int y1 = r.value("y1").toInt();
            const int x2 = std::max(x1 + 1, r.value("x2").toInt());
            const int y2 = std::max(y1 + 1, r.value("y2").toInt());
            note.rect = QRect(x1, y1, x2 - x1, y2 - y1)
                            .intersected(QRect(QPoint(0, 0), doc.image.size()));
            if (note.rect.isEmpty()) continue;

            if (ann.contains("movement") && ann.value("movement").isObject() && !doc.image.isNull()) {
                const QJsonObject mv = ann.value("movement").toObject();
                const QJsonObject srcObj = mv.value("source").toObject();
                const QJsonObject dstObj = mv.value("rect").toObject();
                const QRectF canvasF(QPointF(0, 0), imgBoundsF);
                const QRectF srcRect = QRectF(
                    srcObj.value("x1").toDouble(), srcObj.value("y1").toDouble(),
                    std::max(1.0, srcObj.value("x2").toDouble() - srcObj.value("x1").toDouble()),
                    std::max(1.0, srcObj.value("y2").toDouble() - srcObj.value("y1").toDouble()))
                    .intersected(canvasF);
                const QRectF dstRect = QRectF(
                    dstObj.value("x1").toDouble(), dstObj.value("y1").toDouble(),
                    std::max(1.0, dstObj.value("x2").toDouble() - dstObj.value("x1").toDouble()),
                    std::max(1.0, dstObj.value("y2").toDouble() - dstObj.value("y1").toDouble()))
                    .intersected(canvasF);
                if (srcRect.width() >= 1.0 && srcRect.height() >= 1.0 &&
                    dstRect.width() >= 1.0 && dstRect.height() >= 1.0) {
                    note.movementSource = srcRect;
                    note.rect = dstRect.toAlignedRect();
                    const QString srcKey = QString("%1,%2,%3,%4")
                                               .arg(srcRect.x()).arg(srcRect.y())
                                               .arg(srcRect.width()).arg(srcRect.height());
                    if (!seenSequenceSources.contains(srcKey)) {
                        seenSequenceSources.insert(srcKey);
                        sequences.append(LayoutSequence{srcRect, {dstRect}});
                    }
                }
            }
        }
        doc.notes.append(std::move(note));
    }

    if (!sequences.isEmpty() && !doc.image.isNull()) {
        doc.layout = importLayoutSequences(sequences, doc.image.size());
    }
    return doc;
}

ImageSet B2Window::buildImageSetFromPayload(const QJsonObject &payload) const {
    const QJsonArray jsPages = payload.value("pages").toArray();
    const QJsonArray jsGroupNotes = payload.value("groupNotes").toArray();
    ImageSet set;
    set.name = setTitle_.isEmpty() ? tr("EditHere 组图") : setTitle_;
    for (const auto &gnVal : jsGroupNotes) {
        const auto obj = gnVal.toObject();
        const QString text = obj.value("text").toString().trimmed();
        if (!text.isEmpty()) set.groupNotes.append(QJsonObject{{"text", text}});
    }
    for (const auto &basePage : pages_) {
        QJsonObject matchedJs;
        for (const auto &val : jsPages) {
            const auto obj = val.toObject();
            if (obj.value("id").toString() == basePage.id) {
                matchedJs = obj;
                break;
            }
        }
        ImageSetPage sp;
        sp.id = basePage.id;
        sp.name = basePage.name;
        sp.document = buildDocumentFromJsPage(basePage, matchedJs);
        set.pages.append(std::move(sp));
    }
    return set;
}

void B2Window::buildBundleFromPayload(const QJsonObject &payload, const FeedbackExportOptions &options,
                                      FeedbackBundle *outBundle, int *outCount, QString *outSchema) const {
    if (pages_.isEmpty()) {
        throw std::runtime_error(tr("当前没有可导出的桌面截图页面。").toStdString());
    }
    if (isImageSetProject_ || pages_.size() > 1) {
        const ImageSet set = buildImageSetFromPayload(payload);
        if (outCount) *outCount = imageSetAnnotationCount(set);
        if (outSchema) *outSchema = ImageSetFeedbackVersion;
        if (outBundle) *outBundle = buildImageSetFeedbackBundle(set, options);
        return;
    }
    const QJsonArray jsPages = payload.value("pages").toArray();
    const QJsonObject firstJs = jsPages.isEmpty() ? QJsonObject() : jsPages.first().toObject();
    const Document doc = buildDocumentFromJsPage(pages_.first(), firstJs);
    if (outCount) *outCount = doc.notes.size();
    if (outSchema) *outSchema = FeedbackSchemaVersion;
    if (outBundle) *outBundle = buildFeedbackBundle(doc, options);
}

void B2Window::handleUpdatePageImage(const QJsonValue &rpcId, const QJsonObject &params) {
    const QString pageId = params.value("pageId").toString();
    const QString b64 = params.value("pngBase64").toString();
    const QByteArray bytes = QByteArray::fromBase64(b64.toLatin1());
    QImage img;
    if (bytes.isEmpty() || !img.loadFromData(bytes, "PNG") || img.isNull() ||
        qint64(img.width()) * img.height() > MaxPixels) {
        sendRpcResult(rpcId, false, {}, tr("无效的图像数据。"));
        return;
    }
    for (auto &page : pages_) {
        if (page.id == pageId) {
            page.document.image = img;
            page.document.png = bytes;
            page.document.candidates = detectBlocks(page.document.image);
            page.document.notes.clear();
            page.document.layout.reset();
            page.mediaUrl = server_.registerImage(page.id, page.document.image);
            dirty_ = true;
            sendRpcResult(rpcId, true, QJsonObject{
                {"ok", true},
                {"mediaUrl", page.mediaUrl},
                {"width", img.width()},
                {"height", img.height()},
                {"blocks", blocksForPage(page)},
            });
            return;
        }
    }
    sendRpcResult(rpcId, false, {}, tr("未找到对应页面。"));
}

void B2Window::handleCopyImage(const QJsonValue &rpcId, const QJsonObject &params) {
    const QByteArray bytes = QByteArray::fromBase64(params.value("pngBase64").toString().toLatin1());
    QImage img;
    if (bytes.isEmpty() || !img.loadFromData(bytes, "PNG") || img.isNull()) {
        sendRpcResult(rpcId, false, {}, tr("图像编码失败。"));
        return;
    }
    QGuiApplication::clipboard()->setImage(img);
    sendRpcResult(rpcId, true, QJsonObject{{"copied", true}});
}

void B2Window::handleSaveImageDialog(const QJsonValue &rpcId, const QJsonObject &params) {
    const QString suggested = params.value("defaultName").toString("edithere-annotated.png");
    const QByteArray bytes = QByteArray::fromBase64(params.value("pngBase64").toString().toLatin1());
    if (bytes.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("没有可保存的图片内容。"));
        return;
    }
    const QString dest = QFileDialog::getSaveFileName(
        this, tr("保存批注图片"), suggested, tr("PNG 图片 (*.png);;所有文件 (*.*)"));
    if (dest.isEmpty()) {
        sendRpcResult(rpcId, true, QJsonObject{{"saved", false}});
        return;
    }
    try {
        saveBytes(dest, bytes);
        sendRpcResult(rpcId, true, QJsonObject{{"saved", true}, {"path", dest}});
    } catch (const std::exception &e) {
        sendRpcResult(rpcId, false, {}, QString::fromUtf8(e.what()));
    }
}

void B2Window::handleSaveProjectDialog(const QJsonValue &rpcId, const QJsonObject &params) {
    if (pages_.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("请先截图或打开图片后再保存项目。"));
        return;
    }
    const QString suggested = params.value("defaultName").toString("edithere-project.edithere");
    const QString dest = QFileDialog::getSaveFileName(
        this, tr("保存 EditHere 项目"), suggested,
        tr("EditHere 项目 (*.edithere);;JSON 文件 (*.json);;所有文件 (*.*)"));
    if (dest.isEmpty()) {
        sendRpcResult(rpcId, true, QJsonObject{{"saved", false}});
        return;
    }
    try {
        const QJsonObject payload = params.value("payload").toObject();
        QByteArray bytes;
        if (isImageSetProject_ || pages_.size() > 1) {
            bytes = serializeImageSet(buildImageSetFromPayload(payload));
        } else {
            const QJsonArray jsPages = payload.value("pages").toArray();
            const QJsonObject firstJs = jsPages.isEmpty() ? QJsonObject() : jsPages.first().toObject();
            const Document doc = buildDocumentFromJsPage(pages_.first(), firstJs);
            bytes = serializeDocument(doc, true);
        }
        saveBytes(dest, bytes);
        dirty_ = false;
        sendRpcResult(rpcId, true, QJsonObject{{"saved", true}, {"path", dest}});
    } catch (const std::exception &e) {
        sendRpcResult(rpcId, false, {}, QString::fromUtf8(e.what()));
    }
}

void B2Window::handleCopyFeedbackBundle(const QJsonValue &rpcId, const QJsonObject &params) {
    try {
        const QJsonObject payload = params.value("payload").toObject();
        FeedbackExportOptions options;
        options.mode = settings_.imageExportMode == ImageExportMode::None ? ImageExportMode::File
                                                                           : settings_.imageExportMode;
        options.includeRecognizedText = settings_.regionOcrEnabled;
        options.includeWindowTitle = settings_.includeWindowTitle;
        FeedbackBundle bundle;
        int total = 0;
        QString schema;
        buildBundleFromPayload(payload, options, &bundle, &total, &schema);

        const QString cacheRoot =
            settings_.feedbackDir.isEmpty()
                ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("feedback")
                : settings_.feedbackDir;
        QDir().mkpath(cacheRoot);
        const QString folderName = "edithere-b2-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                                   "-" + uniqueId();
        const QDir parent(cacheRoot);
        if (!parent.mkdir(folderName)) {
            throw std::runtime_error("Unable to create temporary feedback bundle directory.");
        }
        const QDir bundleDir(parent.filePath(folderName));
        const QString jsonPath = bundleDir.filePath("feedback.json");
        writeNewFeedbackBundle(jsonPath, bundle);

        QList<QUrl> urls{QUrl::fromLocalFile(jsonPath)};
        for (auto it = bundle.files.cbegin(); it != bundle.files.cend(); ++it) {
            urls.append(QUrl::fromLocalFile(bundleDir.filePath(it.key())));
        }
        auto *mime = new QMimeData();
        mime->setUrls(urls);
        QGuiApplication::clipboard()->setMimeData(mime);
        sendRpcResult(rpcId, true, QJsonObject{{"copied", true}, {"files", int(urls.size())}, {"annotations", total}});
    } catch (const std::exception &e) {
        sendRpcResult(rpcId, false, {}, QString::fromUtf8(e.what()));
    }
}

void B2Window::handleAgentComplete(const QJsonValue &rpcId, const QJsonObject &params) {
    if (!agentSession_ || agentOutput_.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("当前没有等待返回的 AI 会话。"));
        return;
    }

    const QJsonObject payload = params.value("payload").toObject();

    try {
        FeedbackBundle bundle;
        int totalAnnotations = 0;
        QString schemaVersion = FeedbackSchemaVersion;
        buildBundleFromPayload(payload, agentExportOptions_, &bundle, &totalAnnotations, &schemaVersion);

        const QString completedOutput = agentOutput_;
        writeNewFeedbackBundle(completedOutput, bundle);
        dirty_ = false;
        agentSession_ = false;
        agentOutput_.clear();

        diagnostics::write(diagnostics::Level::Info, "b2.agent_feedback_written",
                           "B2 desktop feedback committed",
                           {{"annotations", totalAnnotations}, {"schema", schemaVersion}});
        pushSessionState();
        emit agentSessionCompleted(completedOutput, totalAnnotations, schemaVersion,
                                   bundle.imageFile, bundle.previewFile);
        sendRpcResult(rpcId, true, QJsonObject{
            {"output", completedOutput},
            {"annotations", totalAnnotations},
            {"schemaVersion", schemaVersion},
        });
    } catch (const std::exception &e) {
        const QString err = QString::fromUtf8(e.what());
        diagnostics::write(diagnostics::Level::Error, "b2.agent_complete_failed", err);
        sendRpcResult(rpcId, false, {}, err);
    }
}

void B2Window::handleAiSaveProviders(const QJsonValue &rpcId, const QJsonObject &params) {
    if (params.contains("defaultProviderId")) {
        const QString defId = sanitizeProviderId(params.value("defaultProviderId").toString());
        if (!defId.isEmpty()) settings_.aiProviderId = defId;
    }
    if (params.contains("systemPrompt")) {
        settings_.aiSystemPrompt = params.value("systemPrompt").toString();
    }

    if (params.contains("providers") && params.value("providers").isArray()) {
        QMap<QString, AiConnectionSettings> nextMap;
        const QJsonArray arr = params.value("providers").toArray();
        for (const auto &val : arr) {
            const QJsonObject obj = val.toObject();
            const QString id = sanitizeProviderId(obj.value("id").toString());
            if (id.isEmpty()) continue;

            AiConnectionSettings conn = connectionFromJson(obj, id);
            nextMap.insert(id, conn);

            if (obj.value("keyEdited").toBool() && aiCredentials_) {
                const QString nextKey = obj.value("key").toString().trimmed();
                if (nextKey.isEmpty()) {
                    apiKeys_.remove(id);
                    aiCredentials_->remove(id);
                } else {
                    apiKeys_[id] = nextKey;
                    aiCredentials_->save(id, nextKey);
                }
            }
        }
        settings_.aiProviders = std::move(nextMap);
    }

    QString saveErr;
    if (!saveSettings(settings_, &saveErr, settingsFile_)) {
        sendRpcResult(rpcId, false, {}, saveErr);
        return;
    }
    emit preferencesUpdated(settings_);
    sendRpcResult(rpcId, true, buildAiStateJson());
}

void B2Window::handleAiTestConnection(const QJsonValue &rpcId, const QJsonObject &params) {
    const QString providerId = sanitizeProviderId(params.value("id").toString(settings_.aiProviderId));
    AiConnectionSettings conn = connectionFromJson(params, providerId);
    const QString validation = validateAiConnection(conn, false);
    if (!validation.isEmpty()) {
        sendRpcResult(rpcId, false, {}, validation);
        return;
    }
    QString key = params.value("key").toString().trimmed();
    if (key.isEmpty() || key.contains(QStringLiteral("••••"))) {
        key = apiKeys_.value(providerId);
    }
    if (key.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("请先填写或保存有效的 API Key。"));
        return;
    }
    if (aiClient_->isRunning()) {
        sendRpcResult(rpcId, false, {}, tr("当前已有 AI 请求正在运行，请等待完成或先取消。"));
        return;
    }
    pendingAiAnalyzeId_ = QJsonValue();
    pendingAiTestId_ = rpcId;
    aiClient_->testConnection(conn, key);
}

void B2Window::handleAiAnalyze(const QJsonValue &rpcId, const QJsonObject &params) {
    if (pages_.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("当前为示例模式，请先「屏幕截图」或导入本地图片后再执行真实 AI 解析。"));
        return;
    }
    const QString providerId = sanitizeProviderId(params.value("providerId").toString(settings_.aiProviderId));
    AiConnectionSettings conn = settings_.aiProviders.value(providerId, defaultAiConnection(providerId));
    if (params.contains("provider") && params.value("provider").isObject()) {
        conn = connectionFromJson(params.value("provider").toObject(), providerId);
    }
    const QString validation = validateAiConnection(conn, true);
    if (!validation.isEmpty()) {
        sendRpcResult(rpcId, false, {}, validation);
        return;
    }
    const QString key = apiKeys_.value(providerId);
    if (key.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("尚未配置「%1」的 API Key，请前往 设置 → 模型服务 填写并保存。").arg(providerId));
        return;
    }
    if (aiClient_->isRunning()) {
        sendRpcResult(rpcId, false, {}, tr("已有 AI 解析请求正在运行，请先等待或取消。"));
        return;
    }

    try {
        const QJsonObject payload = params.value("payload").toObject();
        FeedbackExportOptions options;
        options.mode = ImageExportMode::None;
        options.includeRecognizedText = settings_.regionOcrEnabled;
        options.includeWindowTitle = settings_.includeWindowTitle;

        AiFeedbackInput input;
        if (isImageSetProject_ || pages_.size() > 1) {
            const ImageSet set = buildImageSetFromPayload(payload);
            input = aiFeedbackSnapshot(set, options);
        } else {
            const QJsonArray jsPages = payload.value("pages").toArray();
            const QJsonObject firstJs = jsPages.isEmpty() ? QJsonObject() : jsPages.first().toObject();
            const Document doc = buildDocumentFromJsPage(pages_.first(), firstJs);
            input = aiFeedbackSnapshot(doc, options);
        }

        lastInputTokens_ = 0;
        lastOutputTokens_ = 0;
        pendingAiTestId_ = QJsonValue();
        pendingAiAnalyzeId_ = rpcId;
        aiClient_->generate(conn, key, input, settings_.aiSystemPrompt);
    } catch (const std::exception &e) {
        sendRpcResult(rpcId, false, {}, QString::fromUtf8(e.what()));
    }
}

void B2Window::handleSaveSettings(const QJsonValue &rpcId, const QJsonObject &params) {
    AppSettings next = settings_;
    if (params.contains("theme"))
        next.theme = stringToThemeMode(params.value("theme").toString());
    if (params.contains("language"))
        next.language = stringToLanguageMode(params.value("language").toString(), next.language);
    if (params.contains("ocrLanguage"))
        next.ocrLanguage = stringToOcrLanguageMode(params.value("ocrLanguage").toString(), next.ocrLanguage);
    if (params.contains("captureOnStartup"))
        next.captureOnStartup = params.value("captureOnStartup").toBool();
    if (params.contains("launchAtLogin"))
        next.launchAtLogin = params.value("launchAtLogin").toBool();
    if (params.contains("fitImageOnOpen"))
        next.fitImageOnOpen = params.value("fitImageOnOpen").toBool();
    if (params.contains("confirmBeforeDiscard"))
        next.confirmBeforeDiscard = params.value("confirmBeforeDiscard").toBool();
    if (params.contains("defaultTool"))
        next.defaultTool = params.value("defaultTool").toInt(next.defaultTool);
    if (params.contains("traySingleClick"))
        next.traySingleClick = params.value("traySingleClick").toString();
    if (params.contains("trayDoubleClick"))
        next.trayDoubleClick = params.value("trayDoubleClick").toString();
    if (params.contains("includeOcr")) {
        next.regionOcrEnabled = params.value("includeOcr").toBool();
        if (!params.contains("includeRecognizedText"))
            next.includeRecognizedText = next.regionOcrEnabled;
    }
    if (params.contains("includeRecognizedText"))
        next.includeRecognizedText = params.value("includeRecognizedText").toBool();
    if (params.contains("includeWindowTitle"))
        next.includeWindowTitle = params.value("includeWindowTitle").toBool();
    if (params.contains("exportMode")) {
        const QString m = params.value("exportMode").toString();
        next.imageExportMode = m == "embedded" ? ImageExportMode::Embedded
                                               : (m == "none" ? ImageExportMode::None : ImageExportMode::File);
    }
    if (params.contains("copyJsonTextWithImage"))
        next.copyJsonTextWithImage = params.value("copyJsonTextWithImage").toBool();
    if (params.contains("captureShadow"))
        next.captureStyle.shadow = params.value("captureShadow").toBool();
    if (params.contains("captureCornerRadius"))
        next.captureStyle.cornerRadius = std::clamp(params.value("captureCornerRadius").toInt(next.captureStyle.cornerRadius), 0, 100);
    if (params.contains("feedbackDir"))
        next.feedbackDir = params.value("feedbackDir").toString().trimmed();
    if (params.contains("scrollAxis"))
        next.scrollAxis = params.value("scrollAxis").toString() == "horizontal" ? Qt::Horizontal : Qt::Vertical;
    if (params.contains("scrollAutoCrop"))
        next.scrollAutoCrop = params.value("scrollAutoCrop").toBool();
    if (params.contains("scrollUltraLong"))
        next.scrollUltraLong = params.value("scrollUltraLong").toBool();
    if (params.contains("checkUpdatesOnStartup"))
        next.checkUpdatesOnStartup = params.value("checkUpdatesOnStartup").toBool();
    if (params.contains("updateSource"))
        next.updateSource = params.value("updateSource").toString();

    if (params.contains("shortcuts") && params.value("shortcuts").isObject()) {
        const QJsonObject map = params.value("shortcuts").toObject();
        for (const auto &def : shortcutDefinitions()) {
            if (!map.contains(def.id))
                continue;
            const QString raw = map.value(def.id).toString().trimmed();
            if (raw.isEmpty()) {
                next.shortcuts[def.id] = QKeySequence();
            } else {
                const QKeySequence seq = QKeySequence::fromString(raw, QKeySequence::PortableText);
                if (seq.isEmpty()) {
                    sendRpcResult(rpcId, false, {}, tr("快捷键格式无效：%1").arg(raw));
                    return;
                }
                next.shortcuts[def.id] = seq;
            }
        }
    }

    QString error;
    if (settingsApplyHandler_) {
        error = settingsApplyHandler_(next);
    } else {
        error = validateSettings(next);
        if (error.isEmpty())
            saveSettings(next, &error, settingsFile_);
    }
    if (!error.isEmpty()) {
        sendRpcResult(rpcId, false, {}, error);
        return;
    }

    settings_ = next;
    hasDarkOverride_ = false;
    syncDwmTitleBar(isEffectiveDark());
    if (!settingsApplyHandler_)
        emit preferencesUpdated(settings_);
    sendRpcResult(rpcId, true, buildPreferencesJson());
}

void B2Window::handleDiagnosticsExport(const QJsonValue &rpcId) {
    const auto filename = QStringLiteral("EditHere-diagnostics-%1.txt")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QString dest = QFileDialog::getSaveFileName(
        this, tr("导出诊断日志"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(filename),
        tr("文本文件 (*.txt);;所有文件 (*.*)"));
    if (dest.isEmpty()) {
        sendRpcResult(rpcId, true, QJsonObject{{"saved", false}});
        return;
    }
    if (QFileInfo(dest).suffix().isEmpty())
        dest += QStringLiteral(".txt");
    const QString error = diagnostics::exportReport(dest);
    if (!error.isEmpty()) {
        diagnostics::write(diagnostics::Level::Error, "diagnostics.export", error);
        sendRpcResult(rpcId, false, {}, tr("日志导出失败：%1").arg(error));
        return;
    }
    sendRpcResult(rpcId, true, QJsonObject{{"saved", true}, {"path", QDir::toNativeSeparators(dest)}});
}

void B2Window::handleDiagnosticsOpenFolder(const QJsonValue &rpcId) {
    const QString folder = diagnostics::directory();
    if (folder.isEmpty()) {
        sendRpcResult(rpcId, false, {}, tr("日志目录尚未初始化。"));
        return;
    }
    QDir().mkpath(folder);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        diagnostics::write(diagnostics::Level::Warning, "diagnostics.open_folder", "Unable to open log folder");
        sendRpcResult(rpcId, false, {}, tr("无法打开日志目录：%1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    sendRpcResult(rpcId, true, QJsonObject{{"opened", true}, {"path", QDir::toNativeSeparators(folder)}});
}

void B2Window::handleUpdatesCheck(const QJsonValue &rpcId) {
    if (!updater_) {
        sendRpcResult(rpcId, false, {}, tr("更新检查器尚未初始化。"));
        return;
    }
    if (updater_->busy()) {
        sendRpcResult(rpcId, false, {}, tr("正在检查更新，请稍候…"));
        return;
    }
    pendingUpdateCheckId_ = rpcId;
    updater_->setSourcePreference(settings_.updateSource);
    updater_->check();
}

} // namespace h2d
