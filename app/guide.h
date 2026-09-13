#pragma once
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>
class QLabel;
class QPushButton;
class QGraphicsOpacityEffect;
class QVariantAnimation;
namespace h2d {
class GuideOverlay final : public QWidget {
    Q_OBJECT
  public:
    explicit GuideOverlay(QWidget *parent);
    ~GuideOverlay() override;
    void start();
    void dismiss();
    int currentStep() const { return step_; }
    int stepCount() const { return 6; }
    QVector<QRect> highlightRects() const { return highlights_; }
    QStringList highlightedControls() const { return highlightedControls_; }
  signals:
    void dismissed();
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
  private:
    void setStep(int step);
    void placeCard();
    void queuePlacement();
    bool belongsToEditor(QObject *object) const;
    QWidget *visibleControl(const QString &name) const;
    QWidget *card_;
    QLabel *title_, *body_, *progress_;
    QPushButton *back_, *next_, *skip_;
    QGraphicsOpacityEffect *fade_;
    QVariantAnimation *animation_;
    QPointer<QWidget> previousFocus_;
    QVector<QRect> highlights_;
    QStringList highlightedControls_;
    int step_ = 0;
    bool placementQueued_ = false;
};
} // namespace h2d