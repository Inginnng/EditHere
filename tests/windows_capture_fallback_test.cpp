// Exercise the production Windows backend with a known duplicated frame and an
// unavailable GDI desktop. No screen pixels or driver-specific failure are needed.
#include "platform.h"
#include "windowscapture.h"
#include <QApplication>
#include <QColorSpace>
#include <QScreen>
#include <QTest>
#include <algorithm>

namespace captureFallbackTest {
QImage frame;
QRect geometry(0, 0, 32, 16);
int gdiReads = 0;
bool gdiAvailable = false;
QRect gdiRegion;
HDC unavailableDesktopDc(HWND) {
    ++gdiReads;
    return gdiAvailable ? ::GetDC(nullptr) : nullptr;
}
BOOL readTestDesktop(HDC target, int, int, int width, int height, HDC, int x, int y, DWORD) {
    gdiRegion = {x, y, width, height};
    DIBSECTION dib{};
    if (!GetObjectW(GetCurrentObject(target, OBJ_BITMAP), sizeof(dib), &dib) || !dib.dsBm.bmBits)
        return FALSE;
    auto *pixels = static_cast<QRgb *>(dib.dsBm.bmBits);
    std::fill_n(pixels, width * height, qRgb(31, 35, 40));
    return TRUE;
}
}

namespace h2d::windowscapture {
class FallbackDesktopCapture {
  public:
    static std::unique_ptr<FallbackDesktopCapture> open(HMONITOR, QString *error) {
        if (error) error->clear();
        return std::make_unique<FallbackDesktopCapture>();
    }
    QRect geometry() const { return captureFallbackTest::geometry; }
    QImage read(float, QString *error) {
        if (error) error->clear();
        return captureFallbackTest::frame;
    }
};
DisplayColor fallbackDisplayColor(HMONITOR) { return {true, 1}; }
}

// Only these native inputs are replaced. Frame validation, GDI allocation,
// session cleanup and both public capture callbacks are production code.
#define DesktopCapture FallbackDesktopCapture
#define displayColor fallbackDisplayColor
#define GetDC captureFallbackTest::unavailableDesktopDc
#define BitBlt captureFallbackTest::readTestDesktop
#include "../app/platform_win.cpp"
#undef BitBlt
#undef GetDC
#undef displayColor
#undef DesktopCapture

class WindowsCaptureFallbackTests final : public QObject {
    Q_OBJECT
  private:
    HMONITOR monitor_ = nullptr;
    QRect nativeGeometry_;

  private slots:
    void initTestCase() {
        monitor_ = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        QVERIFY(monitor_);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        QVERIFY(GetMonitorInfoW(monitor_, &info));
        nativeGeometry_ = {info.rcMonitor.left, info.rcMonitor.top,
                           info.rcMonitor.right - info.rcMonitor.left,
                           info.rcMonitor.bottom - info.rcMonitor.top};
    }
    void init() {
        captureFallbackTest::frame = QImage(captureFallbackTest::geometry.size(), QImage::Format_RGB32);
        captureFallbackTest::frame.fill(Qt::black);
        captureFallbackTest::gdiReads = 0;
        captureFallbackTest::gdiAvailable = false;
        captureFallbackTest::gdiRegion = {};
    }
    void cleanup() { h2d::endScrollingCapture(); }

    void validHdrFrameDoesNotNeedGdi() {
        captureFallbackTest::frame.fill(QColor("#1f2328"));
        QRect geometry;
        const auto image = h2d::readHdrScreen(monitor_, 1, &geometry);
        QCOMPARE(image, captureFallbackTest::frame);
        QCOMPARE(geometry, captureFallbackTest::geometry);
        QCOMPARE(captureFallbackTest::gdiReads, 0);
    }
    void blankHdrAndFailedGdiReturnNoImage() {
        const auto image = h2d::readHdrScreen(monitor_, 1);
        QVERIFY2(image.isNull(), "A failed fallback must not return the known blank HDR frame.");
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
    void nullHdrAlsoAttemptsGdiBeforeReportingFailure() {
        captureFallbackTest::frame = {};
        QVERIFY(h2d::readHdrScreen(monitor_, 1).isNull());
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
    void sdrNativePixelsRemainUnscaledAndOpaque() {
        QImage image(3840, 2160, QImage::Format_ARGB32);
        image.fill(QColor(31, 35, 40, 0));
        const QRect monitor(-3840, -200, 3840, 2160);
        const auto result = h2d::readSdrScreen(image, monitor);
        QCOMPARE(result.size(), monitor.size());
        QCOMPARE(result.pixelColor(100, 100), QColor(31, 35, 40));
        QCOMPARE(captureFallbackTest::gdiReads, 0);
    }
    void sdrFallbackReadsPhysicalMonitorGeometry_data() {
        QTest::addColumn<QRect>("region");
        QTest::newRow("4k-left-of-primary") << QRect(-3840, 0, 3840, 2160);
        QTest::newRow("4k-above-primary") << QRect(0, -2160, 3840, 2160);
        QTest::newRow("portrait-right-of-primary") << QRect(2560, -200, 1440, 2560);
    }
    void sdrFallbackReadsPhysicalMonitorGeometry() {
        QFETCH(QRect, region);
        captureFallbackTest::gdiAvailable = true;
        const auto result = h2d::readSdrScreen({}, region);
        QCOMPARE(result.size(), region.size());
        QCOMPARE(captureFallbackTest::gdiRegion, region);
        QCOMPARE(result.pixelColor(result.width() - 1, result.height() - 1), QColor(31, 35, 40));
        QCOMPARE(result.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
    void unavailableSdrFramesNeverBecomeNativeScreens_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("null") << 0;
        QTest::newRow("black") << 1;
        QTest::newRow("logical-sized-after-dpi-change") << 2;
    }
    void unavailableSdrFramesNeverBecomeNativeScreens() {
        QFETCH(int, kind);
        QImage image;
        if (kind) {
            image = QImage(kind == 1 ? QSize(3840, 2160) : QSize(1920, 1080), QImage::Format_RGB32);
            image.fill(kind == 1 ? Qt::black : Qt::white);
        }
        QVERIFY(h2d::readSdrScreen(image, {-3840, 0, 3840, 2160}).isNull());
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
    void ordinaryCaptureReportsFailedFallback() {
        int replies = 0;
        QVector<h2d::ScreenFrame> received;
        QString error;
        h2d::captureScreens([&](QVector<h2d::ScreenFrame> frames, QString problem) {
            ++replies;
            received = std::move(frames);
            error = std::move(problem);
        });
        QCOMPARE(replies, 1);
        QVERIFY(received.isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
    void scrollingCaptureReportsFailedFallback() {
        const QRect region(nativeGeometry_.topLeft(), QSize(32, 16));
        QVERIFY(h2d::beginScrollingCapture({}, region));
        int replies = 0;
        QImage received;
        QString error;
        h2d::captureScrollRegion(region, [&](QImage image, QString problem) {
            ++replies;
            received = std::move(image);
            error = std::move(problem);
        });
        QCOMPARE(replies, 1);
        QVERIFY(received.isNull());
        QVERIFY(!error.isEmpty());
        QCOMPARE(captureFallbackTest::gdiReads, 1);
    }
};

QTEST_MAIN(WindowsCaptureFallbackTests)
#include "windows_capture_fallback_test.moc"
