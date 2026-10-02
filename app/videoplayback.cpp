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
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QVideoWidget>
#include <algorithm>
namespace h2d {
namespace {
constexpr int TimelineSteps = 1000000;
class VideoTimeline final : public QSlider {
  public:
    explicit VideoTimeline(QWidget *parent) : QSlider(Qt::Horizontal, parent) {
        setRange(0, TimelineSteps);
        setMinimumWidth(140);
    }
    QVector<int> markers;
  protected:
    void paintEvent(QPaintEvent *event) override {
        QSlider::paintEvent(event);
        QPainter painter(this);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#f69d46"));
        for (int value : markers) {
            const double x = 9.0 + double(value) / TimelineSteps * std::max(1, width() - 18);
            painter.drawEllipse(QPointF(x, height() - 4.0), 2.5, 2.5);
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) { QSlider::mousePressEvent(event); return; }
        setSliderDown(true);
        setValue(at(event));
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (isSliderDown()) { setValue(at(event)); event->accept(); }
        else QSlider::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            setValue(at(event)); setSliderDown(false); event->accept();
        } else QSlider::mouseReleaseEvent(event);
    }
  private:
    int at(QMouseEvent *event) const {
        return qRound(std::clamp((event->position().x() - 9.0) / std::max(1, width() - 18), 0.0, 1.0) * TimelineSteps);
    }
};
}
VideoPlayback::VideoPlayback(QWidget *parent) : QWidget(parent) {
    setObjectName("videoControls");
    player_ = new QMediaPlayer(this);
    audio_ = new QAudioOutput(this);
    audio_->setVolume(0.6);
    audio_->setMuted(true);
    player_->setAudioOutput(audio_);
    view_ = new QVideoWidget;
    view_->setObjectName("videoView");
    view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    view_->setFocusPolicy(Qt::NoFocus);
    view_->hide();
    player_->setVideoOutput(view_);
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
        } else if (state == QMediaPlayer::EndOfMedia && lastFrame_.isValid()) freeze();
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) {
        playing_ = false; seeking_ = false; freezeNext_ = false; initialSeek_ = false;
        audio_->setMuted(true);
        stage_ = Stage::Error; errorText_ = error; retranslate();
        emit failed(status_->text());
    });
    connect(view_->videoSink(), &QVideoSink::videoFrameChanged, this, &VideoPlayback::receiveFrame);
    retranslate();
}
QWidget *VideoPlayback::videoWidget() const { return view_; }
bool VideoPlayback::playing() const { return playing_; }
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
    audio_->setMuted(true);
    player_->stop(); player_->setSource({}); lastFrame_ = {};
    seeking_ = freezeNext_ = initialSeek_ = false;
    pausedTimeUs_ = -1;
    stage_ = Stage::Idle; errorText_.clear();
    frames_->clear(); timeline_->setValue(0);
    static_cast<VideoTimeline *>(timeline_)->markers.clear();
    retranslate();
}
void VideoPlayback::toggle() {
    if (initialSeek_ || !lastFrame_.isValid()) return;
    if (playing()) pause();
    else {
        if (player_->source().isEmpty()) return;
        playing_ = true;
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
    if (!seeking_ && lastFrame_.isValid()) freeze();
    else { freezeNext_ = true; retranslate(); }
}
void VideoPlayback::seek(qint64 positionMs) {
    if (player_->source().isEmpty() || !player_->isSeekable()) return;
    // Decoding a seek can require play(), but it is not a user playback action.
    // Keep the current annotated canvas visible until the target frame is ready.
    if (playing_) pause();
    seekTargetMs_ = std::clamp<qint64>(positionMs, 0, std::max<qint64>(0, durationMs() - 1));
    seeking_ = true; freezeNext_ = true;
    audio_->setMuted(true);
    emit positioningStarted();
    stage_ = Stage::Seeking; retranslate();
    player_->setPosition(seekTargetMs_);
    player_->play();
}
void VideoPlayback::reviewAt(qint64 timestampUs) {
    playing_ = false;
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
    if (freezeNext_ && !initialSeek_) {
        seeking_ = false; freezeNext_ = false;
        player_->pause(); freeze();
    }
}
void VideoPlayback::freeze() {
    playing_ = false;
    audio_->setMuted(true);
    retranslate();
    if (!lastFrame_.isValid()) return;
    QImage image = lastFrame_.toImage();
    if (image.isNull()) { emit failed(tr("无法读取当前视频画面")); return; }
    const int rotation = int(lastFrame_.rotation());
    if (rotation) image = image.transformed(QTransform().rotate(rotation));
    if (lastFrame_.mirrored()) image = image.mirrored(true, false);
    const qint64 pts = lastFrame_.startTime();
    if (pts < 0) { emit failed(tr("当前视频没有可用的画面时间戳，无法安全关联批注")); return; }
    pausedTimeUs_ = pts;
    stage_ = Stage::Paused; retranslate();
    emit framePaused(image, pts);
    updateTime();
}
void VideoPlayback::setAnnotatedFrames(const QVector<QPair<qint64, int>> &frames) {
    const QSignalBlocker blocker(frames_);
    const auto selected = frames_->currentData();
    frames_->clear(); frames_->addItem(tr("已标注画面 %1 个").arg(frames.size()));
    auto timeline = static_cast<VideoTimeline *>(timeline_);
    timeline->markers.clear();
    for (const auto &[pts, count] : frames) {
        frames_->addItem(tr("%1 · %2 条批注").arg(videoTimeLabel(pts / 1000)).arg(count), pts);
        if (durationMs() > 0) timeline->markers.append(qRound(double(pts / 1000) / durationMs() * TimelineSteps));
    }
    if (selected.isValid()) { int index = frames_->findData(selected); if (index >= 0) frames_->setCurrentIndex(index); }
    timeline->update();
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
    play_->setText(playing() ? tr("暂停并标注") : tr("播放"));
    // Reserve both labels so neither language nor transport state moves the timeline.
    const int labelWidth = std::max(fontMetrics().horizontalAdvance(tr("暂停并标注")),
                                   fontMetrics().horizontalAdvance(tr("播放")));
    play_->setFixedWidth(labelWidth + 32);
    play_->setEnabled(!player_->source().isEmpty() && !initialSeek_);
    switch (stage_) {
    case Stage::Opening: status_->setText(tr("正在打开视频…")); break;
    case Stage::Seeking: status_->setText(tr("正在定位画面…")); break;
    case Stage::Playing: status_->setText(tr("播放中 · 暂停后即可批注")); break;
    case Stage::Paused: status_->setText(tr("暂停后圈选、点选或写批注；橙色标记是已有批注画面。")); break;
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
