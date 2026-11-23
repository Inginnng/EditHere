#include "b2window.h"
#include "agentprotocol.h"
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
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMessageBox>
#include <QMimeData>
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
    case LanguageMode::English: return "English";
    case LanguageMode::TraditionalChinese: return QString::fromUtf8("繁體中文");
    case LanguageMode::SimplifiedChinese:
    case LanguageMode::System:
    default: return QString::fromUtf8("简体中文");
    }
}

LanguageMode stringToLanguageMode(const QString &value, LanguageMode fallback) {
    if (value == "English") return LanguageMode::English;
    if (value == QString::fromUtf8("繁體中文")) return LanguageMode::TraditionalChinese;
    if (value == QString::fromUtf8("简体中文")) return LanguageMode::SimplifiedChinese;
    return fallback;
}

QJsonObject rectToJson(const QRect &r) {
    return QJsonObject{{"x1", r.left()}, {"y1", r.top()}, {"x2", r.left() + r.width()}, {"y2", r.top() + r.height()}};
}

} // namespace

B2Window::B2Window(const AppSettings &settings, const QString &settingsFile, QWidget *parent)
    : QMainWindow(parent),
      server_(this),
      webview_(new WebView2Host(this)),
      ocr_(new OcrEngine(this)),
      settings_(settings),
      settingsFile_(settingsFile) {
    setWindowTitle(tr("EditHere · 改这里"));
    setWindowIcon(QGuiApplication::windowIcon());
    resize(1360, 860);
    setMinimumSize(920, 640);

    setCentralWidget(webview_);

    connect(webview_, &WebView2Host::messageReceived, this, &B2Window::handleWebMessage);
    connect(webview_, &WebView2Host::failed, this, [this](const QString &reason) {
        diagnostics::write(diagnostics::Level::Warning, "b2.window_webview_failed", reason);
        emit webviewFailed(reason);
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

void B2Window::openCapturedDocument(Document doc) {
    if (doc.image.isNull()) return;
    if (doc.png.isEmpty()) doc.png = encodePng(doc.image);
    if (doc.candidates.isEmpty()) doc.candidates = detectBlocks(doc.image);

    if (isImageSetProject_) {
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
            {"blocks", blocksForPage(page)},
            {"annotations", annArr},
        });
    }
    return arr;
}

QJsonObject B2Window::buildPreferencesJson() const {
    return QJsonObject{
        {"theme", themeModeToString(settings_.theme)},
        {"includeCoordinates", true},
        {"includeOcr", settings_.regionOcrEnabled},
        {"captureShadow", settings_.captureStyle.shadow},
        {"compactDensity", false},
        {"exportMode", settings_.imageExportMode == ImageExportMode::Embedded ? "embedded" : "file"},
        {"language", languageModeToString(settings_.language)},
    };
}

QJsonObject B2Window::buildDoctorSnapshotJson() const {
    const QString wvVer = webview_->runtimeVersion().isEmpty() ? tr("内置运行") : webview_->runtimeVersion();
    const QString cliPath = QDir::fromNativeSeparators(
        QDir(QFileInfo(QCoreApplication::applicationDirPath()).absolutePath())
            .filePath("../cli/unified-mcp.mjs"));
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
        {"doctor", buildDoctorSnapshotJson()},
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
        if (params.contains("theme")) settings_.theme = stringToThemeMode(params.value("theme").toString());
        if (params.contains("includeOcr")) settings_.regionOcrEnabled = params.value("includeOcr").toBool();
        if (params.contains("captureShadow")) settings_.captureStyle.shadow = params.value("captureShadow").toBool();
        if (params.contains("exportMode")) {
            settings_.imageExportMode = params.value("exportMode").toString() == "embedded"
                                            ? ImageExportMode::Embedded
                                            : ImageExportMode::File;
        }
        if (params.contains("language")) {
            settings_.language = stringToLanguageMode(params.value("language").toString(), settings_.language);
        }
        QString saveErr;
        if (!saveSettings(settings_, &saveErr, settingsFile_)) {
            sendRpcResult(id, false, {}, saveErr);
            return;
        }
        hasDarkOverride_ = false;
        syncDwmTitleBar(isEffectiveDark());
        emit preferencesUpdated(settings_);
        sendRpcResult(id, true, buildPreferencesJson());
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

    const QImage crop = targetPage->document.image.copy(rect);
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

        const QString cacheRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                      .filePath("feedback");
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

} // namespace h2d
