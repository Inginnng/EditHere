#include "platform.h"
#include "autostart.h"
#include "linuxportal.h"
#include "scrollcapture.h"
#include <QApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QUrl>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cstdio>
using namespace h2d;
// A separate application owns this window, so the production PID/window lock
// and XTest dispatch are exercised rather than mocked or sent to our own UI.
class X11ScrollFixture final : public QWidget {
  public:
    X11ScrollFixture() : content_(360, 7200, QImage::Format_RGB32) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setWindowTitle("EditHere X11 scrolling fixture");
        setGeometry(70, 80, 360, 280);
        for (int y = 0; y < content_.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(content_.scanLine(y));
            for (int x = 0; x < content_.width(); ++x)
                row[x] = qRgb((x * 13 + y * 3 + (y / 37) * 41) % 251,
                              (x * 7 + y * 19 + (x / 29) * 83) % 251,
                              (x * 23 + y * 11 + (y / 51) * 61) % 251);
        }
    }
  protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).drawImage(QPoint(0, -offset_), content_); }
    void wheelEvent(QWheelEvent *event) override {
        offset_ = qBound(0, offset_ - event->angleDelta().y() / 120 * 40, content_.height() - height());
        update(); event->accept();
    }
  private:
    QImage content_;
    int offset_ = 0;
};
class ScopedX11Session {
  public:
    ScopedX11Session() : previous_(qgetenv("XDG_SESSION_TYPE")) { qputenv("XDG_SESSION_TYPE", "x11"); }
    ~ScopedX11Session() { endScrollingCapture(); qputenv("XDG_SESSION_TYPE", previous_); }
  private:
    QByteArray previous_;
};
class X11FixtureProcess {
  public:
    QProcess process;
    ~X11FixtureProcess() { process.terminate(); if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(); } }
    QJsonObject start() {
        process.start(QCoreApplication::applicationFilePath(), {"--scroll-x11-fixture", "-platform", "xcb"});
        if (!process.waitForStarted(5000) || !process.waitForReadyRead(5000)) return {};
        return QJsonDocument::fromJson(process.readLine()).object();
    }
};
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
    QMap<QString, PortalShortcuts> bindings;
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
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &path, const PortalShortcuts &shortcuts,
                                 const QString &, const QVariantMap &options) {
        bindings.insert(path.path(), shortcuts);
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
    void portalShortcutsHaveIndependentSessionsAndActions() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        MockShortcuts mock;
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", &mock,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "wayland");
        GlobalShortcut capture, annotate;
        capture.setAction("capture", "截图");
        annotate.setAction("annotate", "新建批注（空窗口）");
        QSignalSpy captured(&capture, &GlobalShortcut::triggered);
        QSignalSpy annotated(&annotate, &GlobalShortcut::triggered);
        QVERIFY(annotate.start({}));
        QVERIFY(annotate.sequence().isEmpty());
        QVERIFY(mock.bindings.isEmpty());

        const QKeySequence captureKey("Ctrl+Shift+F8"), annotateKey("Ctrl+Shift+F9");
        QVERIFY2(capture.start(captureKey), qPrintable(capture.lastError()));
        const auto captureSession = mock.session;
        QVERIFY2(annotate.start(annotateKey), qPrintable(annotate.lastError()));
        const auto annotateSession = mock.session;
        QVERIFY(captureSession != annotateSession);
        QCOMPARE(mock.bindings.size(), 2);
        const auto captureBinding = mock.bindings.value(captureSession);
        const auto annotateBinding = mock.bindings.value(annotateSession);
        QCOMPARE(captureBinding.size(), 1);
        QCOMPARE(annotateBinding.size(), 1);
        QCOMPARE(captureBinding.first().id, QString("capture"));
        QCOMPARE(captureBinding.first().properties.value("description").toString(), QString("截图"));
        QCOMPARE(annotateBinding.first().id, QString("annotate"));
        QCOMPARE(annotateBinding.first().properties.value("description").toString(), QString("新建批注（空窗口）"));
        QCOMPARE(annotateBinding.first().properties.value("preferred_trigger").toString(), QString("CTRL+SHIFT+F9"));
        QVERIFY(capture.start(captureKey));
        QCOMPARE(mock.bindings.size(), 2);
        QCOMPARE(capture.sequence(), captureKey);
        QCOMPARE(annotate.sequence(), annotateKey);

        emit mock.Activated(QDBusObjectPath(captureSession), "annotate", 1, {});
        emit mock.Activated(QDBusObjectPath(captureSession), "capture", 2, {});
        QTRY_COMPARE(captured.size(), 1);
        QCOMPARE(annotated.size(), 0);
        emit mock.Activated(QDBusObjectPath(annotateSession), "capture", 3, {});
        emit mock.Activated(QDBusObjectPath(annotateSession), "annotate", 4, {});
        QTRY_COMPARE(annotated.size(), 1);
        QCOMPARE(captured.size(), 1);

        QVERIFY(annotate.start({}));
        QVERIFY(annotate.sequence().isEmpty());
        QVERIFY(annotate.lastError().isEmpty());
        emit mock.Activated(QDBusObjectPath(annotateSession), "annotate", 5, {});
        emit mock.Activated(QDBusObjectPath(captureSession), "capture", 6, {});
        QTRY_COMPARE(captured.size(), 2);
        QCOMPARE(annotated.size(), 1);
        capture.stop();
        QVERIFY(capture.sequence().isEmpty());
        qputenv("XDG_SESSION_TYPE", previous);
        bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterService("org.freedesktop.portal.Desktop");
    }
    void x11ShortcutHandlesConflictsAndActivation() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11; run with xvfb-run -platform xcb");
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "x11");
        GlobalShortcut first, second, independent;
        QSignalSpy triggered(&first, &GlobalShortcut::triggered);
        QSignalSpy independentTriggered(&independent, &GlobalShortcut::triggered);
        QVERIFY(first.start(QKeySequence("Ctrl+Shift+F8")));
        QVERIFY(!second.start(QKeySequence("Ctrl+Shift+F8")));
        QVERIFY(!second.lastError().isEmpty());
        QVERIFY(independent.start(QKeySequence("Ctrl+Shift+F9")));
        auto *display = XOpenDisplay(nullptr); QVERIFY(display);
        for (auto key : {XK_Control_L, XK_Shift_L, XK_F8}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), True, 0);
        for (auto key : {XK_F8, XK_Shift_L, XK_Control_L}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), False, 0);
        XFlush(display); QTRY_COMPARE(triggered.size(), 1);
        QCOMPARE(independentTriggered.size(), 0);
        for (auto key : {XK_Control_L, XK_Shift_L, XK_F9}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), True, 0);
        for (auto key : {XK_F9, XK_Shift_L, XK_Control_L}) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key), False, 0);
        XFlush(display); QTRY_COMPARE(independentTriggered.size(), 1);
        QCOMPARE(triggered.size(), 1);
        XCloseDisplay(display);
        first.stop(); QVERIFY(second.start(QKeySequence("Ctrl+Shift+F8")));
        qputenv("XDG_SESSION_TYPE", previous);
    }
    void x11CaptureReturnsPixels() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11");
        const auto previous = qgetenv("XDG_SESSION_TYPE"); qputenv("XDG_SESSION_TYPE", "x11");
        bool done = false; QVector<ScreenFrame> frames;
        captureScreens([&](auto value, auto) { frames = value; done = true; });
        QVERIFY(done); QVERIFY(!frames.isEmpty()); QVERIFY(!frames.first().image.isNull());
        QVERIFY(frames.first().nativePixels);
        QCOMPARE(frames.first().nativeGeometry.size(), frames.first().image.size());
        QCOMPARE(frames.first().nativeGeometry.topLeft(), frames.first().logicalGeometry.topLeft());
        qputenv("XDG_SESSION_TYPE", previous);
    }
    void x11ContinuousSamplingAndNativeWheelKeepLockedWindowAndPixels() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs real X11; use xvfb-run with -platform xcb");
        ScopedX11Session session;
        X11FixtureProcess fixture;
        const auto properties = fixture.start();
        QVERIFY2(!properties.isEmpty(), fixture.process.readAllStandardError().constData());
        const QRect region(properties["x"].toInt(), properties["y"].toInt(),
                           properties["width"].toInt(), properties["height"].toInt());
        const auto window = properties["window"].toString().toULongLong();
        QString error;
        QVERIFY2(supportsScrollingCapture(&error), qPrintable(error));
        QVERIFY2(supportsAutomaticScrollInput(&error), qPrintable(error));
        QVector<ScreenFrame> frames;
        captureScreens([&](auto value, auto message) { frames = value; error = message; });
        QVERIFY2(!frames.isEmpty(), qPrintable(error));
        const auto found = std::find_if(frames.cbegin(), frames.cend(), [&](const ScreenFrame &frame) {
            return frame.nativeGeometry.contains(region);
        });
        QVERIFY(found != frames.cend());
        QVERIFY2(beginScrollingCapture(*found, region, &error), qPrintable(error));
        const auto target = scrollCaptureTargetAt(region.center());
        QCOMPARE(target.window, quintptr(window));
        QCOMPARE(target.processId, quint32(fixture.process.processId()));
        QImage first, second;
        captureScrollRegion(region, [&](auto image, auto message) { first = image; error = message; });
        QVERIFY2(!first.isNull(), qPrintable(error));
        QCOMPARE(first.size(), region.size());
        QCOMPARE(first, found->image.copy(region.translated(-found->nativeGeometry.topLeft())));
        auto *display = XOpenDisplay(nullptr); QVERIFY(display);
        XSetInputFocus(display, DefaultRootWindow(display), RevertToPointerRoot, CurrentTime);
        XSync(display, False);
        const auto rootFocus = captureForegroundWindow();
        focusScrollingCaptureTarget({target.window, target.processId + 1});
        QCOMPARE(captureForegroundWindow(), rootFocus);
        focusScrollingCaptureTarget(target);
        QCOMPARE(captureForegroundWindow(), target.window);
        const auto focus = captureForegroundWindow();
        ScrollCapture capture;
        capture.begin(first);
        QVERIFY2(scrollCaptureStep(target, region.center(), 1, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            captureScrollRegion(region, [&](auto image, auto message) { second = image; error = message; });
            return !second.isNull() && second != first;
        })(), 5000);
        QCOMPARE(captureForegroundWindow(), focus);
        const int movement = qRound(properties["ratio"].toDouble() * 40);
        QCOMPARE(second.copy(0, 0, region.width(), region.height() - movement),
                 first.copy(0, movement, region.width(), region.height() - movement));
        QCOMPARE(capture.take(second), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.size(), QSize(region.width(), region.height() + movement));
        QImage joined(region.width(), region.height() + movement, QImage::Format_ARGB32);
        { QPainter painter(&joined); painter.drawImage(QPoint(), first); painter.drawImage(QPoint(0, movement), second); }
        QCOMPARE(capture.picture(), joined);
        QVERIFY2(scrollCaptureStep(target, region.center(), -1, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            captureScrollRegion(region, [&](auto image, auto message) { second = image; error = message; });
            return second == first;
        })(), 5000);
        QCOMPARE(capture.take(second), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.viewportRect().top(), 0);
        QCOMPARE(capture.picture(), joined);

        // A different client above the selected point must stop dispatch;
        // destroying the old client must not silently retarget another window.
        const Window blocker = XCreateSimpleWindow(display, DefaultRootWindow(display), region.x(), region.y(),
                                                    region.width(), region.height(), 0, 0, 0xff0000);
        const unsigned long foreignPid = target.processId + 900;
        XChangeProperty(display, blocker, XInternAtom(display, "_NET_WM_PID", False), XA_CARDINAL,
                        32, PropModeReplace, reinterpret_cast<const unsigned char *>(&foreignPid), 1);
        XMapRaised(display, blocker); XSync(display, False);
        QVERIFY(!scrollCaptureStep(target, region.center(), 1, &error));
        QVERIFY2(error.contains(QString::fromUtf8("遮挡")), qPrintable(error));
        XDestroyWindow(display, blocker); XSync(display, False);
        QVERIFY(!scrollCaptureStep(target, region.center(), 0, &error));
        QVERIFY(!scrollCaptureStep(target, region.center(), 9, &error));
        QVERIFY(!scrollCaptureStep({target.window, target.processId + 1}, region.center(), 1, &error));
        XDeleteProperty(display, Window(window), XInternAtom(display, "_NET_WM_PID", False));
        XSync(display, False);
        const auto anonymousTarget = scrollCaptureTargetAt(region.center());
        QCOMPARE(anonymousTarget.window, target.window);
        QCOMPARE(anonymousTarget.processId, quint32(0));
        captureScrollRegion(region, [&](auto image, auto message) { second = image; error = message; });
        QCOMPARE(second, first);
        QVERIFY(!scrollCaptureStep(anonymousTarget, region.center(), 1, &error));
        fixture.process.terminate(); QVERIFY(fixture.process.waitForFinished(5000));
        QVERIFY(!scrollCaptureStep(target, region.center(), 1, &error));
        QVERIFY2(error.contains(QString::fromUtf8("关闭")), qPrintable(error));
        XCloseDisplay(display);
        endScrollingCapture();
        captureScrollRegion(region, [&](auto image, auto message) { second = image; error = message; });
        QVERIFY(second.isNull()); QVERIFY(!error.isEmpty());
    }
    void x11RejectsInventedAndOutOfScreenCaptureCoordinates() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11");
        ScopedX11Session session;
        QVector<ScreenFrame> frames; QString error;
        captureScreens([&](auto value, auto message) { frames = value; error = message; });
        QVERIFY(!frames.isEmpty());
        auto frame = frames.first();
        frame.nativePixels = false;
        QVERIFY(!beginScrollingCapture(frame, QRect(20, 20, 200, 200), &error));
        frame = frames.first();
        QVERIFY(!beginScrollingCapture(frame, QRect(-100, -100, 200, 200), &error));
        QVERIFY(beginScrollingCapture(frame, QRect(frame.nativeGeometry.topLeft(), QSize(200, 200)), &error));
        QImage result;
        captureScrollRegion(frame.nativeGeometry.adjusted(-1, 0, 0, 0),
                            [&](auto image, auto message) { result = image; error = message; });
        QVERIFY(result.isNull()); QVERIFY(!error.isEmpty());
    }
    void x11ManualCaptureDoesNotRequireInputInjection() {
        if (QGuiApplication::platformName() != "xcb") QSKIP("Needs X11");
        ScopedX11Session session;
        QString error;
        QVERIFY2(supportsScrollingCapture(&error), qPrintable(error));
        auto *display = XOpenDisplay(nullptr); QVERIFY(display);
        int event = 0, extensionError = 0, major = 0, minor = 0;
        const bool hasXTest = XTestQueryExtension(display, &event, &extensionError, &major, &minor);
        XCloseDisplay(display);
        QCOMPARE(supportsAutomaticScrollInput(&error), hasXTest);
        if (!hasXTest) QVERIFY(error.contains("XTest"));
        QVector<ScreenFrame> frames;
        captureScreens([&](auto value, auto message) { frames = value; error = message; });
        QVERIFY2(!frames.isEmpty(), qPrintable(error));
        const auto &frame = frames.first();
        const QRect region(frame.nativeGeometry.topLeft() + QPoint(20, 20), QSize(200, 200));
        QVERIFY2(beginScrollingCapture(frame, region, &error), qPrintable(error));
        QImage sampled;
        captureScrollRegion(region, [&](auto image, auto message) { sampled = image; error = message; });
        QVERIFY2(!sampled.isNull(), qPrintable(error));
        QCOMPARE(sampled, frame.image.copy(region.translated(-frame.nativeGeometry.topLeft())));
    }
};
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    if (application.arguments().contains("--scroll-x11-fixture")) {
        X11ScrollFixture fixture;
        fixture.show();
        QTimer::singleShot(200, &fixture, [&] {
            auto *display = XOpenDisplay(nullptr);
            XWindowAttributes attributes{};
            int x = 0, y = 0; Window child = 0;
            XGetWindowAttributes(display, Window(fixture.winId()), &attributes);
            XTranslateCoordinates(display, Window(fixture.winId()), attributes.root, 0, 0, &x, &y, &child);
            const QJsonObject properties{{"window", QString::number(fixture.winId())}, {"x", x}, {"y", y},
                {"width", attributes.width}, {"height", attributes.height}, {"ratio", fixture.devicePixelRatioF()}};
            const auto output = QJsonDocument(properties).toJson(QJsonDocument::Compact) + '\n';
            fwrite(output.constData(), 1, size_t(output.size()), stdout); fflush(stdout);
            XCloseDisplay(display);
        });
        return application.exec();
    }
    LinuxPlatformTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "linux_platform_test.moc"
