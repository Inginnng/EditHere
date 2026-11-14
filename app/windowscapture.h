#pragma once

#include <QImage>
#include <QString>
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
    explicit DesktopCapture(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
} // namespace h2d::windowscapture
