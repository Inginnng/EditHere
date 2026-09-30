#include "platform.h"
#include "autostart.h"
#include "linuxportal.h"
#include <QApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QUrl>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
using namespace h2d;
class MockRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
  public:
    uint result = 0;
    QVariantMap values;
  public slots:
    void Close() {}
  signals:
    void Response(uint code, const QVariantMap &results);
};
class MockScreenshot : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
  public:
    QString uri;
    uint result = 0;
    MockRequest *last = nullptr;
  public slots:
    QDBusObjectPath Screenshot(const QString &, const QVariantMap &options) {
        QString sender = message().service().mid(1); sender.replace('.', '_');
        const QString path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + options.value("handle_token").toString();
        auto *request = new MockRequest;
        request->setParent(this); last = request;
        QDBusConnection::sessionBus().registerObject(path, request, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        QTimer::singleShot(0, request, [request, code = result, uri = uri] { emit request->Response(code, {{"uri", uri}}); });
        return QDBusObjectPath(path);
    }
};
class MockShortcuts : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts")
  public:
    QString session;
    uint result = 0;
    QString preferred;
    QDBusObjectPath request(const QVariantMap &options, const QVariantMap &values) {
        QString sender = message().service().mid(1); sender.replace('.', '_');
        const auto path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + options.value("handle_token").toString();
        auto *object = new MockRequest; object->setParent(this);
        QDBusConnection::sessionBus().registerObject(path, object, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        QTimer::singleShot(0, object, [object, code = result, values] { emit object->Response(code, values); });
        return QDBusObjectPath(path);
    }
  public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options) {
        session = "/org/freedesktop/portal/desktop/session/test/" + options.value("session_handle_token").toString();
        return request(options, {{"session_handle", session}});
    }
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &, const PortalShortcuts &shortcuts,
                                 const QString &, const QVariantMap &options) {
        if (!shortcuts.isEmpty()) preferred = shortcuts.first().properties.value("preferred_trigger").toString();
        return request(options, {{"shortcuts", QVariant::fromValue(shortcuts)}});
    }
  signals:
    void Activated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp, const QVariantMap &options);
};
class LinuxPlatformTests : public QObject {
    Q_OBJECT
  private slots:
    void startupUsesIsolatedUserDirectoryAndOriginalAppImage() {
        QTemporaryDir directory;
        const auto previousConfig = qgetenv("XDG_CONFIG_HOME"), previousImage = qgetenv("APPIMAGE");
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        const QString executable = directory.filePath("应用 & quote\" $% AppImage");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));
        QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("APPIMAGE", executable.toUtf8());
        QString error;
        QVERIFY2(setLaunchAtLoginEnabled(true, &error), qPrintable(error));
        QVERIFY(launchAtLoginEnabled(&error));
        QVERIFY(launchAtLoginNotice().isEmpty());
        const auto desktop = directory.filePath("autostart/com.edithere.capture.desktop");
        QCOMPARE(QProcess::execute("desktop-file-validate", {desktop}), 0);
        qputenv("APPIMAGE", directory.filePath("moved.AppImage").toUtf8());
        QVERIFY(!launchAtLoginNotice().isEmpty());
        QVERIFY(setLaunchAtLoginEnabled(false, &error));
        QVERIFY(!QFile::exists(desktop));
        qputenv("XDG_CONFIG_HOME", previousConfig); qputenv("APPIMAGE", previousImage);
    }
    void portalScreenshotSuccessAndCancellation() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        MockScreenshot mock;
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", &mock, QDBusConnection::ExportAllSlots));
        QTemporaryDir directory;
        QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::red);
        const auto file = directory.filePath("portal.png"); QVERIFY(image.save(file));
        mock.uri = QUrl::fromLocalFile(file).toString();
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "wayland");
        bool done = false;
        QVector<ScreenFrame> frames; QString error;
        captureScreens([&](auto value, auto message) { frames = value; error = message; done = true; });
        QTRY_VERIFY(done); QCOMPARE(frames.size(), 1); QVERIFY(error.isEmpty());
        QCOMPARE(frames[0].image, image); QVERIFY(!frames[0].nativePixels);
        mock.result = 1; done = false;
        captureScreens([&](auto value, auto message) { frames = value; error = message; done = true; });
        QTRY_VERIFY(done); QVERIFY(frames.isEmpty()); QVERIFY(!error.isEmpty());
        mock.result = 0; mock.uri = "https://example.com/not-local.png"; done = false;
        captureScreens([&](auto value, auto message) { frames = value; error = message; done = true; });
        QTRY_VERIFY(done); QVERIFY(frames.isEmpty()); QVERIFY(!error.isEmpty());
        qputenv("XDG_SESSION_TYPE", previous);
        bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void missingShortcutPortalFailsWithoutClaimingRegistration() {
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "wayland");
        GlobalShortcut shortcut;
        QVERIFY(!shortcut.start(QKeySequence("Ctrl+Shift+F8")));
        QVERIFY(!shortcut.lastError().isEmpty());
        QVERIFY(shortcut.sequence().isEmpty());
        qputenv("XDG_SESSION_TYPE", previous);
    }
    void portalShortcutRegistersActivatesAndRejectsCancellation() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        MockShortcuts mock;
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", &mock,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "wayland");
        GlobalShortcut shortcut;
        QSignalSpy activated(&shortcut, &GlobalShortcut::triggered);
        QVERIFY2(shortcut.start(QKeySequence("Ctrl+Shift+F8")), qPrintable(shortcut.lastError()));
        QCOMPARE(mock.preferred, QString("CTRL+SHIFT+F8"));
        emit mock.Activated(QDBusObjectPath(mock.session), "capture", 1, {});
        QTRY_COMPARE(activated.size(), 1);
        mock.result = 1;
        QVERIFY(!shortcut.start(QKeySequence("Ctrl+Shift+F9")));
        QVERIFY(shortcut.sequence().isEmpty());
        qputenv("XDG_SESSION_TYPE", previous);
        bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void x11ShortcutHandlesConflictsAndActivation() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11; run with xvfb-run -platform xcb");
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "x11");
        GlobalShortcut first, second;
        QSignalSpy triggered(&first, &GlobalShortcut::triggered);
        QVERIFY(first.start(QKeySequence("Ctrl+Shift+F8")));
        QVERIFY(!second.start(QKeySequence("Ctrl+Shift+F8")));
        QVERIFY(!second.lastError().isEmpty());
        auto *display = XOpenDisplay(nullptr); QVERIFY(display);
        for (auto key : {XK_Control_L, XK_Shift_L, XK_F8}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), True, 0);
        for (auto key : {XK_F8, XK_Shift_L, XK_Control_L}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), False, 0);
        XFlush(display); QTRY_COMPARE(triggered.size(), 1); XCloseDisplay(display);
        first.stop(); QVERIFY(second.start(QKeySequence("Ctrl+Shift+F8")));
        qputenv("XDG_SESSION_TYPE", previous);
    }
    void x11CaptureReturnsPixels() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11");
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "x11");
        bool done = false; QVector<ScreenFrame> frames;
        captureScreens([&](auto value, auto) { frames = value; done = true; });
        QVERIFY(done); QVERIFY(!frames.isEmpty()); QVERIFY(!frames.first().image.isNull());
        qputenv("XDG_SESSION_TYPE", previous);
    }
};
QTEST_MAIN(LinuxPlatformTests)
#include "linux_platform_test.moc"
