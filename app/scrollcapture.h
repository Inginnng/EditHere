#pragma once
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <functional>

#include "scrollstitch.h"

namespace h2d {
// Where a long picture is taken from. The region is in the pixels of the screen it
// belongs to; every frame is re-read from that same region while the page scrolls.
struct ScrollTarget {
    QString screenName;
    QRect pixels;
    QPoint nativePoint; // Where the wheel is sent, in native screen coordinates.
};

// The most frame pairs a long capture takes before it gives up. A page that never
// stops moving would otherwise keep growing for as long as the target answers.
int scrollFrameLimit();

// Drives a page while a long picture is taken: scroll, grab, stitch, and stop once
// the page stops moving. The frames come from the same capture path the rest of the
// application uses, so the overlay has to be hidden before a run starts.
class ScrollCapture final : public QObject {
    Q_OBJECT
  public:
    using Progress = std::function<void(int frames, int height)>;
    using Done = std::function<void(QImage picture, QString message, bool ok)>;
    explicit ScrollCapture(QObject *parent = nullptr);
    // The first frame is the one already on screen, so the first grab is not wasted.
    // "step" is how many wheel lines to send between frames.
    void start(QImage first, ScrollTarget target, int step, Done done);
    void setProgress(Progress progress) {
        progress_ = std::move(progress);
    }
    bool running() const {
        return running_;
    }
    // True when at least one frame had to be placed on a rough match. The picture is
    // still usable, only not pixel perfect.
    bool partial() const {
        return stitcher_.partial();
    }
    void stop();

  private:
    void grab();
    void stitch(const QImage &frame);
    void advance();
    void retry();
    void succeed();
    void fail(const QString &message);
    ScrollTarget target_;
    ScrollStitcher stitcher_;
    QImage held_; // The last look at the frame being waited on.
    Progress progress_;
    Done done_;
    int step_ = 1;
    int frames_ = 0;
    int stableTries_ = 0;
    int idle_ = 0;
    bool running_ = false;
};
} // namespace h2d
