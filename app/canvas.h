#pragma once
#include "model.h"
#include <QWidget>
namespace h2d {
class Canvas final : public QWidget {
    Q_OBJECT
  public:
    enum Mode { Smart, Point, Rectangle, Adjust };
    explicit Canvas(QWidget *parent = nullptr);
    void setDocument(Document *doc);
    void setMode(Mode mode);
    Mode mode() const {
        return mode_;
    }
    void setZoom(double zoom);
    double zoom() const {
        return zoom_;
    }
    void select(const QString &id);
    QString selected() const {
        return selected_;
    }
    void refresh();
    QPoint toImage(QPointF position) const;
  signals:
    void editRequested(h2d::Note note, bool fresh, QPoint global);
    void geometryChanged(h2d::Note note);
    void selectionChanged(QString id);
    void hintChanged(QString text);
    void zoomRequested(double zoom);
    void contextRequested(QPoint global);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    int hit(QPointF screen, bool rectangles) const;
    void updateHint();
    Document *doc_ = nullptr;
    Mode mode_ = Smart;
    double zoom_ = 1;
    QString selected_;
    CandidatePicker picker_;
    bool drawing_ = false, moving_ = false, panning_ = false, space_ = false;
    QPoint start_, end_, panStart_, windowStart_;
    Note original_, preview_;
    int handle_ = -1;
    std::optional<Candidate> pending_;
};
} // namespace h2d
Q_DECLARE_METATYPE(h2d::Note)
