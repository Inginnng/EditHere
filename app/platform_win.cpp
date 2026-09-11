#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.h"
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QtGui/qscreen_platform.h>
#include <dwmapi.h>
#include <memory>
#include <uiautomation.h>
#include <windows.h>
namespace h2d {
namespace {
QString bstrText(BSTR value) {
    QString text = value ? QString::fromWCharArray(value) : QString();
    SysFreeString(value);
    return text;
}
} // namespace
void prepareScreenCapture(QObject *context, std::function<void()> ready) {
    // Run after QMenu / the native tray callback has returned to the event loop.
    QTimer::singleShot(0, context, [context, ready = std::move(ready)]() mutable {
        struct ActivationWindow {
            HWND handle = nullptr;
            ~ActivationWindow() {
                if (handle)
                    DestroyWindow(handle);
            }
        };
        auto activation = std::make_shared<ActivationWindow>();
        // A visible (fully transparent) window can become foreground. Merely hiding
        // the editor or waiting leaves Explorer's overflow panel active on a tray click.
        activation->handle = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"STATIC", L"HelpDesign capture preparation", WS_POPUP,
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 1, 1, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (activation->handle) {
            SetLayeredWindowAttributes(activation->handle, 0, 0, LWA_ALPHA);
            ShowWindow(activation->handle, SW_SHOW);
            SetForegroundWindow(activation->handle);
        }
        // Focus loss lets the shell dismiss its own panel. Allow its closing animation
        // to finish, then wait for the compositor before obtaining the frozen image.
        QTimer::singleShot(300, context, [activation, ready = std::move(ready)] {
            if (activation->handle)
                ShowWindow(activation->handle, SW_HIDE);
            DwmFlush();
            ready();
        });
    });
}
void captureScreens(CaptureCallback callback) {
    QVector<ScreenFrame> frames;
    for (QScreen *screen : QGuiApplication::screens()) {
        QImage image = screen->grabWindow(0).toImage();
        if (image.isNull())
            continue;
        image.setDevicePixelRatio(1);
        QRect native;
        if (auto handle = screen->nativeInterface<QNativeInterface::QWindowsScreen>()) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(handle->handle(), &info))
                native =
                    QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                          info.rcMonitor.bottom - info.rcMonitor.top);
        }
        frames.append({screen->name(), screen->geometry(), native, image, !native.isEmpty()});
    }
    callback(frames, frames.isEmpty() ? "无法读取屏幕画面" : QString());
}
QVector<Candidate> nativeElementsAt(QPoint point, qint64 excludedPid) {
    struct Search {
        POINT point;
        DWORD excluded;
        HWND window = nullptr;
    } search{{point.x(), point.y()}, DWORD(excludedPid)};
    EnumWindows(
        [](HWND hwnd, LPARAM data) -> BOOL {
            auto s = reinterpret_cast<Search *>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == s->excluded || !IsWindowVisible(hwnd) || IsIconic(hwnd))
                return TRUE;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked)
                return TRUE;
            RECT r{};
            GetWindowRect(hwnd, &r);
            if (!PtInRect(&r, s->point))
                return TRUE;
            s->window = hwnd;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));
    QVector<Candidate> result;
    if (!search.window)
        return result;
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation *automation = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                   reinterpret_cast<void **>(&automation)))) {
        IUIAutomationElement *element = nullptr;
        IUIAutomationTreeWalker *walker = nullptr;
        automation->get_ControlViewWalker(&walker);
        if (SUCCEEDED(automation->ElementFromHandle(search.window, &element)) && element) {
            int visited = 0;
            for (int depth = 0; element && depth < 16 && visited < 240; depth++) {
                RECT r{};
                if (SUCCEEDED(element->get_CurrentBoundingRectangle(&r)) && r.right > r.left &&
                    r.bottom > r.top) {
                    BSTR name = nullptr, control = nullptr, automationId = nullptr;
                    element->get_CurrentName(&name);
                    element->get_CurrentLocalizedControlType(&control);
                    element->get_CurrentAutomationId(&automationId);
                    auto target = manualTarget();
                    target["source"] = "uia";
                    target["method"] = "windows-uia";
                    target["label"] = bstrText(name).left(1000);
                    target["controlType"] = bstrText(control);
                    target["automationId"] = bstrText(automationId);
                    QRect rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
                    target["originalScreenBounds"] = rectJson(rect);
                    if (target["label"].toString().isEmpty())
                        target["label"] = target["controlType"];
                    result.append({rect, target});
                }
                IUIAutomationElement *child = nullptr;
                if (walker)
                    walker->GetFirstChildElement(element, &child);
                while (child && ++visited < 240) {
                    RECT bounds{};
                    BOOL offscreen = TRUE;
                    child->get_CurrentIsOffscreen(&offscreen);
                    if (!offscreen && SUCCEEDED(child->get_CurrentBoundingRectangle(&bounds)) &&
                        PtInRect(&bounds, search.point))
                        break;
                    IUIAutomationElement *next = nullptr;
                    walker->GetNextSiblingElement(child, &next);
                    child->Release();
                    child = next;
                }
                element->Release();
                element = child;
            }
            if (element)
                element->Release();
        }
        if (walker)
            walker->Release();
        automation->Release();
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return result;
}
bool requestAccessibility() {
    return true;
}
void configureNativeWindow(QWidget *window, bool overlay) {
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    SetWindowPos(hwnd, overlay ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    DWORD preference = overlay ? 1 : 2;
    DwmSetWindowAttribute(hwnd, 33, &preference, sizeof(preference));
    // Keep our overlay out of capture sources where Windows supports it.
    if (overlay)
        SetWindowDisplayAffinity(hwnd, 0x00000011);
}
QString globalShortcutLabel() {
    return "Ctrl + Shift + 2";
}
GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() {
    if (handle_)
        UnregisterHotKey(nullptr, 0x4822);
    qApp->removeNativeEventFilter(this);
}
bool GlobalShortcut::start() {
    if (!RegisterHotKey(nullptr, 0x4822, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '2'))
        return false;
    handle_ = this;
    qApp->installNativeEventFilter(this);
    return true;
}
bool GlobalShortcut::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
    auto msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 0x4822) {
        emit triggered();
        return true;
    }
    return false;
}
} // namespace h2d
