#include "scrollcapture.h"
#include <QCoreApplication>
#include <QTransform>
#include <algorithm>

namespace h2d {
namespace {
QImage transposed(const QImage &image) {
    return image.transformed(QTransform(0, 1, 1, 0, 0, 0));
}
}
int scrollFrameLimit() { return 600; }
ScrollCapture::ScrollCapture(QObject *parent) : QObject(parent) {}

bool ScrollCapture::begin(QImage first, int step, Qt::Orientation axis) {
    notice_.clear();
    growthDirection_ = lastGrowthDirection_ = 0;
    step_ = std::clamp(step, 1, 10);
    frames_ = 0;
    axis_ = axis;
    stitcher_.reset(axis_ == Qt::Vertical ? first : transposed(first));
    running_ = !stitcher_.lastFrame().isNull();
    if (!running_)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "无法开始长截图，请重新选择捕捉区域。");
    return running_;
}

ScrollCapture::Outcome ScrollCapture::take(const QImage &frame) {
    if (!running_)
        return Outcome::Failed;
    if (!ultraLong() && frames_ >= scrollFrameLimit()) {
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "已达长截图尺寸上限。");
        return Outcome::Failed;
    }
    const auto step = stitcher_.plan(axis_ == Qt::Vertical ? frame : transposed(frame));
    const int added = stitcher_.commit(step);
    if (added > 0) {
        ++frames_;
        updateAutoCrop(step.displacement, added);
        notice_.clear();
        return Outcome::Added;
    }
    if (added == 0) {
        updateAutoCrop(step.displacement, added);
        notice_.clear();
        return Outcome::Repeat;
    }
    if (stitcher_.status() == ScrollStitcher::Status::Limit)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "已达长截图尺寸上限。");
    else if (stitcher_.status() == ScrollStitcher::Status::GeometryChanged)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "捕捉区域尺寸发生变化，请重新选择。");
    else
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "无法可靠拼接当前画面，请缩小到滚动内容区域后重试。");
    return Outcome::Failed;
}

bool ScrollCapture::takeMoving(const QImage &frame) {
    if (!running_ || (!ultraLong() && frames_ >= scrollFrameLimit()))
        return false;
    // An exact overlap is the same evidence a still frame gives. Approximate
    // matches may be motion blur or a half-painted page, and wait for stillness.
    const auto step = stitcher_.plan(axis_ == Qt::Vertical ? frame : transposed(frame));
    if (step.added < 0 || !step.displacement || step.score > 1.0 || stitcher_.commit(step) < 0)
        return false;
    if (step.added > 0)
        ++frames_;
    updateAutoCrop(step.displacement, step.added);
    notice_.clear();
    return true;
}

bool ScrollCapture::cropBeforeViewport() { return stitcher_.trimBeforeViewport(); }
bool ScrollCapture::cropAfterViewport() { return stitcher_.trimAfterViewport(); }

void ScrollCapture::setAutoCrop(bool enabled) {
    autoCrop_ = enabled;
    if (enabled) growthDirection_ = lastGrowthDirection_;
}

void ScrollCapture::updateAutoCrop(int displacement, int added) {
    if (!displacement) return;
    const int direction = displacement > 0 ? 1 : -1;
    if (!growthDirection_ && added > 0) growthDirection_ = direction;
    if (added > 0) lastGrowthDirection_ = direction;
    if (!autoCrop_ || !growthDirection_ || direction == growthDirection_) return;
    if (growthDirection_ > 0) stitcher_.trimAfterViewport();
    else stitcher_.trimBeforeViewport();
    if (stitcher_.height() <= stitcher_.lastFrame().height())
        growthDirection_ = lastGrowthDirection_ = 0;
}

QImage ScrollCapture::picture() const {
    const QImage image = stitcher_.picture();
    return axis_ == Qt::Vertical ? image : transposed(image);
}

QSize ScrollCapture::size() const {
    const QSize normalized(stitcher_.lastFrame().width(), stitcher_.height());
    return axis_ == Qt::Vertical ? normalized : QSize(normalized.height(), normalized.width());
}

QRect ScrollCapture::viewportRect() const {
    const QRect normalized = stitcher_.viewportRect();
    return axis_ == Qt::Vertical ? normalized : QRect(normalized.y(), normalized.x(),
                                                      normalized.height(), normalized.width());
}

QImage ScrollCapture::preview(int width) const {
    if (axis_ == Qt::Vertical)
        return stitcher_.preview(width);
    if (width <= 0 || stitcher_.height() <= 0)
        return {};
    if (ultraLong())
        return transposed(stitcher_.preview(720, width));
    // The normalized stitch grows vertically; its height becomes the original
    // image width. Scale by that dimension before transposing for the panel.
    const int narrowWidth = std::max(1, qRound(double(width) * stitcher_.lastFrame().width() / stitcher_.height()));
    QImage result = transposed(stitcher_.preview(narrowWidth));
    if (result.width() > width)
        result = result.scaledToWidth(width, Qt::SmoothTransformation);
    return result;
}

QImage ScrollCapture::previewRegion(const QRect &region, int width) const {
    if (axis_ == Qt::Vertical) return stitcher_.previewRegion(region, width);
    const QRect normalized(region.y(), region.x(), region.height(), region.width());
    const int crossWidth = region.width() > 0 ? std::max(1, qRound(
        double(width) * region.height() / region.width())) : 0;
    return transposed(stitcher_.previewRegion(normalized, crossWidth));
}

bool ScrollCapture::atLimit() const {
    return (!ultraLong() && frames_ >= scrollFrameLimit()) || stitcher_.atLimit();
}

bool ScrollCapture::fitsImageLimits() const {
    const QSize dimensions = size();
    return std::max(dimensions.width(), dimensions.height()) <= ScrollStitcher::maxHeight() &&
           qint64(dimensions.width()) * dimensions.height() <= ScrollStitcher::maxPixels();
}
} // namespace h2d
