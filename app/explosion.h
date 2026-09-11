#pragma once
#include "model.h"
#include <QWidget>
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QVariantAnimation;
namespace h2d {
class LayoutCanvas final : public QWidget {
    Q_OBJECT
  public:
    LayoutCanvas(QImage original, LayoutState state, QWidget *parent = nullptr);
    const LayoutState &state() const {
        return state_;
    }
    void setState(LayoutState state);
    void setAnnotations(QVector<Note> notes);
    void annotateSelection();
    void cancelInteraction();
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
    void noteEditRequested(QString id, QPoint global);
    void annotationRequested(QRect area, QPoint global);

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
    QPointF noteAnchor(const Note &note) const;
    QVector<Note> displayedAnnotations() const;
    void updateHover(QPointF point);
    void commit(const LayoutState &before);
    void select(QString id);
    QImage original_;
    LayoutState state_, before_, annotationState_;
    QVector<Note> annotations_;
    QVector<LayoutState> undo_, redo_;
    QString selected_, hover_;
    QVector<LayoutChoice> choices_;
    int level_ = 0, handle_ = -1;
    double zoom_ = 1;
    bool dragging_ = false, drawingMode_ = false, drawing_ = false, guides_ = true;
    QPointF press_, end_, hoverAnchor_{-1000, -1000};
    QRectF initial_;
};
class LayoutInspector final : public QWidget {
    Q_OBJECT
  public:
    explicit LayoutInspector(LayoutCanvas *canvas, QWidget *parent = nullptr);

  protected:
    void keyPressEvent(QKeyEvent *) override;

  private:
    void refresh();
    void applyField(int field);
    LayoutCanvas *canvas_;
    QLabel *selection_;
    QPushButton *clear_, *manual_, *annotate_;
    QVector<QDoubleSpinBox *> fields_;
    bool updating_ = false;
    QString fieldSelection_;
    QRectF scaleBase_;
};
class ExplosionWave final : public QWidget {
    Q_OBJECT
  public:
    explicit ExplosionWave(QWidget *parent = nullptr);
    void start();

  protected:
    void paintEvent(QPaintEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    QVariantAnimation *animation_;
    qreal progress_ = 0;
};
} // namespace h2d
