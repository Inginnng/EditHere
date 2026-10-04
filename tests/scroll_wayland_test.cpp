#include "scroll_wayland.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QPointer>
#include <QScreen>
#include <QTest>
#include <QTimer>
#include <fcntl.h>
#include <unistd.h>
using namespace h2d;
struct Pair { int x = 0, y = 0; };
struct Stream { quint32 node = 0; QVariantMap properties; };
using Streams = QList<Stream>;
Q_DECLARE_METATYPE(Pair)
Q_DECLARE_METATYPE(Stream)
Q_DECLARE_METATYPE(Streams)
QDBusArgument &operator<<(QDBusArgument &argument, const Pair &pair) {
    argument.beginStructure(); argument << pair.x << pair.y; argument.endStructure(); return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Pair &pair) {
    argument.beginStructure(); argument >> pair.x >> pair.y; argument.endStructure(); return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const Stream &stream) {
    argument.beginStructure(); argument << stream.node << stream.properties; argument.endStructure(); return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Stream &stream) {
    argument.beginStructure(); argument >> stream.node >> stream.properties; argument.endStructure(); return argument;
}
class MockRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
  public:
    using QObject::QObject;
    int *closed = nullptr;
    int closes = 0;
  public slots:
    void Close() { ++closes; if (closed) ++*closed; }
  signals:
    void Response(uint code, const QVariantMap &results);
};
class MockSession : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Session")
  public:
    int closes = 0;
  public slots:
    void Close() { ++closes; }
  signals:
    void Closed(const QVariantMap &details);
};
class MockScreenCast : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.ScreenCast")
    Q_PROPERTY(uint AvailableSourceTypes READ available)
  public:
    MockSession session;
    QString sessionPath;
    QVariantMap properties;
    int creates = 0, selects = 0, starts = 0, opens = 0, cancelledRequests = 0;
    uint responseCode = 0;
    bool holdStart = false;
    QPointer<MockRequest> lastRequest;
    uint available() const { return 1; }
    QDBusObjectPath request(const QVariantMap &options, QVariantMap values, bool hold = false) {
        QString sender = message().service().mid(1); sender.replace('.', '_');
        const auto path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + options.value("handle_token").toString();
        auto *object = new MockRequest(this); object->closed = &cancelledRequests;
        lastRequest = object;
        QDBusConnection::sessionBus().registerObject(path, object,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        if (!hold) QTimer::singleShot(0, object, [object, code = responseCode, values] {
            emit object->Response(code, values);
        });
        return QDBusObjectPath(path);
    }
  public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options) {
        ++creates;
        QString sender = message().service().mid(1); sender.replace('.', '_');
        sessionPath = "/org/freedesktop/portal/desktop/session/" + sender + "/" +
                      options.value("session_handle_token").toString();
        QDBusConnection::sessionBus().registerObject(sessionPath, &session,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        return request(options, {{"session_handle", sessionPath}});
    }
    QDBusObjectPath SelectSources(const QDBusObjectPath &handle, const QVariantMap &options) {
        ++selects;
        if (handle.path() != sessionPath || options.value("types").toUInt() != 1 ||
            options.value("multiple").toBool()) responseCode = 2;
        return request(options, {});
    }
    QDBusObjectPath Start(const QDBusObjectPath &, const QString &, const QVariantMap &options) {
        ++starts;
        return request(options, {{"streams", QVariant::fromValue(Streams{{31, properties}})}}, holdStart);
    }
    QDBusUnixFileDescriptor OpenPipeWireRemote(const QDBusObjectPath &, const QVariantMap &) {
        ++opens;
        const int fd = open("/dev/null", O_RDONLY);
        QDBusUnixFileDescriptor result(fd); close(fd); return result;
    }
};
class FakeVideoSource : public WaylandVideoSource {
  public:
    using WaylandVideoSource::WaylandVideoSource;
    int starts = 0, stops = 0, discarded = 0;
    quint32 node = 0;
    quint64 serial = 0;
    bool start(int ownedFd, quint32 target, quint64 targetSerial, QString *) override {
        close(ownedFd); ++starts; node = target; serial = targetSerial; return true;
    }
    void stop() override { ++stops; }
    void discardQueuedFrames() override { ++discarded; }
    void send(QImage image, qint64 time = waylandVideoClockNs()) { emit frameReady(image, time); }
};
class ScrollWaylandTests : public QObject {
    Q_OBJECT
    std::unique_ptr<MockScreenCast> portal;
    QPointer<FakeVideoSource> video;
    std::unique_ptr<WaylandCaptureSession> session;
    ScreenFrame initial;
    QString error;
    bool done = false;
    void startSession() {
        session = std::make_unique<WaylandCaptureSession>(nullptr, [this](QObject *parent) {
            video = new FakeVideoSource(parent); return video.data();
        });
        done = false; error.clear(); initial = {};
        session->start([this](QVector<ScreenFrame> frames, QString message) {
            if (!frames.isEmpty()) initial = frames.first();
            error = message; done = true;
        });
    }
    QImage image() const {
        const auto screen = QGuiApplication::primaryScreen();
        QImage result(screen->geometry().size() * 2, QImage::Format_RGB32);
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                result.setPixel(x, y, qRgb(x % 255, y % 255, (x + y) % 255));
        return result;
    }
  private slots:
    void initTestCase() {
        qDBusRegisterMetaType<Pair>(); qDBusRegisterMetaType<Stream>(); qDBusRegisterMetaType<Streams>();
    }
    void init() {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) QSKIP("Run with dbus-run-session");
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        portal = std::make_unique<MockScreenCast>();
        const auto geometry = QGuiApplication::primaryScreen()->geometry();
        portal->properties = {{"source_type", uint(1)}, {"id", "monitor-unique"},
            {"position", QVariant::fromValue(Pair{geometry.x(), geometry.y()})},
            {"size", QVariant::fromValue(Pair{geometry.width(), geometry.height()})},
            {"pipewire-serial", qulonglong(99)}};
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", portal.get(),
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
    }
    void cleanup() {
        session.reset();
        QCoreApplication::processEvents();
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject("/org/freedesktop/portal/desktop", QDBusConnection::UnregisterTree);
        bus.unregisterService("org.freedesktop.portal.Desktop");
        portal.reset();
    }
    void mapsOnlyAnUnambiguousMonitor() {
        QString name, error; QRect logical;
        QVariantMap metadata{{"source_type", uint(1)}, {"position", QVariantList{-800, 0}},
                             {"size", QVariantList{800, 600}}};
        QVector<WaylandScreenGeometry> screens{{"left", QRect(-800, 0, 800, 600)}, {"right", QRect(0, 0, 1024, 768)}};
        QVERIFY(mapWaylandMonitor(metadata, screens, &name, &logical, &error));
        QCOMPARE(name, QString("left")); QCOMPARE(logical, screens.first().logical);
        metadata.remove("position"); QVERIFY(!mapWaylandMonitor(metadata, screens, &name, &logical, &error));
        metadata.insert("position", QVariantList{-800, 0});
        screens.append(screens.first()); QVERIFY(!mapWaylandMonitor(metadata, screens, &name, &logical, &error));
        screens.removeLast(); metadata.insert("source_type", uint(2));
        QVERIFY(!mapWaylandMonitor(metadata, screens, &name, &logical, &error));
    }
    void pixelConversionHonoursPaddingAndRejectsShortBuffers() {
        const uchar bytes[] = {1, 2, 3, 9, 4, 5, 6, 9, 0, 0, 0, 0,
                               7, 8, 9, 9, 10, 11, 12, 9};
        const auto converted = copyWaylandVideoFrame(bytes, sizeof(bytes), QSize(2, 2), 12, WaylandPixelFormat::Bgrx);
        QCOMPARE(converted.pixel(0, 0), qRgb(3, 2, 1)); QCOMPARE(converted.pixel(1, 1), qRgb(12, 11, 10));
        QVERIFY(copyWaylandVideoFrame(bytes, 19, QSize(2, 2), 12, WaylandPixelFormat::Bgrx).isNull());
        QVERIFY(copyWaylandVideoFrame(bytes, sizeof(bytes), QSize(2, 2), 7, WaylandPixelFormat::Bgrx).isNull());
        QVERIFY(copyWaylandVideoFrame(bytes, sizeof(bytes), QSize(2, 2), -12, WaylandPixelFormat::Bgrx).isNull());
    }
    void videoTimestampPreservesProductionAgeAcrossClockOffsets() {
        QCOMPARE(mapWaylandVideoTimestamp(50, 100, 1000), qint64(950));
        QCOMPARE(mapWaylandVideoTimestamp(100, 100, 1000), qint64(1000));
        QCOMPARE(mapWaylandVideoTimestamp(-1, 100, 1000), qint64(0));
        QCOMPARE(mapWaylandVideoTimestamp(101, 100, 1000), qint64(0));
        QCOMPARE(mapWaylandVideoTimestamp(1, 10'000'000'001LL, 20'000'000'000LL), qint64(0));
    }
    void persistentSessionUsesNativePixelsAndOnlyFreshFrames() {
        startSession(); QTRY_VERIFY(video && video->starts == 1);
        const auto first = image(); video->send(first); QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(initial.nativePixels);
        QVERIFY(!initial.elementProbingAllowed); QCOMPARE(initial.image, first);
        QCOMPARE(initial.nativeGeometry, first.rect()); QCOMPARE(video->node, uint(31)); QCOMPARE(video->serial, quint64(99));
        const QRect region(11, 19, 128, 200);
        QVERIFY(session->begin(initial, region, &error));
        auto target = session->targetAt(region.center());
        QCOMPARE(target.window, quintptr(0)); QCOMPARE(target.processId, uint(0)); QVERIFY(target.captureSession != 0);
        bool read = false; QImage captured;
        const qint64 staleTime = waylandVideoClockNs() - 1000000;
        session->capture(region, [&](QImage value, QString message) { captured = value; error = message; read = true; });
        QCOMPARE(video->discarded, 1);
        video->send(first, staleTime); QVERIFY(!read);
        QTest::qWait(1); video->send(first); QVERIFY(read); QCOMPARE(captured, first.copy(region));
        QCOMPARE(portal->creates, 1); QCOMPARE(portal->selects, 1); QCOMPARE(portal->starts, 1); QCOMPARE(portal->opens, 1);
        QCOMPARE(session->targetAt(region.center()).captureSession, target.captureSession);
        session->stop(); QTRY_COMPARE(portal->session.closes, 1); QVERIFY(!video);
        QVERIFY(!session->active()); QVERIFY(!session->targetAt(region.center()).captureSession);
    }
    void readReplacementCannotBeFailedByTheOldTimeout() {
        startSession(); QTRY_VERIFY(video && video->starts == 1); video->send(image()); QTRY_VERIFY(done);
        const QRect region(0, 0, 128, 200); QVERIFY(session->begin(initial, region, &error));
        int oldReads = 0; bool newRead = false;
        session->capture(region, [&](QImage, QString) { ++oldReads; });
        QTest::qWait(900);
        session->capture(region, [&](QImage value, QString message) { newRead = true; QVERIFY(!value.isNull()); QVERIFY(message.isEmpty()); });
        QCOMPARE(oldReads, 1);
        QTest::qWait(650); QVERIFY(!newRead);
        video->send(image()); QVERIFY(newRead); QCOMPARE(oldReads, 1);
    }
    void preparationAcceptsOnlyThisRoundsAlreadyDeliveredCleanFrame() {
        startSession(); QTRY_VERIFY(video && video->starts == 1); video->send(image()); QTRY_VERIFY(done);
        const QRect region(0, 0, 128, 200); QVERIFY(session->begin(initial, region, &error));
        session->prepareRead();
        QTest::qWait(1); const auto clean = image(); video->send(clean);
        QTest::qWait(35);
        bool read = false;
        session->capture(region, [&](QImage value, QString message) {
            QCOMPARE(value, clean.copy(region)); QVERIFY(message.isEmpty()); read = true;
        });
        QVERIFY(read);
        // A second round cannot reuse the previous round's clean frame after
        // controls have been restored; it needs a new post-hide frame.
        session->prepareRead(); read = false;
        session->capture(region, [&](QImage value, QString message) {
            QCOMPARE(value, clean.copy(region)); QVERIFY(message.isEmpty()); read = true;
        });
        QVERIFY(!read); QTest::qWait(1); video->send(clean); QVERIFY(read);
    }
    void cancelPickerClosesTheOutstandingRequestAndSession() {
        portal->holdStart = true; startSession(); QTRY_COMPARE(portal->starts, 1);
        QVERIFY(!done); session->stop(); QVERIFY(done); QVERIFY(!error.isEmpty());
        QTRY_COMPARE(portal->lastRequest->closes, 1); QTRY_COMPARE(portal->session.closes, 1);
        QVERIFY(!session->active());
    }
    void revokedSessionFailsPendingReadAndCleansUp() {
        startSession(); QTRY_VERIFY(video && video->starts == 1); video->send(image()); QTRY_VERIFY(done);
        const QRect region(0, 0, 128, 200); QVERIFY(session->begin(initial, region, &error));
        bool read = false;
        session->capture(region, [&](QImage value, QString message) { QVERIFY(value.isNull()); QVERIFY(!message.isEmpty()); read = true; });
        emit portal->session.Closed({}); QTRY_VERIFY(read); QVERIFY(!session->active()); QVERIFY(!video);
    }
    void returningToSelectionRetainsAuthorizationButClearsTheReadLock() {
        startSession(); QTRY_VERIFY(video && video->starts == 1); video->send(image()); QTRY_VERIFY(done);
        const QRect region(0, 0, 128, 200); QVERIFY(session->begin(initial, region, &error));
        const quint64 identity = session->targetAt(region.center()).captureSession;
        bool cancelled = false;
        session->capture(region, [&](QImage value, QString message) {
            QVERIFY(value.isNull()); QVERIFY(!message.isEmpty()); cancelled = true;
        });
        session->releaseSelection(); QVERIFY(cancelled); QVERIFY(session->active());
        QVERIFY(!session->targetAt(region.center()).captureSession); QCOMPARE(portal->session.closes, 0);
        QVERIFY(session->begin(initial, region, &error));
        QCOMPARE(session->targetAt(region.center()).captureSession, identity);
        QCOMPARE(portal->starts, 1);
    }
    void metadataFailureNeverPretendsToSelectThePrimaryScreen() {
        portal->properties.remove("position"); startSession(); QTRY_VERIFY(done);
        QVERIFY(initial.image.isNull()); QVERIFY(!error.isEmpty()); QCOMPARE(portal->opens, 0);
        QTRY_COMPARE(portal->session.closes, 1);
    }
    void userDenialDoesNotOpenTheVideoTransport() {
        portal->responseCode = 1; startSession(); QTRY_VERIFY(done);
        QVERIFY(initial.image.isNull()); QVERIFY(!error.isEmpty());
        QCOMPARE(portal->selects, 0); QCOMPARE(portal->opens, 0); QVERIFY(!video);
    }
    void userDenialCallbackCanSynchronouslyDestroyItsSession() {
        portal->responseCode = 1;
        session = std::make_unique<WaylandCaptureSession>(nullptr, [this](QObject *parent) {
            video = new FakeVideoSource(parent); return video.data();
        });
        QPointer<WaylandCaptureSession> alive(session.get());
        int callbacks = 0;
        session->start([&](QVector<ScreenFrame> frames, QString message) {
            ++callbacks;
            QVERIFY(frames.isEmpty()); QVERIFY(!message.isEmpty());
            session.reset();
        });
        QTRY_COMPARE(callbacks, 1);
        QVERIFY(!alive); QVERIFY(!video);
        QTRY_COMPARE(portal->session.closes, 1);
        QTRY_VERIFY(portal->lastRequest);
        QTRY_COMPARE(portal->lastRequest->closes, 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QCOMPARE(callbacks, 1);
        QCOMPARE(portal->selects, 0); QCOMPARE(portal->opens, 0);
    }
    void changedPixelGeometryStopsTheLockedSession() {
        startSession(); QTRY_VERIFY(video && video->starts == 1); video->send(image()); QTRY_VERIFY(done);
        const QRect region(0, 0, 128, 200); QVERIFY(session->begin(initial, region, &error));
        bool read = false;
        session->capture(region, [&](QImage value, QString message) { QVERIFY(value.isNull()); QVERIFY(!message.isEmpty()); read = true; });
        video->send(image().scaledToWidth(image().width() / 2)); QVERIFY(read); QVERIFY(!session->active());
    }
    void automaticInputRequiresRealRemoteDesktopAuthorization() {
        QString reason;
        QVERIFY(!waylandSupportsAutomaticScrollInput(&reason)); QVERIFY(!reason.isEmpty());
        QVERIFY(!waylandScrollCaptureStep({}, {}, 1, &reason)); QVERIFY(!reason.isEmpty());
    }
};
QTEST_MAIN(ScrollWaylandTests)
#include "scroll_wayland_test.moc"
