#include "windowscapture.h"
#include "platform.h"
#include "webview2host.h"
#include <QApplication>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPainter>
#include <QScreen>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <qfloat16.h>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>

using namespace h2d;
using namespace h2d::windowscapture;
namespace {
float linear(int srgb) {
    const float value = float(srgb) / 255;
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
void setHalf(QByteArray &bytes, qsizetype offset, float value) {
    const qfloat16 half(value);
    std::memcpy(bytes.data() + offset, &half, sizeof(half));
}
bool runningOnInputDesktop() {
    auto desktopName = [](HDESK desktop) -> QString {
        if (!desktop) return {};
        wchar_t name[256]{};
        DWORD needed = 0;
        if (!GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed))
            return {};
        return QString::fromWCharArray(name);
    };
    const QString current = desktopName(GetThreadDesktop(GetCurrentThreadId()));
    const HDESK input = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!input) return true;
    const QString active = desktopName(input);
    CloseDesktop(input);
    return current.isEmpty() || active.isEmpty() || current.compare(active, Qt::CaseInsensitive) == 0;
}
class ColorBars final : public QWidget {
  public:
    bool reversed = false;
  private:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(reversed ? "#1f2328" : "#f0f1f2"));
        painter.fillRect(0, 0, width() / 2, height(), QColor(reversed ? "#f0f1f2" : "#1f2328"));
    }
};
}
class WindowsCaptureTests final : public QObject {
    Q_OBJECT
  private slots:
    void webView2StartsAndReceivesPageMessages() {
        if (!WebView2Host::runtimeAvailable()) QSKIP("Requires the WebView2 Runtime");

        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile page(fixture.filePath("bridge.html"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        const QByteArray html = R"(<script>chrome.webview.postMessage({kind: "ready"});</script>)";
        QCOMPARE(page.write(html), html.size());
        page.close();

        const char profileSetting[] = "WEBVIEW2_USER_DATA_FOLDER";
        const auto originalProfile = qgetenv(profileSetting);
        QVERIFY(qputenv(profileSetting, fixture.filePath("profile").toUtf8()));
        const auto restoreProfile = qScopeGuard([&] {
            if (originalProfile.isNull()) qunsetenv(profileSetting);
            else qputenv(profileSetting, originalProfile);
        });

        WebView2Host host;
        QSignalSpy failed(&host, &WebView2Host::failed);
        QSignalSpy messages(&host, &WebView2Host::messageReceived);
        QEventLoop loop;
        connect(&host, &WebView2Host::failed, &loop, &QEventLoop::quit);
        connect(&host, &WebView2Host::messageReceived, &loop, &QEventLoop::quit);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        host.navigate(QUrl::fromLocalFile(page.fileName()));
        host.initialize();
        if (messages.isEmpty() && failed.isEmpty()) loop.exec();
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QVERIFY(host.isReady());
        QVERIFY(!messages.isEmpty());
        QCOMPARE(messages.first().first().toJsonObject().value("kind").toString(), QStringLiteral("ready"));
    }

    void sdrUiSurvivesHdrWhiteLevels_data() {
        QTest::addColumn<float>("white");
        QTest::newRow("80-nits") << 1.0f;
        QTest::newRow("160-nits") << 2.0f;
        QTest::newRow("240-nits-reported-exposure") << 3.0f;
        QTest::newRow("400-nits") << 5.0f;
    }
    void sdrUiSurvivesHdrWhiteLevels() {
        QFETCH(float, white);
        const QColor colors[] = {QColor("#1f2328"), QColor("#f0f1f2"), QColor("#0969da"),
                                  QColor("#ffffff"), QColor("#000000"), QColor("#326ca8")};
        // Two rows and GPU-like row padding ensure no packed-buffer assumption.
        const qsizetype pitch = sizeof(colors) / sizeof(colors[0]) * 8 + 24;
        QByteArray bytes(pitch * 2, char(0x7f));
        for (int y = 0; y < 2; ++y) for (int x = 0; x < int(std::size(colors)); ++x) {
            const auto c = colors[x];
            const qsizetype offset = y * pitch + x * 8;
            setHalf(bytes, offset, linear(c.red()) * white);
            setHalf(bytes, offset + 2, linear(c.green()) * white);
            setHalf(bytes, offset + 4, linear(c.blue()) * white);
            setHalf(bytes, offset + 6, 0); // desktop output must be opaque
        }
        const QImage image = toSrgb(bytes.constData(), bytes.size(), {int(std::size(colors)), 2},
                                     pitch, PixelFormat::ScRgb16, white);
        QCOMPARE(image.size(), QSize(int(std::size(colors)), 2));
        QCOMPARE(image.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(image.devicePixelRatio(), 1.0);
        for (int y = 0; y < 2; ++y) for (int x = 0; x < int(std::size(colors)); ++x) {
            const QColor actual = image.pixelColor(x, y), expected = colors[x];
            QVERIFY(std::abs(actual.red() - expected.red()) <= 1);
            QVERIFY(std::abs(actual.green() - expected.green()) <= 1);
            QVERIFY(std::abs(actual.blue() - expected.blue()) <= 1);
            QCOMPARE(actual.alpha(), 255);
        }
        // Prove this fixture exposes the original washout instead of merely
        // exercising an identity conversion: 240-nit dark text becomes #393f47.
        if (white == 3.0f) {
            const QImage wrong = toSrgb(bytes.constData(), bytes.size(), image.size(), pitch,
                                         PixelFormat::ScRgb16, 1);
            QCOMPARE(wrong.pixelColor(0, 0), QColor(57, 63, 71));
            QCOMPARE(wrong.pixelColor(1, 0), QColor(Qt::white));
        }
    }
    void outOfRangeScRgbIsSafeAndOpaque() {
        QByteArray bytes(16, 0);
        setHalf(bytes, 0, -0.2f);
        setHalf(bytes, 2, std::numeric_limits<float>::quiet_NaN());
        setHalf(bytes, 4, std::numeric_limits<float>::infinity());
        setHalf(bytes, 8, 12.5f);
        setHalf(bytes, 10, 0.5f);
        setHalf(bytes, 12, 0.003f);
        const auto image = toSrgb(bytes.constData(), bytes.size(), {2, 1}, 16, PixelFormat::ScRgb16, 1);
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 255));
        QCOMPARE(image.pixelColor(1, 0), QColor(255, 188, 10));
    }
    void paddedBgraAndRotation_data() {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<QList<int>>("order");
        QTest::newRow("landscape") << 0 << QList<int>{1, 2, 3, 4, 5, 6};
        QTest::newRow("portrait") << 90 << QList<int>{4, 1, 5, 2, 6, 3};
        QTest::newRow("landscape-flipped") << 180 << QList<int>{6, 5, 4, 3, 2, 1};
        QTest::newRow("portrait-flipped") << 270 << QList<int>{3, 6, 2, 5, 1, 4};
    }
    void paddedBgraAndRotation() {
        QFETCH(int, rotation);
        QFETCH(QList<int>, order);
        QByteArray bytes(32, char(0xee));
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) {
            const int offset = y * 16 + x * 4;
            bytes[offset] = char(x + y * 3 + 1);
            bytes[offset + 1] = char(20);
            bytes[offset + 2] = char(40);
            bytes[offset + 3] = char(0);
        }
        const auto image = toSrgb(bytes.constData(), bytes.size(), {3, 2}, 16,
                                   PixelFormat::Bgra8, 3, rotation);
        QCOMPARE(image.size(), (rotation == 90 || rotation == 270 ? QSize(2, 3) : QSize(3, 2)));
        int index = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            QCOMPARE(image.pixelColor(x, y), QColor(40, 20, order[index++]));
    }
    void invalidBuffersFailBeforeReading() {
        const QByteArray bytes(32, 0);
        auto convert = [&](const void *data, qsizetype count, QSize size, qsizetype pitch,
                           float white = 1, int rotation = 0) {
            return toSrgb(data, count, size, pitch, PixelFormat::ScRgb16, white, rotation);
        };
        QVERIFY(convert(nullptr, 32, {2, 2}, 16).isNull());
        QVERIFY(convert(bytes.constData(), 31, {2, 2}, 16).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, 15).isNull());
        QVERIFY(convert(bytes.constData(), 32, {}, 16).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, 16, 0).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, 16, -1).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, 16, std::numeric_limits<float>::quiet_NaN()).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, 16, 1, 45).isNull());
        QVERIFY(convert(bytes.constData(), 32, {2, 2}, std::numeric_limits<qsizetype>::max()).isNull());
    }
    void blankDuplicatedFramesAreRecognized() {
        QVERIFY(isBlankFrame({}));
        QImage black(16, 9, QImage::Format_RGB32);
        black.fill(Qt::black);
        QVERIFY(isBlankFrame(black));
        // A frame that never received the desktop is exactly zero; a few levels
        // above that are still not a picture worth showing.
        QImage nearlyBlack(16, 9, QImage::Format_RGB32);
        nearlyBlack.fill(QColor(2, 2, 2));
        QVERIFY(isBlankFrame(nearlyBlack));
        // One real pixel is enough: dark UI over a black wallpaper is a genuine
        // desktop, not a blank duplication.
        QImage single(16, 9, QImage::Format_RGB32);
        single.fill(Qt::black);
        single.setPixel(15, 8, QColor(8, 8, 8).rgb());
        QVERIFY(!isBlankFrame(single));
        QImage darkUi(16, 9, QImage::Format_RGB32);
        darkUi.fill(QColor("#1f2328"));
        QVERIFY(!isBlankFrame(darkUi));
        // Channel order must be honoured, not just the low byte.
        QImage blueOnly(16, 9, QImage::Format_RGB32);
        blueOnly.fill(QColor(0, 0, 9));
        QVERIFY(!isBlankFrame(blueOnly));
    }
    void realGpuCapturePreservesUiColorsAndExcludesOverlay() {
        ColorBars window;
        window.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        const auto screen = QGuiApplication::primaryScreen();
        window.setGeometry(QRect(screen->availableGeometry().topLeft() + QPoint(100, 100), QSize(320, 160)));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        const auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        const auto color = displayColor(monitor);
        qInfo() << "Advanced color:" << color.advancedColor << "SDR white multiplier:" << color.sdrWhiteLevel;
        QString error;
        auto capture = DesktopCapture::open(monitor, &error);
        if (!capture && (error.endsWith("0x887a0004") || error.endsWith("0x80004001")))
            QSKIP(qPrintable("This graphics driver does not support desktop duplication: " + error));
        QVERIFY2(capture, qPrintable(error));
        if (!runningOnInputDesktop()) {
            const auto image = capture->read(color.sdrWhiteLevel, &error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QCOMPARE(image.size(), capture->geometry().size());
            capture.reset();
            capture = DesktopCapture::open(monitor, &error);
            QVERIFY2(capture, qPrintable(error));
            QSKIP("Test window is on a non-active desktop; verified real GPU open/read/reopen without window pixel match.");
        }
        RECT rect{};
        QVERIFY(GetWindowRect(hwnd, &rect));
        const QPoint origin(rect.left - capture->geometry().x(), rect.top - capture->geometry().y());
        const QPoint dark = origin + QPoint((rect.right - rect.left) / 4, (rect.bottom - rect.top) / 2);
        const QPoint light = origin + QPoint(3 * (rect.right - rect.left) / 4, (rect.bottom - rect.top) / 2);
        auto check = [&] {
            const auto image = capture->read(color.sdrWhiteLevel, &error);
            if (image.isNull()) return false;
            const QColor expectedDark(window.reversed ? "#f0f1f2" : "#1f2328");
            const QColor expectedLight(window.reversed ? "#1f2328" : "#f0f1f2");
            return image.pixelColor(dark) == expectedDark && image.pixelColor(light) == expectedLight;
        };
        QTRY_VERIFY_WITH_TIMEOUT(check(), 4000);
        QWidget overlay;
        overlay.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        overlay.setGeometry(window.geometry());
        overlay.setStyleSheet("background:#ff00ff");
        overlay.show();
        configureNativeWindow(&overlay, true);
        QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        QVERIFY(excludedFromCapture(&overlay));
        QTest::qWait(150);
        QTRY_VERIFY_WITH_TIMEOUT(check(), 4000);
        window.reversed = true;
        window.update();
        QTRY_VERIFY_WITH_TIMEOUT(check(), 4000);
        // Releasing and recreating a duplication must not hit the per-output
        // single-process limit or retain stale frame ownership.
        capture.reset();
        capture = DesktopCapture::open(monitor, &error);
        QVERIFY2(capture, qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(check(), 4000);
    }
    void hungDriverTimesOutAndTripsMonitorCircuitBreaker() {
        const auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        QVERIFY(monitor);
        QString error;
        std::unique_ptr<DesktopCapture> capture;
        struct AcquireGate {
            std::mutex mutex;
            std::condition_variable cv;
            bool entered = false;
            bool released = false;
        };
        const auto gate = std::make_shared<AcquireGate>();
        const auto unblockAndReset = [&] {
            {
                std::lock_guard lock(gate->mutex);
                gate->released = true;
            }
            gate->cv.notify_one();
            testing::setAcquireFrameHookForTesting({});
            testing::setWatchdogTimeoutForTesting(0);
            testing::resetBypassedMonitorsForTesting();
        };
        auto cleanup = qScopeGuard(unblockAndReset);
        capture = DesktopCapture::open(monitor, &error);
        if (!capture && (error.endsWith("0x887a0004") || error.endsWith("0x80004001")))
            QSKIP(qPrintable("This graphics driver does not support desktop duplication: " + error));
        QVERIFY2(capture, qPrintable(error));

        testing::setWatchdogTimeoutForTesting(100);
        testing::setAcquireFrameHookForTesting([gate] {
            std::unique_lock lock(gate->mutex);
            gate->entered = true;
            gate->cv.wait(lock, [&] { return gate->released; });
        });

        QElapsedTimer timer;
        timer.start();
        QVERIFY(capture->read(1, &error).isNull());
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY2(error.contains("timed out"), qPrintable(error));
        {
            std::lock_guard lock(gate->mutex);
            QVERIFY(gate->entered);
        }

        timer.restart();
        capture.reset();
        QVERIFY(timer.elapsed() < 1000);

        timer.restart();
        QVERIFY(!DesktopCapture::open(monitor, &error));
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY2(error.contains("bypassed"), qPrintable(error));

        unblockAndReset();
        // The detached worker may still be releasing the old duplication.
        const auto reopen = [&] {
            if (!capture) capture = DesktopCapture::open(monitor, &error);
            return bool(capture);
        };
        QTRY_VERIFY2_WITH_TIMEOUT(reopen(), qPrintable(error), 2000);
    }
};
QTEST_MAIN(WindowsCaptureTests)
#include "windows_capture_test.moc"
