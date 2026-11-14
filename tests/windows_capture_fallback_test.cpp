// Exercise the production Windows backend with a known duplicated frame and an
// unavailable GDI desktop. No screen pixels or driver-specific failure are needed.
#include "platform.h"
#include "windowscapture.h"
#include <QApplication>
#include <QScreen>
#include <QTest>

namespace captureFallbackTest {
QImage frame;
QRect geometry(0, 0, 32, 16);
int gdiReads = 0;
HDC unavailableDesktopDc(HWND) {
    ++gdiReads;
    return nullptr;
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
#include "../app/platform_win.cpp"
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
