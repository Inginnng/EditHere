#pragma once
#include "capturesession.h"
#include "platform.h"
#include "scrollcapture.h"
#include "settings.h"
#include <QColor>
#include <QImage>
#include <optional>
#include <QProcess>
#include <QRect>
#include <QTimer>
#include <QWidget>
class QPainter;
namespace h2d {
class CaptureToolbar;
class CaptureSidebar;
// The window a capture starts in. It shows one screen, lets a region be drawn, and
// then keeps that region while the bar above it and the column of tools beside it
// offer what to do with it. Handing the region to the annotation editor is one of
// those things rather than the only one, which is the difference from the way a
// capture used to end.
class Overlay final : public QWidget {
    Q_OBJECT
  public:
    explicit Overlay(ScreenFrame frame, QWidget *parent = nullptr);
    const ScreenFrame &frame() const {
        return frame_;
    }
    // Gives up the current region and waits for a new one.
    void resetSelection();
    // The pixels of the region with the chosen corner radius, border and shadow
    // applied, which is the picture the picture-making actions work on.
    QImage selectionImage() const;
    // The region exactly as it was captured, with no decoration. Reading text works on
    // this rather than on the decorated picture: a shadow is presentation, and a halo
    // around the words is neither something to recognise nor a place to point at.
    QImage selectionPixels() const;
    QRect selection() const {
        return selected_;
    }
    // Where the pointer is, in this window's own coordinates. An arrow key moves it by
    // one of these, which is the smallest step a pointer can be put on, so a caller
    // watching it sees exactly one step per press however fine the pixels of the grab
    // underneath are.
    QPoint pointer() const {
        return pointer_;
    }
    // The block the window is offering, which is the one a click would take. It is the
    // block the wheel picked, so a caller can tell whether that choice was kept.
    std::optional<Candidate> hovered() const {
        return picker_.current();
    }
    // The corner radius, border and shadow the user settled on; the caller applies
    // them again to a picture the window did not take itself, such as a long capture.
    const CaptureStyle &style() const {
        return style_;
    }
    // The style every later capture should start from, as the user asked for it to be
    // remembered. Applied to this window too.
    void setStyle(const CaptureStyle &style);
    // Where a picture of this size belongs, in global logical coordinates, so that the
    // region it was taken from reappears exactly where it was instead of in the middle
    // of the screen. The picture may be larger than the region because of a shadow,
    // which is why the size is asked for rather than assumed.
    QRect placementFor(QSize picture) const;
    // Where the wheel has to be sent to scroll the page in the region.
    ScrollTarget scrollTarget() const;
    // While something is running the region is frozen and the bar says what is going
    // on, so the picture under it does not move while it is being read.
    void setBusy(bool busy, const QString &message = {});
    void setOcrLanguage(OcrLanguageMode language);
    // The recent work the owner knows about, handed on to the bar's menu. The window
    // only lists and forwards: the pictures themselves stay with the owner.
    void setHistory(const QStringList &labels, int index = -1);
    // Which entry of the history is on show, or -1 for the screen itself.
    void setHistoryIndex(int index);
    void setSelections(const QVector<QRect> &selections);
    // Shows one of the earlier captures in place of the screen, so the recent work can
    // be browsed without leaving the window. A null picture brings the screen back,
    // which is what stepping forward past the newest capture means.
    void showHistoryPicture(const QImage &picture);
  signals:
    void accepted(QRect pixels, QVector<h2d::Candidate> candidates);
    void copyRequested(QRect pixels);
    void pinRequested(QRect pixels);
    void saveRequested(QRect pixels);
    void ocrRequested(QRect pixels, h2d::OcrLanguageMode language);
    void scrollRequested(QRect pixels);
    // The user picked an earlier capture to open instead of taking a new one.
    void historyRequested(int index);
    // The user stepped through the history with < or >.
    void historyStepRequested(int delta);
    // The user asked for the current corner radius, border and shadow to be what every
    // later capture starts from.
    void styleRememberRequested(const h2d::CaptureStyle &style);
    void cancelled();
    void selectionBegan();

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

