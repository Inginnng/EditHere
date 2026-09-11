#pragma once
#include "layout.h"
#include <QDialog>
#include <QWidget>
class QDoubleSpinBox;
class QScrollArea;
class QLabel;
class QPushButton;
namespace h2d {
class LayoutCanvas final : public QWidget {
    Q_OBJECT
  public:
    LayoutCanvas(QImage original, LayoutState state, QWidget *parent = nullptr);
    const LayoutState &state() const {
        return state_;
    }
    QString selected() const {
        return selected_;
    }
    QRectF selectionBounds() const {
        return layoutBounds(state_, selected_);
    }
    double zoom() const {
        return zoom_;
    }
    void setZoom(double value);
    void setDrawing(bool enabled);
    bool drawingMode() const {
        return drawingMode_;
    }
    void clearSelection();
    void transformSelection(QRectF destination);
    void undo();
    void redo();
    bool canUndo() const {
        return !undo_.isEmpty();
    }
    bool canRedo() const {
        return !redo_.isEmpty();
    }
    void setGuides(bool visible);
  signals:
    void changed();
    void selectionChanged();
    void hintChanged(QString hint);
    void zoomRequested(double value);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;

  private:
    QPointF pixel(QPointF position) const;
    void updateHover(QPointF point);
    void commit(const LayoutState &before);
    void select(QString id);
    QImage original_;
    LayoutState state_, before_;
    QVector<LayoutState> undo_, redo_;
    QString selected_, hover_;
    QVector<LayoutChoice> choices_;
    int level_ = 0, handle_ = -1;
    double zoom_ = 1;
    bool dragging_ = false, drawingMode_ = false, drawing_ = false, guides_ = true;
    QPointF press_, end_, hoverAnchor_{-1000, -1000};
    QRectF initial_;
};
class ExplosionDialog final : public QDialog {
    Q_OBJECT
  public:
    ExplosionDialog(const QImage &original, LayoutState state, QWidget *parent = nullptr);
    const LayoutState &result() const {
        return canvas_->state();
    }
    LayoutCanvas *canvas() const {
        return canvas_;
    }

  protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    void fit();
    void refresh();
    void applyField(int field);
    LayoutCanvas *canvas_;
    QScrollArea *scroll_;
    QLabel *hint_, *selection_, *count_;
    QPushButton *undo_, *redo_, *clear_, *manual_;
    QVector<QDoubleSpinBox *> fields_;
    bool updating_ = false, fitted_ = true;
    QString fieldSelection_;
    QRectF scaleBase_;
};
} // namespace h2d
