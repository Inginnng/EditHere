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
// belongs to; every frame is re-read from that region while the page scrolls.
struct ScrollTarget {
    QString screenName;
    QRect pixels;
    QPoint nativePoint; // Where the wheel is sent, in native screen coordinates.
};

// The most frame pairs a long capture takes before it gives up. A page that never
// stops moving would otherwise keep growing for as long as the target answers.
int scrollFrameLimit();

// A long capture that the user drives. It owns the growing picture and the arithmetic
// that decides where each frame belongs, and nothing else: frames are handed to it by
// whoever manages to read the screen, and the wheel is sent by whoever owns the window
// under the pointer. That split is what lets the capture window stay on screen, stay
// draggable, and stop the moment the user says so — the timer-driven version had to
// hide the window and could not be interrupted.
//
// One frame is added at a time, and the answer says whether anything new was found.
// Nothing here talks to a screen, a timer or a wheel, so a test can drive a whole
// capture with pictures it made up.
class ScrollCapture final : public QObject {
    Q_OBJECT
  public:
    enum class Outcome {
        Added,  // The frame carried rows that were not in the picture yet.
        Repeat, // The frame was the same page again: the content has run out.
        Failed, // The frame could not be placed at all, even loosely.
    };
    explicit ScrollCapture(QObject *parent = nullptr);
    // Starts a run from the frame already on screen. `step` is how many wheel lines
    // are sent between frames, which is only needed by the caller doing the scrolling.
    void begin(QImage first, int step = 1);
    // Places one more look at the region. Anything the frame does not have in common
    // with the picture is added; the outcome says which of the three cases it was.
    Outcome take(const QImage &frame);
    // The picture as it stands, which is what the preview panel shows while the run is
    // going and what is handed over when it ends.
    QImage picture() const {
        return stitcher_.picture();
    }
    int height() const {
        return stitcher_.height();
    }
    int frames() const {
        return frames_;
    }
    int step() const {
        return step_;
    }
    // True once the picture has grown as far as a run is allowed to. The caller is
    // expected to stop offering frames at that point and say so.
    bool atLimit() const;
    // True when at least one frame had to be placed on a rough match. The picture is
    // still usable, only not pixel perfect.
    bool partial() const {
        return stitcher_.partial();
    }
    bool running() const {
        return running_;
    }
    // The last message worth showing the user, or an empty string.
    QString notice() const {
        return notice_;
    }
    void stop();

  private:
    ScrollStitcher stitcher_;
    QString notice_;
    int step_ = 1;
    int frames_ = 0;
    bool running_ = false;
};
} // namespace h2d
