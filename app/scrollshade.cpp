#include "scrollshade.h"
#include "platform.h"
#include <QPainter>
#include <QPainterPath>
namespace h2d {
// The film is the colour the ordinary capture dims the screen with, so a long capture
// reads as the same mode with one region kept alive rather than as a new interface.
namespace {
constexpr auto kFilmColour = QColor(16, 18, 24, 105);
} // namespace

ScrollShade::ScrollShade(QSize imageSize, const QRect &geometry) : imageSize_(imageSize) {
    // Tool keeps the film out of the task bar and the Alt-Tab list; staying on top is
    // what a film is for. It must never take the pointer or the keyboard away from the
    // page being scrolled — every input the film would receive is input the page does
    // not, and scrolling is what drives the whole capture.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setGeometry(geometry);
}

void ScrollShade::setHole(QRect pixels) {
    pixels_ = pixels;
    update();
}

void ScrollShade::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // The film has to stay out of the frames the capture reads: it covers the whole
    // screen, and a film that was read back would dim every frame of the run.
    configureNativeWindow(this, true);
}

void ScrollShade::paintEvent(QPaintEvent *) {
    QPainter p(this);
    QPainterPath film;
    film.addRect(rect());
    if (!pixels_.isEmpty() && imageSize_.width() > 0 && imageSize_.height() > 0 && width() > 0 &&
        height() > 0) {
        // The hole is handed over in the pixels of the grab while the film is laid out
        // in its own units, so it is mapped the way the capture window maps its region.
        const double sx = double(width()) / imageSize_.width();
        const double sy = double(height()) / imageSize_.height();
        film.addRect(QRectF(pixels_.left() * sx, pixels_.top() * sy, pixels_.width() * sx,
                            pixels_.height() * sy));
    }
    film.setFillRule(Qt::OddEvenFill);
    p.fillPath(film, kFilmColour);
}
} // namespace h2d
