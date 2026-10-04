#include "platform.h"
#include "linuxportal.h"
#include "scroll_wayland.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QEventLoop>
#include <QImageReader>
#include <QPointer>
#include <QScreen>
#include <QSocketNotifier>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWidget>
#include <QWindow>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <limits>
#include <memory>

namespace h2d {
namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto shortcutInterface = "org.freedesktop.portal.GlobalShortcuts";
QString tr(const char *text) { return QCoreApplication::translate("h2d", text); }
bool wayland() { return QGuiApplication::platformName().startsWith("wayland") ||
                       qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland"; }
QVariantMap portalCall(const QString &method, QList<QVariant> args, QVariantMap options, QString *error) {
    QEventLoop loop;
    QVariantMap result;
    auto *request = new LinuxPortalRequest(&loop);
    request->start(shortcutInterface, method, args, options, [&](uint code, QVariantMap values) {
        if (code == 0) result = values;
        else *error = code == 1 ? tr("快捷键授权已取消。") :
            tr("桌面不支持快捷键授权。请在系统快捷键设置中绑定 EditHere --capture。") + "\n" + values.value("error").toString();
        loop.quit();
    });
    loop.exec();
    return result;
}
int xError = 0;
int catchXError(Display *, XErrorEvent *event) { xError = event->error_code; return 0; }

// Xlib's error handler is process-wide. These short, synchronous sections run
// only on the GUI thread and drain our own connection before restoring it.
class XErrorScope {
  public:
    explicit XErrorScope(Display *display) : display_(display) {
        XSync(display_, False);
        xError = 0;
        previous_ = XSetErrorHandler(catchXError);
    }
    ~XErrorScope() { XSync(display_, False); XSetErrorHandler(previous_); }
    bool failed() { XSync(display_, False); return xError != 0; }
  private:
    Display *display_;
    XErrorHandler previous_;
};
struct DisplayCloser { void operator()(Display *display) const { if (display) XCloseDisplay(display); } };
using XDisplay = std::unique_ptr<Display, DisplayCloser>;
XDisplay openXDisplay() { return XDisplay(XOpenDisplay(nullptr)); }

Window screenRoot(Display *display, QScreen *screen) {
    // Creating an unmapped native window gives the correct X root even with
    // classic multi-screen X servers, where each screen has a separate root.
    QWindow probe;
    probe.setScreen(screen);
    probe.create();
    QGuiApplication::sync();
    XWindowAttributes attributes{};
    return XGetWindowAttributes(display, Window(probe.winId()), &attributes) ? attributes.root : 0;
}
QRect rootGeometry(Display *display, Window root) {
    XWindowAttributes attributes{};
    return root && XGetWindowAttributes(display, root, &attributes) ?
        QRect(0, 0, attributes.width, attributes.height) : QRect();
}
quint32 windowPid(Display *display, Window window) {
    const Atom property = XInternAtom(display, "_NET_WM_PID", True);
    if (!property) return 0;
    Atom actual = 0; int format = 0; unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    quint32 result = 0;
    if (XGetWindowProperty(display, window, property, 0, 1, False, XA_CARDINAL,
                           &actual, &format, &count, &remaining, &data) == Success &&
        actual == XA_CARDINAL && format == 32 && count == 1 && data)
        result = quint32(*reinterpret_cast<unsigned long *>(data));
    if (data) XFree(data);
    return result;
}
bool windowContains(Display *display, Window window, Window root, QPoint point) {
    XWindowAttributes attributes{};
    int x = 0, y = 0; Window child = 0;
    return XGetWindowAttributes(display, window, &attributes) && attributes.map_state == IsViewable &&
        attributes.c_class != InputOnly &&
        XTranslateCoordinates(display, window, root, 0, 0, &x, &y, &child) &&
        QRect(x, y, attributes.width, attributes.height).contains(point);
}
Window clientWindow(Display *display, Window window, Window root, QPoint point, int depth = 0) {
    if (depth > 32) return 0;
    // A reparenting WM owns the decoration frame, while the application's PID
    // belongs to the client inside it. Stop at that client, before renderer
    // children belonging to a different browser process.
    if (windowPid(display, window)) return window;
    Window returnedRoot = 0, parent = 0, *children = nullptr; unsigned int count = 0;
    if (!XQueryTree(display, window, &returnedRoot, &parent, &children, &count)) return 0;
    Window result = 0;
    for (int i = int(count) - 1; i >= 0 && !result; --i)
        if (windowContains(display, children[i], root, point))
            result = clientWindow(display, children[i], root, point, depth + 1);
    if (children) XFree(children);
    return result;
}
ScrollCaptureTarget targetAt(Display *display, Window root, QPoint point) {
    Window returnedRoot = 0, parent = 0, *children = nullptr; unsigned int count = 0;
    if (!root || !XQueryTree(display, root, &returnedRoot, &parent, &children, &count)) return {};
    ScrollCaptureTarget result;
    for (int i = int(count) - 1; i >= 0; --i) {
        if (!windowContains(display, children[i], root, point)) continue;
        const Window client = clientWindow(display, children[i], root, point);
        const auto pid = client ? windowPid(display, client) : 0;
        if (pid == quint32(QCoreApplication::applicationPid())) continue;
        // An unidentifiable foreign window still occludes the old target. Do
        // not look through it and send a wheel to some application underneath.
        result = {quintptr(client ? client : children[i]), pid};
        break;
    }
    if (children) XFree(children);
    return result;
}
struct X11ScrollState {
    XDisplay display;
    Window root = 0;
    QPointer<QScreen> screen;
    QRect logicalGeometry, nativeGeometry;
    qreal ratio = 1;
};
X11ScrollState xScroll;
bool validXScreen(QString *error) {
    if (!xScroll.display || !xScroll.root || !xScroll.screen ||
        xScroll.screen->geometry() != xScroll.logicalGeometry ||
        !qFuzzyCompare(xScroll.screen->devicePixelRatio(), xScroll.ratio)) {
        if (error) *error = tr("屏幕布局或缩放已改变，请重新开始长截图。");
        return false;
    }
    return true;
}
QImage readXRegion(Display *display, Window root, const QRect &region) {
    auto *pixels = XGetImage(display, root, region.x(), region.y(), region.width(), region.height(), AllPlanes, ZPixmap);
    if (!pixels) return {};
    QImage result(region.size(), QImage::Format_RGB32);
    if (result.isNull() || !pixels->red_mask || !pixels->green_mask || !pixels->blue_mask) {
        XDestroyImage(pixels);
        return {};
    }
    auto component = [](unsigned long pixel, unsigned long mask) -> int {
        while (!(mask & 1)) { mask >>= 1; pixel >>= 1; }
        return int((pixel & mask) * 255 / mask);
    };
    const bool native32 = pixels->bits_per_pixel == 32 && pixels->byte_order == LSBFirst &&
        QSysInfo::ByteOrder == QSysInfo::LittleEndian && pixels->red_mask == 0xff0000 &&
        pixels->green_mask == 0xff00 && pixels->blue_mask == 0xff;
    for (int y = 0; y < region.height(); ++y) {
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        if (native32) {
            const auto *in = reinterpret_cast<const quint32 *>(pixels->data + y * pixels->bytes_per_line);
            for (int x = 0; x < region.width(); ++x) out[x] = in[x] | 0xff000000;
        } else {
            for (int x = 0; x < region.width(); ++x) {
                const auto pixel = XGetPixel(pixels, x, y);
                out[x] = qRgb(component(pixel, pixels->red_mask), component(pixel, pixels->green_mask),
                              component(pixel, pixels->blue_mask));
            }
        }
    }
    XDestroyImage(pixels);
    return result;
}
}
class LinuxShortcut final : public QObject {
    Q_OBJECT
  public:
    explicit LinuxShortcut(GlobalShortcut *owner) : QObject(owner), owner(owner) {}
    ~LinuxShortcut() override {
        if (display) {
            XUngrabKey(display, AnyKey, AnyModifier, DefaultRootWindow(display));
            XCloseDisplay(display);
        }
        if (!session.isEmpty()) {
            auto close = QDBusMessage::createMethodCall(service, session, "org.freedesktop.portal.Session", "Close");
            QDBusConnection::sessionBus().asyncCall(close);
        }
    }
    GlobalShortcut *owner;
    Display *display = nullptr;
    QString session;
  public slots:
    void activated(const QDBusObjectPath &path, const QString &id, qulonglong, const QVariantMap &options) {
        if (path.path() != session || id != "capture") return;
        const auto token = options.value("activation_token").toString();
        if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
        emit owner->triggered();
    }
};
GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() { stop(); }
bool GlobalShortcut::start(const QKeySequence &sequence) {
    stop(); lastError_.clear(); sequence_ = {};
    if (sequence.count() != 1) { lastError_ = tr("请选择单个快捷键组合。"); return false; }
    auto *state = new LinuxShortcut(this);
    handle_ = state;
    if (wayland()) {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<PortalShortcuts>();
        const QString token = "edithere_" + QUuid::createUuid().toString(QUuid::Id128);
        const auto result = portalCall("CreateSession", {}, {{"session_handle_token", token}}, &lastError_);
        state->session = result.value("session_handle").toString();
        if (state->session.isEmpty()) { stop(); return false; }
        QString preferred;
        const auto combo = sequence[0];
        const auto mods = combo.keyboardModifiers();
        if (mods & Qt::ControlModifier) preferred += "CTRL+";
        if (mods & Qt::AltModifier) preferred += "ALT+";
        if (mods & Qt::ShiftModifier) preferred += "SHIFT+";
        if (mods & Qt::MetaModifier) preferred += "LOGO+";
        preferred += QKeySequence(combo.key()).toString(QKeySequence::PortableText);
        QDBusConnection::sessionBus().connect(service, "/org/freedesktop/portal/desktop", shortcutInterface,
            "Activated", state, SLOT(activated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
        PortalShortcuts shortcuts{{"capture", {{"description", tr("截图")}, {"preferred_trigger", preferred}}}};
        const auto bound = portalCall("BindShortcuts", {QVariant::fromValue(QDBusObjectPath(state->session)),
            QVariant::fromValue(shortcuts), QString()}, {}, &lastError_);
        const auto registered = qdbus_cast<PortalShortcuts>(bound.value("shortcuts"));
        if (registered.isEmpty()) {
            if (lastError_.isEmpty()) lastError_ = tr("截图快捷键未能注册。");
            stop(); return false;
        }
    } else {
        if (QGuiApplication::platformName() != "xcb") { lastError_ = tr("当前显示环境不支持全局快捷键。"); stop(); return false; }
        state->display = XOpenDisplay(nullptr);
        if (!state->display) { lastError_ = tr("无法连接 X11 显示服务。"); stop(); return false; }
        auto combo = sequence[0];
        QString keyName = QKeySequence(combo.key()).toString(QKeySequence::PortableText);
        if (combo.key() == Qt::Key_Space) keyName = "space";
        if (combo.key() == Qt::Key_Escape) keyName = "Escape";
        if (combo.key() == Qt::Key_Return) keyName = "Return";
        const auto symbol = XStringToKeysym(keyName.toLatin1().constData());
        const auto code = XKeysymToKeycode(state->display, symbol);
        if (!code) { lastError_ = tr("此按键无法注册为全局快捷键。"); stop(); return false; }
        unsigned int modifiers = 0;
        const auto mods = combo.keyboardModifiers();
        if (mods & Qt::ControlModifier) modifiers |= ControlMask;
        if (mods & Qt::ShiftModifier) modifiers |= ShiftMask;
        if (mods & Qt::AltModifier) modifiers |= Mod1Mask;
        if (mods & Qt::MetaModifier) modifiers |= Mod4Mask;
        unsigned int numLock = 0;
        auto *mapping = XGetModifierMapping(state->display);
        const auto numCode = XKeysymToKeycode(state->display, XK_Num_Lock);
        for (int mod = 0; mod < 8; ++mod)
            for (int key = 0; key < mapping->max_keypermod; ++key)
                if (mapping->modifiermap[mod * mapping->max_keypermod + key] == numCode) numLock = 1u << mod;
        XFreeModifiermap(mapping);
        xError = 0;
        auto previous = XSetErrorHandler(catchXError);
        for (unsigned int lock : {0u, unsigned(LockMask), numLock, numLock | unsigned(LockMask)})
            XGrabKey(state->display, code, modifiers | lock, DefaultRootWindow(state->display),
                     False, GrabModeAsync, GrabModeAsync);
        XSync(state->display, False);
        XSetErrorHandler(previous);
        if (xError) { lastError_ = tr("快捷键已被其他程序占用。"); stop(); return false; }
        auto *notifier = new QSocketNotifier(ConnectionNumber(state->display), QSocketNotifier::Read, state);
        connect(notifier, &QSocketNotifier::activated, state, [state] {
            while (XPending(state->display)) {
                XEvent event; XNextEvent(state->display, &event);
                if (event.type == KeyPress) emit state->owner->triggered();
            }
        });
    }
    sequence_ = sequence;
    return true;
}
void GlobalShortcut::stop() { delete static_cast<LinuxShortcut *>(handle_); handle_ = nullptr; }
bool GlobalShortcut::nativeEventFilter(const QByteArray &, void *, qintptr *) { return false; }
QString globalShortcutLabel() { return wayland() ? tr("截图快捷键（由桌面授权管理）") : tr("截图快捷键"); }
void prepareScreenCapture(QObject *context, std::function<void()> ready, bool) {
    QTimer::singleShot(150, context, std::move(ready));
}
bool supportsScrollingCapture(QString *reason) {
    if (wayland()) return waylandSupportsScrollingCapture(reason);
    if (reason) reason->clear();
    if (QGuiApplication::platformName() != "xcb") {
        if (reason) *reason = tr("当前显示环境不支持持续屏幕采集。");
        return false;
    }
    auto display = openXDisplay();
    if (!display) {
        if (reason) *reason = tr("无法连接 X11 显示服务。");
        return false;
    }
    return true;
}
bool supportsAutomaticScrollInput(QString *reason) {
    if (wayland()) return waylandSupportsAutomaticScrollInput(reason);
    if (!supportsScrollingCapture(reason)) return false;
    auto display = openXDisplay();
    if (!display) { if (reason) *reason = tr("无法连接 X11 显示服务。"); return false; }
    int event = 0, error = 0, major = 0, minor = 0;
    if (!XTestQueryExtension(display.get(), &event, &error, &major, &minor)) {
        if (reason) *reason = tr("X11 显示服务没有启用 XTest，请使用手动滚动。");
        return false;
    }
    return true;
}
void focusScrollingCaptureTarget(const ScrollCaptureTarget &target) {
    // Portal sessions have no authority to activate a foreign application.
    if (wayland() || !validXScreen(nullptr) || !target.window || !target.processId ||
        target.processId == quint32(QCoreApplication::applicationPid())) return;
    auto *display = xScroll.display.get();
    XErrorScope errors(display);
    XWindowAttributes attributes{};
    const auto window = Window(target.window);
    if (XGetWindowAttributes(display, window, &attributes) && attributes.map_state == IsViewable &&
        attributes.root == xScroll.root && windowPid(display, window) == target.processId && !errors.failed())
        XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
}
bool beginScrollingCapture(const ScreenFrame &frame, const QRect &nativeRegion, QString *error) {
    if (wayland()) return waylandBeginScrollingCapture(frame, nativeRegion, error);
    xScroll = {};
    if (!supportsScrollingCapture(error)) return false;
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!frame.nativePixels || frame.nativeGeometry.size() != frame.image.size() || nativeRegion.isEmpty() ||
        !frame.nativeGeometry.contains(nativeRegion) || qint64(nativeRegion.width()) * nativeRegion.height() > MaxPixels)
        return fail(tr("长截图区域无效或过大。"));
    QScreen *screen = nullptr;
    for (auto *candidate : QGuiApplication::screens())
        if (candidate->name() == frame.name && candidate->geometry() == frame.logicalGeometry) { screen = candidate; break; }
    if (!screen) return fail(tr("屏幕布局或缩放已改变，请重新开始长截图。"));
    auto display = openXDisplay();
    if (!display) return fail(tr("无法连接 X11 显示服务。"));
    XErrorScope errors(display.get());
    const auto root = screenRoot(display.get(), screen);
    if (!rootGeometry(display.get(), root).contains(nativeRegion) || errors.failed())
        return fail(tr("长截图区域无效或过大。"));
    xScroll.display = std::move(display);
    xScroll.root = root;
    xScroll.screen = screen;
    xScroll.logicalGeometry = screen->geometry();
    xScroll.nativeGeometry = frame.nativeGeometry;
    xScroll.ratio = screen->devicePixelRatio();
    if (error) error->clear();
    return true;
}
void endScrollingCapture(bool retainScreenSession) {
    xScroll = {};
    waylandEndCapture(retainScreenSession);
}
void prepareScrollFrameRead() {
    if (wayland()) waylandPrepareScrollFrameRead();
}
ScrollCaptureTarget scrollCaptureTargetAt(QPoint point) {
    if (wayland()) return waylandScrollTargetAt(point);
    if (!validXScreen(nullptr) || !xScroll.nativeGeometry.contains(point)) return {};
    XErrorScope errors(xScroll.display.get());
    const auto result = targetAt(xScroll.display.get(), xScroll.root, point);
    return errors.failed() ? ScrollCaptureTarget{} : result;
}
bool scrollCaptureStep(const ScrollCaptureTarget &target, QPoint point, int wheelSteps, QString *error) {
    if (wayland()) return waylandScrollCaptureStep(target, point, wheelSteps, error);
    if (error) error->clear();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!validXScreen(error)) return false;
    if (!wheelSteps || wheelSteps < -8 || wheelSteps > 8 || !xScroll.nativeGeometry.contains(point) ||
        point.x() < std::numeric_limits<short>::min() || point.x() > std::numeric_limits<short>::max() ||
        point.y() < std::numeric_limits<short>::min() || point.y() > std::numeric_limits<short>::max())
        return fail(tr("滚动步长或屏幕坐标无效。"));
    auto *display = xScroll.display.get();
    XErrorScope errors(display);
    const Window window = Window(target.window);
    if (window && !target.processId)
        return fail(tr("无法确认滚动窗口所属进程，请使用手动滚动。"));
    XWindowAttributes attributes{};
    if (!window || !target.processId || target.processId == quint32(QCoreApplication::applicationPid()) ||
        !XGetWindowAttributes(display, window, &attributes) || attributes.map_state != IsViewable ||
        windowPid(display, window) != target.processId || attributes.root != xScroll.root || errors.failed())
        return fail(tr("原滚动窗口已关闭或不可见，长截图已停止。"));
    int event = 0, extensionError = 0, major = 0, minor = 0;
    if (!XTestQueryExtension(display, &event, &extensionError, &major, &minor))
        return fail(tr("X11 显示服务没有启用 XTest，请使用手动滚动。"));
    // Validate the top window and dispatch as one server transaction. Unlike
    // XSendEvent, XTest produces wheel input accepted by browser renderers.
    // No focus or active-window change is made, and the pointer is restored.
    XGrabServer(display);
    const auto current = targetAt(display, xScroll.root, point);
    Window pointerRoot = 0, pointerChild = 0; int oldX = 0, oldY = 0, localX = 0, localY = 0;
    unsigned int modifiers = 0;
    const bool pointer = XQueryPointer(display, xScroll.root, &pointerRoot, &pointerChild,
                                      &oldX, &oldY, &localX, &localY, &modifiers);
    if (current.window != target.window || current.processId != target.processId ||
        !pointer || (modifiers & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask | Button1Mask | Button2Mask | Button3Mask))) {
        XUngrabServer(display);
        return fail(current.window != target.window || current.processId != target.processId ?
            tr("滚动区域被其他窗口遮挡，长截图已停止。") :
            tr("请先松开修饰键和鼠标按键，再使用自动滚动。"));
    }
    const int screen = XScreenNumberOfScreen(attributes.screen);
    bool sent = XTestFakeMotionEvent(display, screen, point.x(), point.y(), CurrentTime);
    const unsigned int button = wheelSteps > 0 ? 5 : 4;
    for (int i = 0; i < qAbs(wheelSteps); ++i) {
        sent &= bool(XTestFakeButtonEvent(display, button, True, CurrentTime));
        sent &= bool(XTestFakeButtonEvent(display, button, False, CurrentTime));
    }
    sent &= bool(XTestFakeMotionEvent(display, screen, oldX, oldY, CurrentTime));
    XUngrabServer(display);
    if (!sent || errors.failed()) return fail(tr("滚动窗口未响应输入，长截图已停止。"));
    return true;
}
void captureScrollRegion(const QRect &nativeRegion, ScrollRegionCallback callback) {
    if (wayland()) { waylandCaptureScrollRegion(nativeRegion, std::move(callback)); return; }
    QString error;
    if (!validXScreen(&error)) { callback({}, error); return; }
    if (nativeRegion.isEmpty() || !xScroll.nativeGeometry.contains(nativeRegion) ||
        qint64(nativeRegion.width()) * nativeRegion.height() > MaxPixels) {
        callback({}, tr("长截图区域无效或过大。")); return;
    }
    QImage image;
    {
        XErrorScope errors(xScroll.display.get());
        if (rootGeometry(xScroll.display.get(), xScroll.root).contains(nativeRegion))
            image = readXRegion(xScroll.display.get(), xScroll.root, nativeRegion);
        if (errors.failed()) image = {};
    }
    error = image.isNull() ? tr("无法读取长截图区域，请检查屏幕采集权限。") : QString();
    callback(std::move(image), error);
}
quintptr captureForegroundWindow() {
    if (wayland() || QGuiApplication::platformName() != "xcb") return 0;
    auto display = openXDisplay();
    if (!display) return 0;
    XErrorScope errors(display.get());
    Window focus = 0; int revert = 0;
    XGetInputFocus(display.get(), &focus, &revert);
    return errors.failed() || focus == PointerRoot ? 0 : quintptr(focus);
}
void restoreCaptureForegroundWindow(quintptr window) {
    if (wayland() || !window || QGuiApplication::platformName() != "xcb") return;
    auto display = openXDisplay();
    if (!display) return;
    XErrorScope errors(display.get());
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display.get(), Window(window), &attributes) && attributes.map_state == IsViewable)
        XSetInputFocus(display.get(), Window(window), RevertToPointerRoot, CurrentTime);
}
QVector<Candidate> linuxAccessibleElements(QPoint, qint64);
QVector<Candidate> nativeElementsAt(QPoint point, qint64 excluded) { return linuxAccessibleElements(point, excluded); }
bool requestAccessibility() { return false; }
void configureNativeWindow(QWidget *, bool) {}
bool excludedFromCapture(const QWidget *) { return false; }
void captureScreens(CaptureCallback callback) {
    if (!wayland()) {
        QVector<ScreenFrame> frames;
        for (auto *screen : QGuiApplication::screens()) {
            auto image = screen->grabWindow(0).toImage();
            image.setDevicePixelRatio(1);
            if (image.isNull() || qint64(image.width()) * image.height() > MaxPixels) {
                callback({}, tr("无法读取屏幕图像。")); return;
            }
            ScreenFrame frame;
            frame.name = screen->name(); frame.logicalGeometry = screen->geometry();
            // X11 arranges monitors in root-window pixels. Qt scales their
            // sizes but preserves each monitor's origin, including negative
            // origins and mixed scale factors. Never multiply that origin.
            frame.nativeGeometry = QRect(screen->geometry().topLeft(), image.size());
            frame.nativePixels = QGuiApplication::platformName() == "xcb";
            frame.image = image;
            frames.append(frame);
        }
        callback(frames, frames.isEmpty() ? tr("没有可截图的屏幕。") : QString());
        return;
    }
    waylandCaptureScreens(std::move(callback));
}
}
#include "platform_linux.moc"
