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
    void resume() { running_ = !stitcher_.lastFrame().isNull() && !atLimit(); }
    void pause() { running_ = false; }
    Outcome take(const QImage &frame);
    QImage picture() const { return stitcher_.picture(); }
    int height() const { return stitcher_.height(); }
    int frames() const { return frames_; }
    int step() const { return step_; }
    bool hasProgress() const { return frames_ > 0; }
    bool atLimit() const;
    bool partial() const { return stitcher_.partial(); }
    bool running() const { return running_; }
    QString notice() const { return notice_; }
    void stop() { running_ = false; }

  private:
    ScrollStitcher stitcher_;
    QString notice_;
    int step_ = 1;
    int frames_ = 0;
    bool running_ = false;
};
} // namespace h2d
