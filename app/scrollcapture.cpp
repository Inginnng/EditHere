#include "scrollcapture.h"
#include <QCoreApplication>
#include <algorithm>

namespace h2d {
int scrollFrameLimit() { return 160; }
ScrollCapture::ScrollCapture(QObject *parent) : QObject(parent) {}

bool ScrollCapture::begin(QImage first, int step, Qt::Orientation axis) {
    notice_.clear();
    step_ = std::clamp(step, 1, 10);
    frames_ = 0;
    stitcher_.reset(axis == Qt::Vertical ? first : QImage());
    running_ = !stitcher_.lastFrame().isNull();
    if (!running_)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "无法开始纵向长截图，请重新选择捕捉区域。");
    return running_;
}

ScrollCapture::Outcome ScrollCapture::take(const QImage &frame) {
    if (!running_)
        return Outcome::Failed;
    if (atLimit()) {
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "已达长截图尺寸上限。");
        return Outcome::Failed;
    }
    const int added = stitcher_.add(frame);
    if (added > 0) {
        ++frames_;
        notice_.clear();
        return Outcome::Added;
    }
    if (added == 0)
        return Outcome::Repeat;
    if (stitcher_.status() == ScrollStitcher::Status::Limit)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "已达长截图尺寸上限。");
    else if (stitcher_.status() == ScrollStitcher::Status::GeometryChanged)
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "捕捉区域尺寸发生变化，请重新选择。");
    else
        notice_ = QCoreApplication::translate("h2d::ScrollCapture", "无法可靠拼接当前画面，请缩小到滚动内容区域后重试。");
    return Outcome::Failed;
}

bool ScrollCapture::atLimit() const {
    return frames_ >= scrollFrameLimit() || stitcher_.atLimit();
}
} // namespace h2d
