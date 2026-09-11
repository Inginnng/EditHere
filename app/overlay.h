#pragma once
#include "platform.h"
#include <QProcess>
#include <QTimer>
#include <QWidget>
namespace h2d {
class Overlay final : public QWidget {
    Q_OBJECT
  public:
    explicit Overlay(ScreenFrame frame, QWidget *parent = nullptr);
    const ScreenFrame &frame() const {
        return frame_;
    }
    void resetSelection();
  signals:
    void accepted(QRect pixels, QVector<h2d::Candidate> candidates);
    void copyRequested(QRect pixels);
    void cancelled();
    void selectionBegan();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    QPoint pixelPoint(QPointF point) const;
    QRectF localRect(QRect rect) const;
    void requestProbe();
    QVector<Candidate> candidates() const;
    void finish(bool copy);
    ScreenFrame frame_;
    QVector<Candidate> visual_, native_;
    CandidatePicker picker_;
    QRect selected_;
    QPoint start_, cursor_;
    bool drawing_ = false, finished_ = false;
    QTimer debounce_;
    QProcess *probe_ = nullptr;
};
} // namespace h2d
Q_DECLARE_METATYPE(QVector<h2d::Candidate>)
