#include "scrollcapture.h"
#include "platform.h"
#include "scrollstitch.h"
#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace h2d {
namespace {
// A window that animates its scrolling needs a moment to settle before the next
// grab, or the picture shows it half way between two positions. Three hundred
// milliseconds is what a smooth-scrolling page needs to come to rest.
constexpr int kSettleMs = 300;
// How long to wait before looking at the same position again to see whether the
// page is still moving.
constexpr int kStableMs = 90;
// How many extra looks a frame gets before it is taken as it is. A page with video
// in it never goes still, and waiting for it forever would capture nothing.
constexpr int kStableTries = 3;
// Two looks at the same position count as the same frame below this much average
// difference per channel.
constexpr double kStableDifference = 1.0;
// A page that shows nothing new once may be loading an image and may just have
// reached the end, so the wheel is tried again before the run is called finished.
constexpr int kIdleTries = 2;
// Grabbing a screen is not instant, and a page that never stops moving would
// otherwise fill memory.
constexpr int kFrameLimit = 60;
} // namespace

int scrollFrameLimit() {
    return kFrameLimit;
}

ScrollCapture::ScrollCapture(QObject *parent) : QObject(parent) {}

void ScrollCapture::start(QImage first, ScrollTarget target, int step, Done done) {
    stop();
    if (first.isNull() || target.pixels.isEmpty()) {
        if (done)
            done({}, QCoreApplication::translate("h2d::ScrollCapture", "没有可以继续的长截图内容。"), false);
        return;
    }
    target_ = std::move(target);
    step_ = std::max(1, step);
    stitcher_.reset(first);
    held_ = {};
    stableTries_ = 0;
    idle_ = 0;
    done_ = std::move(done);
    frames_ = 0;
    running_ = true;
    advance();
}

void ScrollCapture::stop() {
    running_ = false;
    done_ = nullptr;
    stitcher_.reset({});
    held_ = {};
}

void ScrollCapture::advance() {
    if (!running_)
        return;
    held_ = {};
    stableTries_ = 0;
    // The wheel is sent to the page and the next grab waits for it to settle.
    if (!scrollAt(target_.nativePoint, step_)) {
        // Nothing under the point can be scrolled. That is not always the end: the
        // window may not have been ready for the wheel yet.
        retry();
        return;
    }
    QTimer::singleShot(kSettleMs, this, [this] { grab(); });
}

// One more turn of the wheel after a frame that showed nothing new. A page that is
// still loading, or a window that ignored the first wheel, gets another chance
// before the run is called finished.
void ScrollCapture::retry() {
    if (!running_)
        return;
    if (++idle_ >= kIdleTries) {
        succeed();
        return;
    }
    QTimer::singleShot(kSettleMs, this, [this] { advance(); });
}

void ScrollCapture::grab() {
    if (!running_)
        return;
    // The frame is re-read through the same path the first screenshot came from, so
    // scaling and multi-monitor handling stay in one place.
    captureScreens([this](QVector<ScreenFrame> frames, QString error) {
        if (!running_)
            return;
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        const ScreenFrame *frame = nullptr;
        for (const auto &candidate : frames) {
            if (candidate.name == target_.screenName) {
                frame = &candidate;
                break;
            }
        }
        if (frame == nullptr || frame->image.isNull()) {
            fail(tr("找不到要截取的屏幕。"));
            return;
        }
        const QRect area = target_.pixels.intersected(QRect(QPoint(0, 0), frame->image.size()));
        if (area.isEmpty()) {
            fail(tr("选区已不在屏幕范围内。"));
            return;
        }
        const QImage grabbed = frame->image.copy(area);
        // A page takes a moment to stop moving, and a frame taken while it is still
        // sliding would be stitched at a position it never rested at. Two looks at
        // the same position that agree are what makes a frame trustworthy.
        if (frameDifference(held_, grabbed) <= kStableDifference) {
            stitch(grabbed);
            return;
        }
        held_ = grabbed;
        if (++stableTries_ > kStableTries) {
            stitch(grabbed);
            return;
        }
        QTimer::singleShot(kStableMs, this, [this] { grab(); });
    });
}

void ScrollCapture::stitch(const QImage &frame) {
    if (!running_)
        return;
    // A frame with nothing new in it means the page reached its end, which is how
    // a long capture finishes normally.
    const int added = stitcher_.add(frame);
    if (added <= 0) {
        retry();
        return;
    }
    idle_ = 0;
    ++frames_;
    if (progress_)
        progress_(frames_, stitcher_.height());
    if (frames_ >= kFrameLimit || stitcher_.height() >= ScrollStitcher::maxHeight()) {
        succeed();
        return;
    }
    advance();
}

void ScrollCapture::succeed() {
    if (!running_)
        return;
    running_ = false;
    Done done = done_;
    done_ = nullptr;
    if (done)
        done(stitcher_.picture(), {}, true);
}

void ScrollCapture::fail(const QString &message) {
    if (!running_)
        return;
    running_ = false;
    Done done = done_;
    done_ = nullptr;
    if (done)
        done({}, message, false);
}
} // namespace h2d
