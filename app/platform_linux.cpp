#include "platform.h"
#include "linuxportal.h"
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
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWidget>
#include <X11/Xlib.h>
#include <X11/keysym.h>

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
    if (reason)
        *reason = wayland() ? tr("此版本暂不支持 Wayland 长截图。") :
                             tr("此版本暂不支持 X11 长截图。");
    return false;
}
ScrollCaptureTarget scrollCaptureTargetAt(QPoint) { return {}; }
bool scrollCaptureStep(const ScrollCaptureTarget &, QPoint, int, QString *error) {
    supportsScrollingCapture(error);
    return false;
}
void captureScrollRegion(const QRect &, ScrollRegionCallback callback) {
    QString error;
    supportsScrollingCapture(&error);
    callback({}, error);
}
quintptr captureForegroundWindow() { return 0; }
void restoreCaptureForegroundWindow(quintptr) {}
QVector<Candidate> linuxAccessibleElements(QPoint, qint64);
QVector<Candidate> nativeElementsAt(QPoint point, qint64 excluded) { return linuxAccessibleElements(point, excluded); }
bool requestAccessibility() { return false; }
void configureNativeWindow(QWidget *, bool) {}
void captureScreens(CaptureCallback callback) {
    if (!wayland()) {
        QVector<ScreenFrame> frames;
        const auto screens = QGuiApplication::screens();
        const double ratio = screens.isEmpty() ? 1.0 : screens.first()->devicePixelRatio();
        bool uniformScale = true;
        for (auto *screen : screens) uniformScale &= qFuzzyCompare(screen->devicePixelRatio(), ratio);
        for (auto *screen : QGuiApplication::screens()) {
            auto image = screen->grabWindow(0).toImage();
            image.setDevicePixelRatio(1);
            if (image.isNull() || qint64(image.width()) * image.height() > MaxPixels) {
                callback({}, tr("无法读取屏幕图像。")); return;
            }
            ScreenFrame frame;
            frame.name = screen->name(); frame.logicalGeometry = screen->geometry();
            if (uniformScale)
                frame.nativeGeometry = QRect(QPoint(qRound(screen->geometry().x() * ratio),
                                                     qRound(screen->geometry().y() * ratio)), image.size());
            frame.image = image;
            frames.append(frame);
        }
        callback(frames, frames.isEmpty() ? tr("没有可截图的屏幕。") : QString());
        return;
    }
    auto *request = new LinuxPortalRequest(qApp);
    request->start("org.freedesktop.portal.Screenshot", "Screenshot", {QString()}, {{"interactive", true}},
        [callback = std::move(callback)](uint code, QVariantMap result) {
            if (code != 0) {
                callback({}, code == 1 ? tr("截图已取消。") : tr("截图授权失败，请确认桌面已安装截图 Portal。")); return;
            }
            const QUrl uri(result.value("uri").toString());
            QImageReader reader(uri.toLocalFile());
            if (!uri.isLocalFile() || !reader.size().isValid() ||
                qint64(reader.size().width()) * reader.size().height() > MaxPixels) {
                callback({}, tr("截图文件无效或过大。")); return;
            }
            auto image = reader.read();
            auto *screen = QGuiApplication::primaryScreen();
            if (image.isNull() || !screen) { callback({}, tr("无法读取屏幕图像。")); return; }
            image.setDevicePixelRatio(1);
            // Portal screenshots may be a selected area or the whole desktop and
            // carry no global coordinates. Review them as one image, preserving
            // aspect ratio, rather than inventing multi-monitor coordinates.
            const auto size = image.size().scaled(screen->availableGeometry().size(), Qt::KeepAspectRatio);
            ScreenFrame frame;
            frame.name = "portal"; frame.image = image;
            frame.logicalGeometry = QRect(screen->availableGeometry().topLeft(), size);
            // No desktop origin is provided by the portal. Disable native probes
            // for this review image rather than attaching unrelated screen UI.
            frame.nativeGeometry = {};
            callback({frame}, {});
        });
}
}
#include "platform_linux.moc"
