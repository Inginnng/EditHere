#pragma once

#include <QImage>
#include <QString>
#include <functional>
#include <memory>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace h2d::windowscapture {
struct DisplayColor {
    bool advancedColor = false;
    float sdrWhiteLevel = 1.0f; // linear scRGB: 1.0 = 80 nits
};
DisplayColor displayColor(HMONITOR monitor);

// A duplicated desktop can arrive as an all-black frame that no window ever
// painted: some drivers hand back an empty first frame for an advanced-color
// output, and a duplicated output that is attached but not composited (a
// virtual display, a mirror source) never paints at all. Such a frame is not
// the desktop and must not be shown to the user.
bool isBlankFrame(const QImage &image);

enum class PixelFormat { Bgra8, ScRgb16 };
// The surface is unrotated GPU storage, with padding after each scanline.
// Rotation is the clockwise DXGI output rotation, independent of Qt's DPR.
QImage toSrgb(const void *data, qsizetype byteCount, QSize size, qsizetype rowPitch,
              PixelFormat format, float sdrWhiteLevel, int rotation = 0);

class DesktopCapture {
  public:
    static std::unique_ptr<DesktopCapture> open(HMONITOR monitor, QString *error);
    ~DesktopCapture();
    QRect geometry() const;
    // A static desktop may produce no new frame. Only a previously acquired
    // frame from this same duplication session may be reused on timeout.
    QImage read(float sdrWhiteLevel, QString *error);

  private:
    struct State;
    struct Worker;
    explicit DesktopCapture(HMONITOR monitor);
    DesktopCapture(const DesktopCapture &) = delete;
    DesktopCapture &operator=(const DesktopCapture &) = delete;
    QImage readFrame(float sdrWhiteLevel, int timeoutMs, QString *error);
    std::shared_ptr<Worker> worker_;
    // Remember blank outputs so scrolling does not pay for a retry every frame.
    bool blank_ = false;
};

namespace testing {
void setWatchdogTimeoutForTesting(int timeoutMs);
void setAcquireFrameHookForTesting(std::function<void()> hook);
void resetBypassedMonitorsForTesting();
}
} // namespace h2d::windowscapture
