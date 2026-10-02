#pragma once
#include <QWidget>
#include <QVideoFrame>
#include <QVector>
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QLabel;
class QPushButton;
class QComboBox;
class QSlider;
namespace h2d {
class VideoPlayback final : public QWidget {
    Q_OBJECT
  public:
    explicit VideoPlayback(QWidget *parent = nullptr);
    QWidget *videoWidget() const;
    void open(const QString &source, qint64 positionMs = 0);
    void clear();
    void toggle();
    void pause();
    void seek(qint64 positionMs);
    void reviewAt(qint64 timestampUs);
    void setAnnotatedFrames(const QVector<QPair<qint64, int>> &frames);
    bool playing() const;
    bool positioning() const { return seeking_ || initialSeek_; }
    qint64 positionMs() const;
    qint64 durationMs() const;
    void retranslate();
  signals:
    void playbackStarted();
    void positioningStarted();
    void framePaused(const QImage &image, qint64 timestampUs);
    void metadataChanged(qint64 durationMs);
    void reviewRequested(qint64 timestampUs);
    void sourceRelocationRequested();
    void failed(const QString &message);
  private:
    void receiveFrame(const QVideoFrame &frame);
    void freeze();
    void updateTime();
    QMediaPlayer *player_;
    QAudioOutput *audio_;
    QVideoWidget *view_;
    QPushButton *play_, *back_, *forward_, *relocate_;
    QLabel *time_, *status_;
    QSlider *timeline_;
    QComboBox *frames_;
    QVideoFrame lastFrame_;
    bool freezeNext_ = false;
    bool playing_ = false;
    bool seeking_ = false;
    qint64 seekTargetMs_ = 0;
    qint64 initialPositionMs_ = 0;
    bool initialSeek_ = false;
    qint64 pausedTimeUs_ = -1;
    enum class Stage { Idle, Opening, Seeking, Playing, Paused, Saved, Error };
    Stage stage_ = Stage::Idle;
    QString errorText_;
};
} // namespace h2d
