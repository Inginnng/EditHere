#pragma once
#include "scrollstitch.h"
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

namespace h2d {
struct ScrollTarget {
    QString screenName;
    QRect pixels;
    QPoint nativePoint;
};
int scrollFrameLimit();

class ScrollCapture final : public QObject {
    Q_OBJECT
  public:
    enum class Outcome { Added, Repeat, Failed };
    explicit ScrollCapture(QObject *parent = nullptr);
    bool begin(QImage first, int step = 1, Qt::Orientation axis = Qt::Vertical);
    void resume() { running_ = !stitcher_.lastFrame().isNull(); }
    void pause() { running_ = false; }
    Outcome take(const QImage &frame);
    // Appends a sample taken while the page is still moving, but only when it
    // continues the stitch almost exactly. Anything less waits for a still frame.
    bool takeMoving(const QImage &frame);
    QImage picture() const;
    QImage preview(int width) const;
    QImage previewRegion(const QRect &region, int width) const;
    void setUltraLong(bool enabled) { stitcher_.setUltraLong(enabled); }
    bool ultraLong() const { return stitcher_.ultraLong(); }
    bool fitsImageLimits() const;
    bool savePng(const QString &path, QString *error = nullptr) const {
        return stitcher_.savePng(path, axis_ == Qt::Horizontal, error);
    }
    bool cropBeforeViewport();
    bool cropAfterViewport();
    void setAutoCrop(bool enabled);
    bool autoCrop() const { return autoCrop_; }
    int stitchDirection() const { return growthDirection_; }
    QSize size() const;
    QRect viewportRect() const;
    int originOffset() const { return stitcher_.originOffset(); }
    bool matched() const { return stitcher_.matched(); }
    Qt::Orientation axis() const { return axis_; }
    int height() const { return size().height(); }
    int frames() const { return frames_; }
    int step() const { return step_; }
    bool hasProgress() const { return frames_ > 0; }
    bool atLimit() const;
    bool partial() const { return stitcher_.partial(); }
    bool running() const { return running_; }
    QString notice() const { return notice_; }
    void stop() { running_ = false; }

  private:
    void updateAutoCrop(int displacement, int added);
    ScrollStitcher stitcher_;
    QString notice_;
    int step_ = 1;
    int frames_ = 0;
    Qt::Orientation axis_ = Qt::Vertical;
    bool running_ = false;
    bool autoCrop_ = false;
    int growthDirection_ = 0;
    int lastGrowthDirection_ = 0;
};
} // namespace h2d
