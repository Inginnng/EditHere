#pragma once
#include "capturesession.h"
#include <QImage>
#include <QMenu>
#include <QRect>
#include <QWidget>
namespace h2d {
// A window edge, usable as a combination: which sides the pointer is near decides both
// the cursor it gets and which way a drag resizes the picture.
enum Side { SideNone = 0, SideLeft = 1, SideRight = 2, SideTop = 4, SideBottom = 8 };
// A picture held on top of everything else, so a reference can sit beside the window
// being worked on. It is frameless, movable, and scales with the wheel or by dragging
// one of its edges.
class PinWindow final : public QWidget {
    Q_OBJECT
  public:
    // `placement` is where the picture belongs, in global logical coordinates. A
    // region handed over straight from a capture arrives with the rectangle it was
    // taken from, so the pin lands on top of the very thing it shows instead of in
    // the middle of the screen. An empty rectangle means "no opinion", which is what
    // the editor's pin action has, and that one is centred on the screen.
    explicit PinWindow(QImage image, QRect placement = {}, QWidget *parent = nullptr);
    const QImage &image() const {
        return image_;
    }
    // Where the picture was first put. Not updated as it is dragged: it is the place
    // the pin belongs to, which is what the menu can put it back to.
    QRect placement() const {
        return placement_;
    }
    // The size the picture is drawn at where it was put, before any wheel zoom.
    QSize baseSize() const {
        return baseSize_;
    }
    // How much of the picture on every side is shadow rather than screenshot. A pin of
    // a capture carries the halo it was given, and this is how much of it is not the
    // thing that was captured.
    void setDecoration(int pixels);
    int decoration() const {
        return decoration_;
    }
    // The picture with that margin taken back off, which is what the editor is handed.
    QImage editableImage() const;
    // The picture before the decoration was applied, and the style that was applied to
    // it. Having both is what lets the shadow be turned on and off later instead of
    // being baked into the pixels for good.
    void setUndecorated(const QImage &source, const CaptureStyle &style);
    bool canToggleShadow() const {
        return !undecorated_.isNull();
    }
    const CaptureStyle &style() const {
        return style_;
    }
    bool shadowEnabled() const {
        return style_.shadow;
    }
    void setShadowEnabled(bool on);
    // Turns the picture a quarter turn at a time, keeping the middle where it is.
    void rotate(int quarters);
    // Turns it over, left to right or top to bottom.
    void flip(bool horizontal);
    // What is actually painted: the picture resampled for the window, carrying the
    // device pixel ratio it was resampled for. Readable so a test can tell a pin that
    // kept every pixel from one that threw half of them away and invented them again.
    const QImage &display() const {
        return display_;
    }
    // The menu the right button opens. Built here rather than inside the event so a
    // test can check that a pin still offers to be annotated.
    QMenu *createContextMenu();

  signals:
    void copyRequested(const QImage &image);
    void saveRequested(const QImage &image);
    void ocrRequested(const QImage &image);
    // The pinned picture handed back to the editor, which is what the whole program is
    // for: a pin is a place to keep a picture, not somewhere it has to stay.
    void annotateRequested(const QImage &image);
    void closed(PinWindow *window);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    void setZoom(qreal zoom);
    void setTransparency(qreal transparency);
    // Puts the picture back together from the undecorated one and the style, then
    // resizes and re-centres the window around where it already was.
    void rebuild();
    // Which edge or corner the point is on, as a combination of Side bits, or zero.
    int edgeAt(QPoint point) const;
    void applyEdgeCursor(int edge);
    // Grows or shrinks the window to a size the pointer dragged it to.
    void resizeTo(QSize size);
    // How many device pixels there are to a logical one where this window is. A capture
    // carries the answer in the region it came from.
    qreal deviceRatio() const;
    // The size the picture is shown at before any wheel zoom, in logical pixels.
    QSize baseLogical() const;
    // Puts the picture back at its original size and place, or centres and fits it
    // when it never had one.
    void fitToScreen();
    void retranslate();
    QImage image_;
    QImage display_;
    // The picture with no decoration, kept so the decoration can be changed later.
    QImage undecorated_;
    CaptureStyle style_;
    QPoint offset_;
    qreal zoom_ = 1.0;
    qreal transparency_ = 0.0;
    // The scale the picture is shown at where it was placed, remembered so that
    // "original size" can mean the size it arrived at rather than one pixel per pixel.
    qreal placedZoom_ = 1.0;
    QSize baseSize_;
    QRect placement_;
    // A frame is drawn around a picture that was dropped on the screen out of nowhere;
    // one that took the place of the region it came from is the region, so a frame
    // around it would be a lie about its size.
    bool dragging_ = false;
    int decoration_ = 0;
    // Which edge is being dragged to resize, zero when none is.
    int draggingEdge_ = 0;
    QRect dragStartGeometry_;
    QPoint dragStartGlobal_;
};
} // namespace h2d
