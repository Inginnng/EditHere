#include "videoplayback.h"
#include "videoproject.h"
#include "ui.h"
#include "fonts.h"
#include <QAudioOutput>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <functional>
namespace h2d {
namespace {
constexpr int TimelineSteps = 1000000;
QImage frameImage(const QVideoFrame &frame) {
    QImage image = frame.toImage();
    if (image.isNull()) return image;
    const int rotation = int(frame.rotation());
    if (rotation) image = image.transformed(QTransform().rotate(rotation));
    if (frame.mirrored()) image = image.mirrored(true, false);
    return image;
}
class VideoTimeline final : public QSlider {
  public:
    explicit VideoTimeline(QWidget *parent) : QSlider(Qt::Horizontal, parent) {
        setRange(0, TimelineSteps);
        setMinimumWidth(140);
        // Keep room above the groove for a hovered tag without moving its tip.
        setMinimumHeight(42);
        setMouseTracking(true);
    }
    struct Marker { int value; qint64 timestampUs; QString label; };
    QVector<Marker> markers;
    std::function<void(qint64)> markerClicked;
    void setMarkers(QVector<Marker> next) {
        markers = std::move(next);
        hovered_ = -1;
        unsetCursor();
        setToolTip({});
        update();
    }
  protected:
    void paintEvent(QPaintEvent *event) override {
        QSlider::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto drawMarker = [&](int index) {
            const bool hovered = index == hovered_;
            painter.setPen(QPen(QColor(hovered ? "#188c97" : "#249ba5"), 0.8));
            painter.setBrush(QColor(hovered ? "#40cbd2" : "#2bbac3"));
            painter.drawPath(markerPath(markers[index].value, hovered));
        };
        for (int index = 0; index < markers.size(); ++index)
            if (index != hovered_) drawMarker(index);
        if (hovered_ >= 0 && hovered_ < markers.size()) drawMarker(hovered_);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) { QSlider::mousePressEvent(event); return; }
        // An annotated frame opens its saved screenshot; elsewhere the click seeks.
        if (const int marker = markerAt(event->position()); marker >= 0) {
            if (markerClicked) markerClicked(markers[marker].timestampUs);
            event->accept();
            return;
        }
        setSliderDown(true);
        setValue(at(event));
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (isSliderDown()) { setValue(at(event)); event->accept(); return; }
        const int marker = markerAt(event->position());
        if (marker != hovered_) {
            hovered_ = marker;
            setCursor(marker >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            setToolTip(marker >= 0 ? markers[marker].label : QString());
            update();
        }
        QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            setValue(at(event)); setSliderDown(false); event->accept();
        } else QSlider::mouseReleaseEvent(event);
    }
    void leaveEvent(QEvent *event) override {
        if (hovered_ >= 0) { hovered_ = -1; unsetCursor(); setToolTip({}); update(); }
        QSlider::leaveEvent(event);
    }
  private:
    double markerX(int value) const {
        QStyleOptionSlider option;
        initStyleOption(&option);
        option.sliderPosition = option.sliderValue = std::clamp(value, minimum(), maximum());
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        return handle.left() + handle.width() / 2.0;
    }
    double markerTipY() const {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        return groove.center().y() - 2.0;
    }
    QPainterPath markerPath(int value, bool hovered) const {
        const double x = markerX(value), tip = markerTipY();
        const double halfWidth = hovered ? 6.0 : 4.5;
        const double height = hovered ? 17.0 : 13.0;
        const double shoulder = tip - (hovered ? 5.0 : 4.0);
        const double top = tip - height, radius = 1.4;
        QPainterPath path;
        path.moveTo(x - halfWidth + radius, top);
        path.lineTo(x + halfWidth - radius, top);
        path.quadTo(x + halfWidth, top, x + halfWidth, top + radius);
        path.lineTo(x + halfWidth, shoulder);
        path.lineTo(x, tip);
        path.lineTo(x - halfWidth, shoulder);
        path.lineTo(x - halfWidth, top + radius);
        path.quadTo(x - halfWidth, top, x - halfWidth + radius, top);
        path.closeSubpath();
        return path;
    }
    int markerAt(QPointF position) const {
        int nearest = -1;
        double distance = 8.0;
        for (int index = 0; index < markers.size(); ++index) {
            QRectF hit = markerPath(markers[index].value, index == hovered_).boundingRect().adjusted(-2, -2, 2, 1);
            // A click on the groove still seeks, even directly below a tag.
            if (!hit.contains(position)) continue;
            const double offset = std::abs(position.x() - markerX(markers[index].value));
            if (offset <= distance) { distance = offset; nearest = index; }
        }
        return nearest;
    }
    int at(QMouseEvent *event) const {
        const double first = markerX(minimum()), last = markerX(maximum());
        const double fraction = std::clamp((event->position().x() - first) / std::max(1.0, last - first), 0.0, 1.0);
        return qRound(fraction * TimelineSteps);
    }
    int hovered_ = -1;
};
}
class VideoSurface final : public QWidget {
  public:
    explicit VideoSurface(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        connect(&watcher_, &QFutureWatcher<QImage>::finished, this, [this] {
            image_ = watcher_.result();
            update();
            if (pending_.isValid()) convert();
        });
    }
    void setFrame(const QVideoFrame &frame) {
        pending_ = frame;
        if (!frame.isValid()) image_ = {};
        if (frame.isValid() && isVisible() && !watcher_.isRunning()) convert();
        else if (!frame.isValid()) update();
    }
    void clear() { setFrame({}); }
  protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        if (pending_.isValid() && !watcher_.isRunning()) convert();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        if (image_.isNull()) { painter.fillRect(rect(), Qt::black); return; }
        painter.drawImage(rect(), image_);
    }
  private:
    // Colour conversion and scaling run off the UI thread, one frame at a time.
    // Frames arriving meanwhile replace each other, so the newest one is shown
    // next and playback never queues work on the window that takes annotations.
    void convert() {
        const QVideoFrame frame = pending_;
        pending_ = {};
        const QSize target = (QSizeF(size()) * devicePixelRatioF()).toSize();
        watcher_.setFuture(QtConcurrent::run([frame, target] {
            QImage image = frameImage(frame);
            if (!image.isNull() && target.isValid() && target.width() < image.width())
                image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            return image;
        }));
    }
    QVideoFrame pending_;
    QImage image_;
    QFutureWatcher<QImage> watcher_;
};
VideoPlayback::VideoPlayback(QWidget *parent) : QWidget(parent) {
    setObjectName("videoControls");
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    audio_->setVolume(0.6);
    audio_->setMuted(true);
    player_->setAudioOutput(audio_);
    sink_ = new QVideoSink(this);
    view_ = new VideoSurface;
    view_->setObjectName("videoView");
    view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    view_->setFocusPolicy(Qt::NoFocus);
    view_->hide();
    player_->setVideoSink(sink_);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 8, 14, 6);
    layout->setSpacing(5);
    auto row = new QHBoxLayout;
    play_ = textButton({}, true, this); play_->setObjectName("videoPlayPause");
    back_ = textButton("−1s", false, this); back_->setObjectName("videoSeekBack");
    forward_ = textButton("+1s", false, this); forward_->setObjectName("videoSeekForward");
    timeline_ = new VideoTimeline(this); timeline_->setObjectName("videoTimeline");
    time_ = new QLabel(this); time_->setObjectName("videoTime");
    time_->setFont(QFont(monoFontFamily()));
    row->addWidget(play_); row->addWidget(back_); row->addWidget(timeline_, 1);
    row->addWidget(forward_); row->addWidget(time_);
    layout->addLayout(row);
    auto frameRow = new QHBoxLayout;
    status_ = mutedLabel({}, this); status_->setObjectName("videoStatus");
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    frames_ = new QComboBox(this); frames_->setObjectName("videoAnnotatedFrames"); frames_->setFixedWidth(220);
    frames_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    relocate_ = textButton({}, false, this); relocate_->setObjectName("videoRelocate");
    frameRow->addWidget(status_, 1); frameRow->addWidget(frames_); frameRow->addWidget(relocate_);
    layout->addLayout(frameRow);
    connect(play_, &QPushButton::clicked, this, &VideoPlayback::toggle);
    connect(back_, &QPushButton::clicked, this, [this] { seek(positionMs() - 1000); });
    connect(forward_, &QPushButton::clicked, this, [this] { seek(positionMs() + 1000); });
    connect(relocate_, &QPushButton::clicked, this, &VideoPlayback::sourceRelocationRequested);
    connect(frames_, &QComboBox::activated, this, [this](int index) {
        if (index > 0) emit reviewRequested(frames_->itemData(index).toLongLong());
    });
    static_cast<VideoTimeline *>(timeline_)->markerClicked = [this](qint64 timestampUs) {
        if (const int index = frames_->findData(timestampUs); index > 0) frames_->setCurrentIndex(index);
        emit reviewRequested(timestampUs);
    };
    connect(timeline_, &QSlider::sliderReleased, this, [this] {
        seek(qint64(double(timeline_->value()) / TimelineSteps * durationMs()));
    });
    connect(timeline_, &QSlider::valueChanged, this, [this](int value) {
        if (timeline_->isSliderDown()) time_->setText(videoTimeLabel(qint64(double(value) / TimelineSteps * durationMs())) + " / " + videoTimeLabel(durationMs()));
    });
    connect(player_, &QMediaPlayer::positionChanged, this, [this] { updateTime(); });
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        emit metadataChanged(duration); updateTime();
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus state) {
        if (state == QMediaPlayer::LoadedMedia && initialSeek_) {
            initialSeek_ = false;
            seek(initialPositionMs_);
        } else if (state == QMediaPlayer::EndOfMedia && lastFrame_.isValid()) settlePausedFrame();
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) {
        playing_ = false; seeking_ = false; freezeNext_ = false; initialSeek_ = false;
        audio_->setMuted(true);
        stage_ = Stage::Error; errorText_ = error; retranslate();
        emit failed(status_->text());
    });
    connect(sink_, &QVideoSink::videoFrameChanged, this, &VideoPlayback::receiveFrame);
    retranslate();
}
QWidget *VideoPlayback::videoWidget() const { return view_; }
bool VideoPlayback::playing() const { return playing_; }
QSize VideoPlayback::frameSize() const {
    if (!lastFrame_.isValid()) return {};
    auto size = lastFrame_.size();
    if (int(lastFrame_.rotation()) % 180) size.transpose();
    return size;
}
qint64 VideoPlayback::positionMs() const {
    if (seeking_) return seekTargetMs_;
    return !playing() && !seeking_ && pausedTimeUs_ >= 0 ? pausedTimeUs_ / 1000 : player_->position();
}
qint64 VideoPlayback::durationMs() const { return player_->duration(); }
void VideoPlayback::open(const QString &source, qint64 positionMs) {
    clear();
    initialPositionMs_ = std::max<qint64>(0, positionMs);
    initialSeek_ = true;
    const QUrl address(source);
    const auto url = address.isValid() && !address.scheme().isEmpty() && !QFileInfo(source).isAbsolute()
        ? address : QUrl::fromLocalFile(QFileInfo(source).absoluteFilePath());
    stage_ = Stage::Opening; retranslate();
    player_->setSource(url);
    freezeNext_ = true;
    player_->play();
}
void VideoPlayback::clear() {
    playing_ = false;
    captured_ = false;
    audio_->setMuted(true);
    player_->stop(); player_->setSource({}); lastFrame_ = {}; view_->clear();
    seeking_ = freezeNext_ = initialSeek_ = false;
    pausedTimeUs_ = -1;
    stage_ = Stage::Idle; errorText_.clear();
    frames_->clear(); timeline_->setValue(0);
    static_cast<VideoTimeline *>(timeline_)->setMarkers({});
    retranslate();
}
void VideoPlayback::toggle() {
    if (initialSeek_ || !lastFrame_.isValid()) return;
    if (playing()) pause();
    else {
        if (player_->source().isEmpty()) return;
        playing_ = true;
        captured_ = false;
        stage_ = Stage::Playing;
        freezeNext_ = false; seeking_ = false;
        pausedTimeUs_ = -1;
        audio_->setMuted(false);
        emit playbackStarted(); player_->play(); retranslate();
    }
}
void VideoPlayback::pause() {
    playing_ = false;
    audio_->setMuted(true);
    player_->pause();
    if (!seeking_ && lastFrame_.isValid()) settlePausedFrame();
    else { freezeNext_ = true; retranslate(); }
}
void VideoPlayback::seek(qint64 positionMs) {
    if (player_->source().isEmpty() || !player_->isSeekable()) return;
    // Decoding a seek can require play(), but it is not a user playback action.
    // Transport remains on the same video surface; no screenshot is read back.
    if (playing_) pause();
    seekTargetMs_ = std::clamp<qint64>(positionMs, 0, std::max<qint64>(0, durationMs() - 1));
    seeking_ = true; freezeNext_ = true;
    captured_ = false;
    audio_->setMuted(true);
    emit positioningStarted();
    stage_ = Stage::Seeking; retranslate();
    player_->setPosition(seekTargetMs_);
    player_->play();
}
void VideoPlayback::reviewAt(qint64 timestampUs) {
    playing_ = false;
    captured_ = true;
    audio_->setMuted(true);
    freezeNext_ = false; seeking_ = false; initialSeek_ = false;
    pausedTimeUs_ = timestampUs;
    player_->pause(); player_->setPosition(timestampUs / 1000);
    stage_ = Stage::Saved; retranslate();
    time_->setText(videoTimeLabel(timestampUs / 1000) + " / " + videoTimeLabel(durationMs()));
}
void VideoPlayback::receiveFrame(const QVideoFrame &frame) {
    if (!frame.isValid()) return;
    const qint64 pts = frame.startTime();
    if (seeking_ && pts >= 0) {
        // Ignore queued frames from the old position. A seek resolves on an actual
        // decoded frame, never by pairing player.position() with an older image.
        const qint64 end = frame.endTime() > pts ? frame.endTime() : pts + 100000;
        const qint64 target = seekTargetMs_ * 1000;
        if (pts > target + 200000 || end < target - 100000) return;
    }
    lastFrame_ = frame;
    view_->setFrame(frame);
    if (freezeNext_ && !initialSeek_) {
        seeking_ = false; freezeNext_ = false;
        player_->pause(); settlePausedFrame();
    } else if (!playing_ && !initialSeek_ && !seeking_ && !captured_ && pts >= 0 && pts != pausedTimeUs_) {
        // A queued frame can finish presentation after pause(). Keep metadata
        // paired with that final presented frame without reading pixels back.
        settlePausedFrame();
    }
}
void VideoPlayback::settlePausedFrame() {
    playing_ = false;
    audio_->setMuted(true);
    if (!lastFrame_.isValid()) return;
    pausedTimeUs_ = lastFrame_.startTime();
    stage_ = Stage::Paused; retranslate();
    emit framePaused(frameSize(), pausedTimeUs_);
    updateTime();
}
bool VideoPlayback::captureCurrentFrame() {
    if (playing_ || positioning() || !lastFrame_.isValid()) return false;
    if (captured_) return true;
    const QVideoFrame captured = lastFrame_;
    const qint64 pts = captured.startTime();
    if (pts < 0) {
        emit frameCaptureFailed(tr("当前视频没有可用的画面时间戳，无法安全关联批注"));
        return false;
    }
    const QImage image = frameImage(captured);
    if (image.isNull()) { emit frameCaptureFailed(tr("无法读取当前视频画面")); return false; }
    captured_ = true;
    pausedTimeUs_ = pts;
    emit frameCaptured(image, pts);
    updateTime();
    return true;
}
void VideoPlayback::setAnnotatedFrames(const QVector<QPair<qint64, int>> &frames) {
    const QSignalBlocker blocker(frames_);
    const auto selected = frames_->currentData();
    frames_->clear(); frames_->addItem(tr("已标注画面 %1 个").arg(frames.size()));
    auto timeline = static_cast<VideoTimeline *>(timeline_);
    QVector<VideoTimeline::Marker> markers;
    for (const auto &[pts, count] : frames) {
        const QString label = tr("%1 · %2 条批注").arg(videoTimeLabel(pts / 1000)).arg(count);
        frames_->addItem(label, pts);
        if (durationMs() > 0)
            markers.append({qRound(double(pts / 1000) / durationMs() * TimelineSteps), pts,
                                      tr("%1 · 点击查看批注").arg(label)});
    }
    if (selected.isValid()) { int index = frames_->findData(selected); if (index >= 0) frames_->setCurrentIndex(index); }
    timeline->setMarkers(std::move(markers));
}
void VideoPlayback::updateTime() {
    const QString duration = videoTimeLabel(durationMs());
    QString reserved = duration + " / " + duration;
    for (auto &character : reserved) if (character.isDigit()) character = QLatin1Char('8');
    time_->setFixedWidth(time_->fontMetrics().horizontalAdvance(reserved) + 4);
    if (!timeline_->isSliderDown()) {
        QSignalBlocker blocker(timeline_);
        timeline_->setValue(durationMs() > 0 ? qRound(double(positionMs()) / durationMs() * TimelineSteps) : 0);
        time_->setText(videoTimeLabel(positionMs()) + " / " + videoTimeLabel(durationMs()));
    }
    timeline_->setEnabled(player_->isSeekable());
}
void VideoPlayback::retranslate() {
    play_->setText(playing() ? tr("暂停") : tr("播放"));
    // Reserve both labels so neither language nor transport state moves the timeline.
    const int labelWidth = std::max(fontMetrics().horizontalAdvance(tr("暂停")),
                                   fontMetrics().horizontalAdvance(tr("播放")));
    play_->setFixedWidth(labelWidth + 32);
    play_->setEnabled(!player_->source().isEmpty() && !initialSeek_);
    switch (stage_) {
    case Stage::Opening: status_->setText(tr("正在打开视频…")); break;
    case Stage::Seeking: status_->setText(tr("正在定位画面…")); break;
    case Stage::Playing: status_->setText(tr("播放中 · 暂停后即可批注")); break;
    case Stage::Paused: status_->setText(tr("已暂停 · 开始批注时保存当前画面")); break;
    case Stage::Saved: status_->setText(tr("已保存画面 · %1").arg(videoTimeLabel(pausedTimeUs_ / 1000))); break;
    case Stage::Error: status_->setText(tr("视频无法播放：%1。已保存的批注画面仍可查看。").arg(errorText_)); break;
    case Stage::Idle: status_->clear(); break;
    }
    status_->setToolTip(status_->text());
    back_->setToolTip(tr("向前定位 1 秒")); forward_->setToolTip(tr("向后定位 1 秒"));
    timeline_->setAccessibleName(tr("视频时间轴"));
    relocate_->setText(tr("重新指定视频")); updateTime();
}
} // namespace h2d
