#pragma once
#include <QEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>
#include <limits>

namespace h2d {
// Image coordinates stay on the canvas; only its position in this viewport changes.
// Scrollbar values represent the view offset, independent of the image's bounds.
class ImageArea final : public QScrollArea {
  public:
    explicit ImageArea(QWidget *parent = nullptr) : QScrollArea(parent) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        updateView();
    }
    void setCanvas(QWidget *canvas) {
        const QPointF saved = origin_;
        const QSignalBlocker horizontal(horizontalScrollBar()), vertical(verticalScrollBar());
        QScrollArea::setWidget(canvas);
        origin_ = saved;
        updateView();
    }
    QPointF imageOrigin() const { return origin_; }
    void setImageOrigin(QPointF origin) {
        // Leave headroom for Qt's integer widget geometry arithmetic.
        constexpr double limit = std::numeric_limits<int>::max() / 4;
        origin_ = {std::clamp(origin.x(), -limit, limit), std::clamp(origin.y(), -limit, limit)};
        updateView();
    }
    void centerImage() {
        if (widget())
            setImageOrigin({(viewport()->width() - widget()->width()) / 2.0,
                            (viewport()->height() - widget()->height()) / 2.0});
    }

  protected:
    bool event(QEvent *event) override {
        // QScrollArea would restore image-bound ranges here, including after a theme change.
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutRequest)
            return QAbstractScrollArea::event(event);
        return QScrollArea::event(event);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == widget() && event->type() == QEvent::Resize) {
            updateView();
            return false;
        }
        return QScrollArea::eventFilter(object, event);
    }
    void resizeEvent(QResizeEvent *event) override {
        QAbstractScrollArea::resizeEvent(event);
        updateView();
    }
    void scrollContentsBy(int, int) override {
        origin_ = {-double(horizontalScrollBar()->value()), -double(verticalScrollBar()->value())};
        if (widget()) widget()->move(origin_.toPoint());
    }
    bool focusNextPrevChild(bool next) override {
        // Moving keyboard focus must not automatically recenter a freely positioned canvas.
        return QWidget::focusNextPrevChild(next);
    }

  private:
    void updateView() {
        const QSignalBlocker horizontal(horizontalScrollBar()), vertical(verticalScrollBar());
        constexpr int extent = std::numeric_limits<int>::max() / 4;
        horizontalScrollBar()->setRange(-extent, extent);
        verticalScrollBar()->setRange(-extent, extent);
        horizontalScrollBar()->setPageStep(viewport()->width());
        verticalScrollBar()->setPageStep(viewport()->height());
        horizontalScrollBar()->setValue(qRound(-origin_.x()));
        verticalScrollBar()->setValue(qRound(-origin_.y()));
        if (widget()) widget()->move(origin_.toPoint());
    }
    QPointF origin_;
};
} // namespace h2d
