#include "webview2host.h"
#include "diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QMoveEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

#include <atomic>
#include <cstdint>

namespace h2d {

namespace {

// Exact WebView2 COM IIDs verified against official WebView2 bindings.
const IID kIidEnvCompletedHandler = {
    0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
const IID kIidControllerCompletedHandler = {
    0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
const IID kIidWebMessageReceivedHandler = {
    0x57213f19, 0x00e6, 0x49fa, {0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2}};
const IID kIidNavigationCompletedHandler = {
    0xd33a35bf, 0x1c49, 0x4f98, {0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c}};

struct ICoreWebView2Settings;
struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

struct ICoreWebView2WebMessageReceivedEventArgs : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR *source) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR *webMessageAsJson) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR *webMessageAsString) = 0;
};

struct ICoreWebView2Settings : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL *isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL isScriptEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL *isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL isWebMessageEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreHostObjectsAllowed(BOOL *allowed) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreHostObjectsAllowed(BOOL allowed) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsZoomControlEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsZoomControlEnabled(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsBuiltInErrorPageEnabled(BOOL *enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBuiltInErrorPageEnabled(BOOL enabled) = 0;
};

struct ICoreWebView2 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings **settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR *uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR htmlContent) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationStarting(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ContentLoading(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ContentLoading(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_HistoryChanged(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_HistoryChanged(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PermissionRequested(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ProcessFailed(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(LPCWSTR javaScript, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR id) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExecuteScript(LPCWSTR javaScript, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CapturePreview(int format, void *imageStream, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reload() = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR webMessageAsJson) = 0;
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR webMessageAsString) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(std::int64_t token) = 0;
};

struct ICoreWebView2Controller : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL *isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(RECT *bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ZoomFactor(double *zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveFocus(int reason) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_GotFocus(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_GotFocus(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_LostFocus(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_LostFocus(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(std::int64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND *parentWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND parentWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2) = 0;
};

struct ICoreWebView2Environment : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parentWindow, void *handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(void *content, int statusCode, LPCWSTR reasonPhrase,
                                                                 LPCWSTR headers, void **response) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR *versionInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(void *handler, std::int64_t *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(std::int64_t token) = 0;
};

template <typename Fn>
class ComCallback final : public IUnknown {
  public:
    ComCallback(const IID &iid, Fn fn) : iid_(iid), fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) {
            *ppv = static_cast<void *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    virtual HRESULT STDMETHODCALLTYPE Invoke(std::uintptr_t arg1, void *arg2) {
        return fn_(arg1, arg2);
    }

  private:
    ~ComCallback() = default;
    std::atomic<ULONG> refs_{1};
    IID iid_;
    Fn fn_;
};

QString readRegistryWebView2Version() {
    static const wchar_t *kClientKey =
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    static const wchar_t *kWowClientKey =
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

    const struct {
        HKEY root;
        const wchar_t *subkey;
    } candidates[] = {
        {HKEY_LOCAL_MACHINE, kWowClientKey},
        {HKEY_LOCAL_MACHINE, kClientKey},
        {HKEY_CURRENT_USER, kClientKey},
    };
    for (const auto &c : candidates) {
        HKEY hkey = nullptr;
        if (RegOpenKeyExW(c.root, c.subkey, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            wchar_t buf[128] = {};
            DWORD bytes = sizeof(buf);
            DWORD type = 0;
            const LONG rc = RegQueryValueExW(hkey, L"pv", nullptr, &type, reinterpret_cast<BYTE *>(buf), &bytes);
            RegCloseKey(hkey);
            if (rc == ERROR_SUCCESS && type == REG_SZ) {
                const QString pv = QString::fromWCharArray(buf).trimmed();
                if (!pv.isEmpty() && pv != "0.0.0.0") return pv;
            }
        }
    }
    return {};
}

QString findEmbeddedBrowserDll(const QString &pv) {
    if (pv.isEmpty()) return {};
    const QStringList roots = {
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("LocalAppData"),
    };
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString p = QDir(root).filePath(
            QString("Microsoft/EdgeWebView/Application/%1/EBWebView/x64/EmbeddedBrowserWebView.dll").arg(pv));
        if (QFileInfo::exists(p)) return QDir::toNativeSeparators(p);
    }
    return {};
}

using FnCreateCoreEnvWithOptions = HRESULT(STDAPICALLTYPE *)(LPCWSTR, LPCWSTR, void *, void *);
using FnCreateEnvInternal = HRESULT(STDAPICALLTYPE *)(BOOL, int, LPCWSTR, void *, void *);

HMODULE gEmbeddedWebViewMod = nullptr;
HMODULE gLoaderWebViewMod = nullptr;

} // namespace

class WebView2Host::Impl {
  public:
    ICoreWebView2Environment *env = nullptr;
    ICoreWebView2Controller *controller = nullptr;
    ICoreWebView2 *webview = nullptr;
    std::int64_t webMessageToken = 0;
    std::int64_t navCompletedToken = 0;
    bool initialized = false;
    QUrl pendingUrl;
    QList<QJsonObject> outboundQueue;

    ~Impl() { releaseCom(); }

    void releaseCom() {
        if (webview) {
            if (webMessageToken) webview->remove_WebMessageReceived(webMessageToken);
            if (navCompletedToken) webview->remove_NavigationCompleted(navCompletedToken);
            webview->Release();
            webview = nullptr;
        }
        if (controller) {
            controller->Close();
            controller->Release();
            controller = nullptr;
        }
        if (env) {
            env->Release();
            env = nullptr;
        }
    }
};

bool WebView2Host::runtimeAvailable(QString *versionOut) {
    const QString pv = readRegistryWebView2Version();
    if (pv.isEmpty()) return false;
    if (!findEmbeddedBrowserDll(pv).isEmpty() ||
        QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("WebView2Loader.dll"))) {
        if (versionOut) *versionOut = pv;
        return true;
    }
    return false;
}

WebView2Host::WebView2Host(QWidget *parent)
    : QWidget(parent), impl_(new Impl()) {
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
}

WebView2Host::~WebView2Host() {
    delete impl_;
    impl_ = nullptr;
}

void WebView2Host::initialize() {
    if (impl_->initialized) return;
    impl_->initialized = true;

    const QString pv = readRegistryWebView2Version();
    runtimeVersion_ = pv;
    diagnostics::write(diagnostics::Level::Info, "b2.webview2_init",
                       "Initializing WebView2 host", {{"runtimeVersion", pv}});

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Q_UNUSED(coHr);

    const QString userDataDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                    .filePath("webview2");
    QDir().mkpath(userDataDir);
    const std::wstring wUserData = QDir::toNativeSeparators(userDataDir).toStdWString();

    QPointer<WebView2Host> self(this);

    auto *envHandler = new ComCallback(
        kIidEnvCompletedHandler,
        [self](std::uintptr_t resultArg, void *envPtr) -> HRESULT {
            const HRESULT result = static_cast<HRESULT>(resultArg);
            if (!self) return S_OK;
            if (FAILED(result) || !envPtr) {
                const QString msg = QString("CreateWebViewEnvironment failed (HRESULT 0x%1)")
                                        .arg(quint32(result), 8, 16, QChar('0'));
                diagnostics::write(diagnostics::Level::Error, "b2.webview2_env_failed", msg);
                emit self->failed(msg);
                return S_OK;
            }
            auto *env = static_cast<ICoreWebView2Environment *>(envPtr);
            env->AddRef();
            self->impl_->env = env;

            LPWSTR verStr = nullptr;
            if (SUCCEEDED(env->get_BrowserVersionString(&verStr)) && verStr) {
                self->runtimeVersion_ = QString::fromWCharArray(verStr);
                CoTaskMemFree(verStr);
            }

            auto *ctrlHandler = new ComCallback(
                kIidControllerCompletedHandler,
                [self](std::uintptr_t ctrlResultArg, void *ctrlPtr) -> HRESULT {
                    const HRESULT ctrlResult = static_cast<HRESULT>(ctrlResultArg);
                    if (!self) return S_OK;
                    if (FAILED(ctrlResult) || !ctrlPtr) {
                        const QString msg = QString("CreateCoreWebView2Controller failed (HRESULT 0x%1)")
                                                .arg(quint32(ctrlResult), 8, 16, QChar('0'));
                        diagnostics::write(diagnostics::Level::Error, "b2.webview2_ctrl_failed", msg);
                        emit self->failed(msg);
                        return S_OK;
                    }
                    auto *ctrl = static_cast<ICoreWebView2Controller *>(ctrlPtr);
                    ctrl->AddRef();
                    self->impl_->controller = ctrl;

                    ICoreWebView2 *wv = nullptr;
                    if (FAILED(ctrl->get_CoreWebView2(&wv)) || !wv) {
                        const QString msg = "ICoreWebView2Controller::get_CoreWebView2 failed";
                        diagnostics::write(diagnostics::Level::Error, "b2.webview2_core_failed", msg);
                        emit self->failed(msg);
                        return S_OK;
                    }
                    self->impl_->webview = wv;

                    ICoreWebView2Settings *settings = nullptr;
                    if (SUCCEEDED(wv->get_Settings(&settings)) && settings) {
                        settings->put_IsScriptEnabled(TRUE);
                        settings->put_IsWebMessageEnabled(TRUE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->put_IsBuiltInErrorPageEnabled(TRUE);
                        settings->Release();
                    }

                    auto *msgHandler = new ComCallback(
                        kIidWebMessageReceivedHandler,
                        [self](std::uintptr_t, void *argsPtr) -> HRESULT {
                            if (!self || !argsPtr) return S_OK;
                            auto *args = static_cast<ICoreWebView2WebMessageReceivedEventArgs *>(argsPtr);
                            LPWSTR jsonStr = nullptr;
                            if (SUCCEEDED(args->get_WebMessageAsJson(&jsonStr)) && jsonStr) {
                                const QByteArray utf8 = QString::fromWCharArray(jsonStr).toUtf8();
                                CoTaskMemFree(jsonStr);
                                const QJsonDocument doc = QJsonDocument::fromJson(utf8);
                                if (doc.isObject()) {
                                    emit self->messageReceived(doc.object());
                                }
                            }
                            return S_OK;
                        });
                    wv->add_WebMessageReceived(msgHandler, &self->impl_->webMessageToken);
                    msgHandler->Release();

                    auto *navHandler = new ComCallback(
                        kIidNavigationCompletedHandler,
                        [self](std::uintptr_t, void *) -> HRESULT {
                            if (!self) return S_OK;
                            const auto queued = self->impl_->outboundQueue;
                            self->impl_->outboundQueue.clear();
                            for (const auto &msg : queued) {
                                self->postJsonMessage(msg);
                            }
                            return S_OK;
                        });
                    wv->add_NavigationCompleted(navHandler, &self->impl_->navCompletedToken);
                    navHandler->Release();

                    RECT rc{};
                    GetClientRect(reinterpret_cast<HWND>(self->winId()), &rc);
                    ctrl->put_Bounds(rc);
                    ctrl->put_IsVisible(self->isVisible() ? TRUE : FALSE);

                    self->ready_ = true;
                    diagnostics::write(diagnostics::Level::Info, "b2.webview2_ready",
                                       "WebView2 controller ready",
                                       {{"runtimeVersion", self->runtimeVersion_}});
                    if (self->impl_->pendingUrl.isValid()) {
                        const std::wstring wUrl = self->impl_->pendingUrl.toString().toStdWString();
                        wv->Navigate(wUrl.c_str());
                    }
                    emit self->ready();
                    return S_OK;
                });

            const HWND hwnd = reinterpret_cast<HWND>(self->winId());
            const HRESULT ctrlHr = env->CreateCoreWebView2Controller(hwnd, ctrlHandler);
            ctrlHandler->Release();
            if (FAILED(ctrlHr)) {
                const QString msg = QString("CreateCoreWebView2Controller invocation failed (0x%1)")
                                        .arg(quint32(ctrlHr), 8, 16, QChar('0'));
                diagnostics::write(diagnostics::Level::Error, "b2.webview2_ctrl_call_failed", msg);
                emit self->failed(msg);
            }
            return S_OK;
        });

    HRESULT hr = E_FAIL;

    // 1. Prefer WebView2Loader.dll if bundled beside EditHere.exe.
    if (!gLoaderWebViewMod) {
        const QString exeLoader = QDir::toNativeSeparators(
            QDir(QCoreApplication::applicationDirPath()).filePath("WebView2Loader.dll"));
        if (QFileInfo::exists(exeLoader)) {
            gLoaderWebViewMod = LoadLibraryW(reinterpret_cast<LPCWSTR>(exeLoader.utf16()));
        }
    }
    if (gLoaderWebViewMod) {
        auto fn = reinterpret_cast<FnCreateCoreEnvWithOptions>(
            GetProcAddress(gLoaderWebViewMod, "CreateCoreWebView2EnvironmentWithOptions"));
        if (fn) {
            hr = fn(nullptr, wUserData.c_str(), nullptr, envHandler);
        }
    }

    // 2. Direct invocation against system-installed Edge WebView2 Runtime (zero SDK redistribution needed).
    if (FAILED(hr)) {
        if (!gEmbeddedWebViewMod) {
            const QString dllPath = findEmbeddedBrowserDll(pv);
            if (!dllPath.isEmpty()) {
                gEmbeddedWebViewMod = LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath.utf16()));
            }
        }
        if (gEmbeddedWebViewMod) {
            auto fnInternal = reinterpret_cast<FnCreateEnvInternal>(
                GetProcAddress(gEmbeddedWebViewMod, "CreateWebViewEnvironmentWithOptionsInternal"));
            if (fnInternal) {
                hr = fnInternal(TRUE, 0, wUserData.c_str(), nullptr, envHandler);
            }
        }
    }

    envHandler->Release();

    if (FAILED(hr)) {
        const QString msg = QString("WebView2 runtime failed to initialize (HRESULT 0x%1)")
                                .arg(quint32(hr), 8, 16, QChar('0'));
        diagnostics::write(diagnostics::Level::Error, "b2.webview2_bootstrap_failed", msg);
        QTimer::singleShot(0, this, [this, msg]() { emit failed(msg); });
    }
}

void WebView2Host::navigate(const QUrl &url) {
    impl_->pendingUrl = url;
    if (impl_->webview && url.isValid()) {
        const std::wstring wUrl = url.toString().toStdWString();
        impl_->webview->Navigate(wUrl.c_str());
    }
}

void WebView2Host::postJsonMessage(const QJsonObject &message) {
    if (!impl_->webview || !ready_) {
        impl_->outboundQueue.append(message);
        return;
    }
    const QString text = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    const std::wstring wJson = text.toStdWString();
    impl_->webview->PostWebMessageAsJson(wJson.c_str());
}

void WebView2Host::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (impl_->controller) {
        RECT rc{};
        GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
        impl_->controller->put_Bounds(rc);
    }
}

void WebView2Host::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!impl_->initialized) initialize();
    if (impl_->controller) {
        RECT rc{};
        GetClientRect(reinterpret_cast<HWND>(winId()), &rc);
        impl_->controller->put_Bounds(rc);
        impl_->controller->put_IsVisible(TRUE);
    }
}

void WebView2Host::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (impl_->controller) {
        impl_->controller->NotifyParentWindowPositionChanged();
    }
}

} // namespace h2d