  private:
    QPoint pixelPoint(QPointF point) const;
    QRectF localRect(QRect rect) const;
    // The screen, already scaled to the size of this window. Painting the full-sized
    // grab and letting the painter scale it means resampling the whole screen on every
    // repaint, and a repaint happens on every move of the pointer.
    const QImage &scaledFrame();
    void requestProbe();
    QVector<Candidate> candidates() const;
    void finish(bool copy);
    // Moves the region to a new rectangle and brings the tools up to date with it.
    void applySelection(QRect area, bool move);
    void showBar();
    void hideTools();
    // Grows or shrinks the region by one pixel on the side the arrow points at, which
    // is what Shift and Ctrl turn an arrow into now that the plain arrow moves the
    // pointer.
    void stretchSelection(Qt::Key key, int amount);
    // Moves the pointer itself by one screen pixel. Once the region has settled that is
    // what a plain arrow does: the region stays where the user put it, and the aim is
    // what moves.
    void movePointer(int dx, int dy);
    // The screen with the dimming and the frame around the region painted on it.
    // `viewport` is the piece of the window to paint, in the window's own coordinates:
    // the window asks for all of it, the enlargement for the few pixels under the
    // pointer, and both have to get the same picture.
    void drawScene(QPainter &painter, const QRectF &viewport);
    void drawMagnifier(QPainter &painter, QPointF at);
    // The next click reads a colour instead of touching the region.
    void startPicking();
    void leavePicking();
    // Brings back the most recent selection, which is the one the user is most likely
    // to want again.
    void restoreLastSelection();
    void copyColour();
    QColor colourAt(QPoint pixel) const;
    // The corner the pointer is on, or -1; the region is resized by dragging one.
    int handleAt(QPoint pixel) const;
    ScreenFrame frame_;
    // The screen this window was handed, kept because browsing the history replaces the
    // picture and something has to hand the screen back afterwards.
    ScreenFrame liveFrame_;
    QVector<Candidate> visual_, native_;
    CandidatePicker picker_;
    QRect selected_;
    QPoint start_, cursor_, keyboardOffset_;
    // Where the pointer is, in the window's own units rather than in pixels of the
    // grab. A keypress moves the pointer by one whole unit of these, which is the
    // smallest step a pointer can actually be put on; a step taken in pixels of the
    // grab and then rounded back into these sometimes landed on the pixel it started
    // from, which is what made an arrow key look dead, and sometimes rounded a
    // different way on each axis, which is what made one key move in two directions.
    QPoint pointer_;
    bool nudged_ = false;
    bool drawing_ = false, finished_ = false;
    QTimer debounce_, hoverTimer_, copyNote_;
    bool magnifierVisible_ = false;
    QProcess *probe_ = nullptr;
    QImage frameScaled_;
    CaptureToolbar *bar_ = nullptr;
    CaptureSidebar *sidebar_ = nullptr;
    CaptureStyle style_;
    CaptureRatio ratio_ = CaptureRatio::Free;
    OcrLanguageMode ocrLanguage_ = OcrLanguageMode::System;
    // The region is settled and the tools are up: the pointer now moves and resizes it
    // rather than drawing a new one.
    bool ready_ = false;
    // The next click reads a colour rather than touching the region.
    bool picking_ = false;
    bool busy_ = false;
    // Which of the four ways of writing a colour a copy puts on the clipboard.
    ColourFormat colourFormat_ = ColourFormat::Hex;
    // Set for a moment after a colour is copied, so the panel can say it happened.
    bool colourCopied_ = false;
    int handle_ = -1;
    QRect dragOrigin_;
    QPoint dragStart_;
    QVector<QRect> selections_;
};} // namespace h2d
Q_DECLARE_METATYPE(QVector<h2d::Candidate>)
Q_DECLARE_METATYPE(h2d::OcrLanguageMode)
