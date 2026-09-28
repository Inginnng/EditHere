#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>
#include <uiautomation.h>
#include "platform.h"
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QtGui/qscreen_platform.h>
#include <atomic>
#include <memory>
namespace h2d {
namespace {
// Free functions have no tr(); the enclosing "h2d" context groups them so the
// translation file stays easy to review.
inline QString tr(const char *text) {
    return QCoreApplication::translate("h2d", text);
}
QString bstrText(BSTR value) {
    QString text = value ? QString::fromWCharArray(value) : QString();
    SysFreeString(value);
    return text;
}

UINT virtualKey(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return UINT('A' + key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return UINT('0' + key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return UINT(VK_F1 + key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Backspace:
        return VK_BACK;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Insert:
        return VK_INSERT;
    case Qt::Key_Delete:
        return VK_DELETE;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    case Qt::Key_Left:
        return VK_LEFT;
    case Qt::Key_Right:
        return VK_RIGHT;
    case Qt::Key_Up:
        return VK_UP;
    case Qt::Key_Down:
        return VK_DOWN;
    case Qt::Key_PageUp:
        return VK_PRIOR;
    case Qt::Key_PageDown:
        return VK_NEXT;
    case Qt::Key_Print:
        return VK_SNAPSHOT;
    case Qt::Key_Pause:
        return VK_PAUSE;
    default:
        return 0;
    }
}
ATOM allocateShortcutId() {
    static std::atomic<quint64> nextId{1};
    const QString name =
        QString("EditHere.HotKey.%1.%2").arg(GetCurrentProcessId()).arg(nextId.fetch_add(1));
    return GlobalAddAtomW(reinterpret_cast<LPCWSTR>(name.utf16()));
}
} // namespace
void *windowUnderPoint(QPoint nativePoint) {
    const POINT point{nativePoint.x(), nativePoint.y()};
    // What the user sees at a point, which is not always what Windows answers with: the
    // capture window covers every screen it was taken over and is topmost for as long as
    // a long picture is being taken, so WindowFromPoint lands on it every time. The
    // window that has to be scrolled is the first one below it in the z-order that really
    // lies under the point and is not ours.
    HWND candidate = WindowFromPoint(point);
    if (candidate == nullptr)
        return nullptr;
    // A child of our own window is still ours, so every candidate is walked back up to
    // its top level before it is judged. Doing that first also keeps the walk below on
    // the z-order of top level windows rather than on that of some child.
    if (HWND root = GetAncestor(candidate, GA_ROOT))
        candidate = root;
    for (; candidate != nullptr; candidate = GetWindow(candidate, GW_HWNDNEXT)) {
        DWORD process = 0;
        GetWindowThreadProcessId(candidate, &process);
        if (process == GetCurrentProcessId())
            continue;
        if (!IsWindowVisible(candidate) || IsIconic(candidate))
            continue;
        // A cloaked window is on the z-order but is not on screen: it is what a window
        // of a suspended or virtual desktop looks like.
        DWORD cloaked = 0;
        DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked != 0)
            continue;
        RECT bounds{};
        if (GetWindowRect(candidate, &bounds) && PtInRect(&bounds, point))
            return candidate;
    }
    return nullptr;
}
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
        // Send Escape to dismiss any active shell popup (tray overflow flyout,
        // jump list, etc.). SetForegroundWindow alone does not reliably close
        // Windows 11's tray overflow panel.
        INPUT escape[2]{};
        escape[0].type = INPUT_KEYBOARD;
        escape[0].ki.wVk = VK_ESCAPE;
        escape[1].type = INPUT_KEYBOARD;
        escape[1].ki.wVk = VK_ESCAPE;
        escape[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, escape, sizeof(INPUT));
        // A visible (fully transparent) window can become foreground. Merely hiding
        // the editor or waiting leaves Explorer's overflow panel active on a tray click.
        activation->handle = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"STATIC", L"EditHere capture preparation", WS_POPUP,
            GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 1, 1, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (activation->handle) {
            SetLayeredWindowAttributes(activation->handle, 0, 0, LWA_ALPHA);
            ShowWindow(activation->handle, SW_SHOW);
            SetForegroundWindow(activation->handle);
        }
        // Escape closes the flyout quickly; 200ms is enough for the compositor flush.
        QTimer::singleShot(200, context, [activation, ready = std::move(ready)] {
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
        ScreenFrame frame{screen->name(), screen->geometry(), native, image, !native.isEmpty()};
        frame.windowScopeAvailable = true;
        struct Windows { ScreenFrame *frame; DWORD excluded; } data{&frame, GetCurrentProcessId()};
        EnumWindows([](HWND hwnd, LPARAM param)->BOOL {
            auto &data=*reinterpret_cast<Windows *>(param);
            DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
            if(pid==data.excluded || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
            DWORD cloaked=0; DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
            if(cloaked) return TRUE;
            wchar_t name[128]{}; GetClassNameW(hwnd,name,128);
            if(wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0) return TRUE;
            RECT r{};
            if(FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(hwnd,&r);
            auto &f=*data.frame;
            if(f.nativeGeometry.isEmpty()) return TRUE;
            double sx=double(f.image.width())/f.nativeGeometry.width(), sy=double(f.image.height())/f.nativeGeometry.height();
            QRect bounds(qRound((r.left-f.nativeGeometry.x())*sx),qRound((r.top-f.nativeGeometry.y())*sy),
                         qRound((r.right-r.left)*sx),qRound((r.bottom-r.top)*sy));
            bounds=bounds.intersected(f.image.rect());
            if(!bounds.isEmpty()) {
                auto target=manualTarget(); target["label"]=QT_TRANSLATE_NOOP("EditHere", "窗口");
                target["source"]="window";
                target["windowId"]=QString::number(quintptr(hwnd));
                f.frontWindows.append({bounds,target});
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&data));
        frames.append(frame);
    }
    callback(frames, frames.isEmpty() ? tr("无法读取屏幕画面") : QString());
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
bool scrollAt(QPoint nativePoint, int steps, Qt::Orientation axis) {
    if (steps == 0)
        return false;
    const POINT point{nativePoint.x(), nativePoint.y()};
    HWND target = reinterpret_cast<HWND>(windowUnderPoint(nativePoint));
    if (target == nullptr)
        return false;
    // The wheel is sent whether or not the window advertises a scroll bar. Most pages
    // today are drawn by the application itself and carry no WS_VSCROLL at all, so
    // asking that question first would refuse to scroll exactly the windows a long
    // capture is for. A wheel over something that cannot scroll is simply ignored, and
    // the run ends on the frame that did not move.
    // The wheel is posted to the window that was found rather than injected. An injected
    // wheel is delivered to whatever is under the pointer, and while a long capture runs
    // that is the capture window itself, which stays on screen for the whole run so the
    // region can still be dragged to reach the end of a page.
    const UINT message = axis == Qt::Horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL;
    // A wheel message counts its movement in the direction of the wheel, and the caller
    // counts it in the direction the content should travel: positive steps are the page
    // moving on. Vertical wheel messages use negative deltas for down, whereas
    // horizontal wheel messages use positive deltas for right.
    const int direction = axis == Qt::Horizontal ? 1 : -1;
    const WPARAM delta = MAKEWPARAM(0, static_cast<short>(direction * steps * WHEEL_DELTA));
    // The point goes with the message in screen coordinates. It is what the application
    // hit-tests with, and it is why the pointer does not have to be moved at all: moving
    // it would pull the cursor out of the region the user is dragging.
    const LPARAM where = MAKELPARAM(point.x, point.y);
    return PostMessageW(target, message, delta, where) != FALSE;
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
    return QKeySequence("Ctrl+Shift+2", QKeySequence::PortableText).toString(QKeySequence::NativeText);
}
GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() {
    stop();
}
void GlobalShortcut::stop() {
    if (handle_) {
        UnregisterHotKey(nullptr, int(shortcutId_));
        GlobalDeleteAtom(ATOM(shortcutId_));
        if (qApp)
            qApp->removeNativeEventFilter(this);
    }
    handle_ = nullptr;
    shortcutId_ = 0;
    sequence_ = {};
    lastError_.clear();
}
bool GlobalShortcut::start(const QKeySequence &sequence) {
    lastError_.clear();
    if (sequence.isEmpty()) {
        stop();
        return true;
    }
    if (handle_ && sequence == sequence_)
        return true;
    if (sequence.count() != 1) {
        lastError_ = tr("全局截图快捷键只支持一组按键，不能使用连续组合。");
        return false;
    }
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    const auto supported = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    const UINT key = virtualKey(combination.key());
    if (!key || (modifiers & ~supported)) {
        lastError_ = tr("不支持此按键，请使用字母、数字、F1–F24、PrintScreen 或方向与导航键。");
        return false;
    }
    UINT nativeModifiers = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier))
        nativeModifiers |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        nativeModifiers |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier))
        nativeModifiers |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier))
        nativeModifiers |= MOD_WIN;
    const ATOM newId = allocateShortcutId();
    if (!newId) {
        lastError_ = tr("无法创建快捷键注册，请稍后重试。");
        return false;
    }
    if (!RegisterHotKey(nullptr, int(newId), nativeModifiers, key)) {
        const DWORD error = GetLastError();
        GlobalDeleteAtom(newId);
        lastError_ = error == ERROR_HOTKEY_ALREADY_REGISTERED
                         ? tr("这个快捷键已被其他应用或系统占用，请更换一组按键。")
                         : tr("系统无法注册此快捷键（错误 %1），请更换一组按键。").arg(error);
        return false;
    }
    // Register the replacement first, so a conflict never disables the working shortcut.
    const bool wasRegistered = handle_ != nullptr;
    if (wasRegistered) {
        UnregisterHotKey(nullptr, int(shortcutId_));
        GlobalDeleteAtom(ATOM(shortcutId_));
    }
    handle_ = this;
    shortcutId_ = newId;
    sequence_ = sequence;
    if (!wasRegistered)
        qApp->installNativeEventFilter(this);
    return true;
}
bool GlobalShortcut::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
    const auto msg = static_cast<MSG *>(message);
    if (handle_ && msg && msg->message == WM_HOTKEY && msg->wParam == shortcutId_) {
        emit triggered();
        return true;
    }
    return false;
}
} // namespace h2d
