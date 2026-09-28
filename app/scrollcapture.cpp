#include "scrollcapture.h"
#include "scrollstitch.h"
#include <QCoreApplication>
#include <algorithm>
#include <utility>

namespace h2d {
namespace {
// A frame that repeats the whole picture has nothing new in it, which is how the end
// of a page is recognised. The stitcher answers -1 for that and for a frame it could
// not place at all, and the two are told apart here by asking the same question the
// stitcher did: an exact comparison of the two frames.
constexpr double kSameFrame = 1.0;
} // namespace

int scrollFrameLimit() {
    return 60;
}

ScrollCapture::ScrollCapture(QObject *parent) : QObject(parent) {}

void ScrollCapture::begin(QImage first, int step) {
    notice_.clear();
    step_ = std::max(1, step);
    frames_ = 0;
    if (first.isNull()) {
        running_ = false;
        stitcher_.reset({});
        return;
    }
    stitcher_.reset(first);
    running_ = true;
}

ScrollCapture::Outcome ScrollCapture::take(const QImage &frame) {
    if (!running_ || frame.isNull())
        return Outcome::Failed;
    if (atLimit()) {
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "已达长截图上限。");
        return Outcome::Repeat;
    }
    // Whether the frame is the same page again has to be asked before it is placed: a
    // frame that repeats what is already there is refused by the stitcher with the same
    // answer as a frame that has nothing in common with it, and only one of those two
    // means the content has run out.
    const QImage previous = stitcher_.lastFrame();
    const int added = stitcher_.add(frame);
    if (added > 0) {
        ++frames_;
        return Outcome::Added;
    }
    if (!previous.isNull() && frameDifference(previous, frame) <= kSameFrame) {
        return Outcome::Repeat;
    }
    return Outcome::Failed;
}

bool ScrollCapture::atLimit() const {
    return frames_ >= scrollFrameLimit() || stitcher_.height() >= ScrollStitcher::maxHeight();
}

void ScrollCapture::stop() {
    running_ = false;
    frames_ = 0;
}
} // namespace h2d
