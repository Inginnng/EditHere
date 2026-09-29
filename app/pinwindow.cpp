#include "pinwindow.h"
#include "platform.h"
#include "ui.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <memory>

namespace h2d {
namespace {
constexpr qreal kMinimumZoom = 0.1;
constexpr qreal kMaximumZoom = 4.0;
// How thick the outline drawn around a pin is, in logical pixels.
constexpr int kOutline = 2;
// How close to an edge the pointer has to be to grab it.
constexpr int kEdgeGrab = 6;
// The smallest a pin can be dragged down to, so it can always be grabbed again.
constexpr int kMinimumSide = 24;
// The colour the halo falls back to once the user has gone to another window. It is
// the shadow of a picture nobody is looking at: still there, so the pin still reads
// as a thing lying on the desktop, but no longer claiming the accent — which is how
// several pins say which one is being worked on.
constexpr auto kIdleShadow = QColor(120, 120, 122);
} // namespace

PinWindow::PinWindow(QImage image, QRect placement, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      image_(std::move(image)), baseSize_(image_.size()), placement_(placement) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    // The picture may carry a shadow, and a shadow needs the pixels around it to be
    // the desktop rather than an opaque grey rectangle.
    setAttribute(Qt::WA_TranslucentBackground, true);
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    // Without this the cursor only changes while a button is held, and the edges are
    // meant to announce themselves before the drag starts.
    setMouseTracking(true);
    if (placement_.isValid() && !image_.isNull() && placement_.width() > 0) {
        // A screenshot comes back at the size the region was on screen, which on a
        // scaled display is not one picture pixel per screen pixel. Taking the place
        // of the region means being drawn at the scale the region was drawn at.
        placedZoom_ = 1.0;
        baseSize_ = placement_.size();
        setZoom(placedZoom_);
        move(placement_.topLeft());
    } else {
        placement_ = {};
        fitToScreen();
    }
    retranslate();
}

void PinWindow::retranslate() {
    setWindowTitle(tr("置顶图片 %1 × %2").arg(image_.width()).arg(image_.height()));
    setToolTip(tr("拖动移动 · 拖边缘缩放 · 滚轮缩放 · 双击关闭 · Ctrl+C 复制 · 右键更多选项 · Esc 关闭"));
    setAccessibleName(windowTitle());
}

qreal PinWindow::deviceRatio() const {
    // The picture is made of device pixels and the window is measured in logical ones.
    // On a display scaled by two they are not the same number, and drawing without
    // saying so throws away half the pixels on the way in and invents them again on
    // the way out, which is what a blurry pin is.
    if (!placement_.isEmpty() && placement_.width() > 0 && !image_.isNull())
        return std::clamp(double(image_.width()) / placement_.width(), 0.25, 4.0);
    if (auto *here = screen())
        return std::clamp(double(here->devicePixelRatio()), 0.25, 4.0);
    if (auto *primary = QApplication::primaryScreen())
        return std::clamp(double(primary->devicePixelRatio()), 0.25, 4.0);
    return 1.0;
}

void PinWindow::fitToScreen() {
    if (image_.isNull())
        return;
    const QRect available = screen() != nullptr ? screen()->availableGeometry()
                                                : QApplication::primaryScreen()->availableGeometry();
    if (!placement_.isEmpty()) {
        // Putting it back means the place and the size it was placed at, both.
        placedZoom_ = 1.0;
        baseSize_ = placement_.size();
        setZoom(placedZoom_);
        move(placement_.topLeft());
        return;
    }
    // A picture bigger than the screen is shown reduced, so pinning something tall
    // does not put most of it out of reach.
    qreal zoom = 1.0;
    const QSize logical = baseLogical();
    if (logical.width() > available.width() || logical.height() > available.height()) {
        zoom = std::min(double(available.width()) / logical.width(),
                        double(available.height()) / logical.height());
    }
    setZoom(std::clamp(zoom, kMinimumZoom, kMaximumZoom));
    move(available.center() - rect().center());
}

QSize PinWindow::baseLogical() const {
    // The size the picture is shown at before any wheel zoom, in the units the window
    // manager uses. A capture carries the region it came from and is shown at that
    // size; one that arrived on its own is shown at its own size.
    if (!baseSize_.isEmpty() && !placement_.isEmpty())
        return baseSize_;
    const qreal ratio = deviceRatio();
    return QSize(std::max(1, qRound(image_.width() / ratio)),
                 std::max(1, qRound(image_.height() / ratio)));
}

void PinWindow::setZoom(qreal zoom) {
    zoom_ = std::clamp(zoom, kMinimumZoom, kMaximumZoom);
    const qreal ratio = deviceRatio();
    const QSize logical(std::max(1, qRound(baseLogical().width() * zoom_)),
                        std::max(1, qRound(baseLogical().height() * zoom_)));
    // The pixels are what the picture is made of; the window is told how many logical
    // pixels they cover. Keeping the two apart is the whole trick, and it is why a pin
    // of a capture is drawn exactly as sharp as the capture was.
    const QSize pixels(std::max(1, qRound(logical.width() * ratio)),
                       std::max(1, qRound(logical.height() * ratio)));
    if (image_.size() == pixels) {
        display_ = image_;
    } else {
        // The picture is painted into a target of the wanted size rather than asked
        // for from QImage::scaled, because the scaler is free to round a pixel away
        // and a pin put back where it came from has to be the exact size of the
        // region it was taken from.
        display_ = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        display_.fill(Qt::transparent);
        QPainter painter(&display_);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(display_.rect(), image_);
        painter.end();
    }
    display_.setDevicePixelRatio(ratio);
    // The outline is drawn over the outermost pixels rather than around them, so the
    // window is exactly the size the picture is: a pin of a region has to be the size
    // of that region, to the pixel.
    setFixedSize(logical);
    update();
}

void PinWindow::setDecoration(int pixels) {
    decoration_ = std::max(0, pixels);
}

void PinWindow::setUndecorated(const QImage &source, const CaptureStyle &style) {
    if (source.isNull())
        return;
    undecorated_ = source;
    style_ = style;
}

void PinWindow::setShadowEnabled(bool on) {
    if (undecorated_.isNull() || style_.shadow == on)
        return;
    style_.shadow = on;
    rebuild();
}

void PinWindow::changeEvent(QEvent *event) {
    // Becoming the window the user is on, and ceasing to be it, both arrive here.
    // Nothing else about the pin changes: the same picture stays put, only the halo
    // changes colour.
    if (event->type() == QEvent::ActivationChange)
        setActive(isActiveWindow());
    QWidget::changeEvent(event);
}

CaptureStyle PinWindow::effectiveStyle() const {
    CaptureStyle style = style_;
    if (!active_ && style.shadow)
        style.shadowColor = kIdleShadow;
    return style;
}

void PinWindow::setActive(bool active) {
    if (active_ == active)
        return;
    active_ = active;
    // Only the colour of the halo changes, and only a pin that has one has anything
    // to redraw: a picture that arrived on its own has no shadow either way.
    if (canToggleShadow() && style_.shadow)
        rebuild();
    else
        update();
}

void PinWindow::rebuild() {
    // A pin is dragged around by hand as often as it is placed, so what has to stay
    // put is the middle of where it is now rather than the rectangle it arrived in.
    const QPoint centre = geometry().center();
    // Read before anything changes: the ratio is what the region it came from says it
    // is, and it stops saying that the moment the picture grows.
    const qreal ratio = deviceRatio();
    const int before = decoration_;
    image_ = composeCapture(undecorated_, effectiveStyle());
    decoration_ = style_.shadowRadius();
    if (!placement_.isEmpty()) {
        // The rectangle it belongs to grows with the halo, in the units the window
        // manager measures in.
        const int grown = qRound(double(decoration_ - before) / ratio);
        placement_.adjust(-grown, -grown, grown, grown);
        baseSize_ = placement_.size();
    }
    setZoom(zoom_);
    move(centre - rect().center());
    retranslate();
}

void PinWindow::rotate(int quarters) {
    if (quarters % 4 == 0)
        return;
    QTransform turn;
    turn.rotate(quarters * 90);
    const QPoint centre = geometry().center();
    image_ = image_.transformed(turn);
    if (!undecorated_.isNull())
        undecorated_ = undecorated_.transformed(turn);
    if (!placement_.isEmpty()) {
        // The rectangle it belongs to turns with it, around the same middle.
        QRect turned(0, 0, placement_.height(), placement_.width());
        turned.moveCenter(placement_.center());
        placement_ = turned;
        baseSize_ = turned.size();
    }
    setZoom(zoom_);
    move(centre - rect().center());
    retranslate();
}

QImage PinWindow::editableImage() const {
    // The editor gets the screenshot inside the shadow rather than the shadow with it:
    // a margin of nothing is not something to mark up, and 批注 from the capture bar
    // starts from the bare region too.
    const int twice = decoration_ * 2;
    if (decoration_ <= 0 || image_.isNull() || image_.width() <= twice || image_.height() <= twice)
        return image_;
    return image_.copy(decoration_, decoration_, image_.width() - twice, image_.height() - twice);
}

void PinWindow::flip(bool horizontal) {
    QTransform mirror;
    mirror.scale(horizontal ? -1.0 : 1.0, horizontal ? 1.0 : -1.0);
    image_ = image_.transformed(mirror);
    if (!undecorated_.isNull())
        undecorated_ = undecorated_.transformed(mirror);
    // The size is unchanged, so nothing has to be moved; the pixels under the window
    // are what changed.
    setZoom(zoom_);
}

void PinWindow::setTransparency(qreal transparency) {
    transparency_ = std::clamp(transparency, 0.0, 0.9);
    setWindowOpacity(1.0 - transparency_);
}

void PinWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QPoint(0, 0), display_);
    // A pin that has a shadow is already told apart from the desktop by the shadow,
    // and a line on top of that reads as a frame drawn around the picture rather than
    // as the picture's own edge — which is what was on screen before and looked wrong.
    // So the outline is only drawn when there is no shadow to do the job: a flat pin
    // on a desktop of the same colour would otherwise have no edge at all. The edge a
    // drag grabs is the picture's edge either way, which is what pictureRect() is for.
    if (decoration_ <= 0) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(accent(), kOutline));
        const QRect picture = pictureRect();
        painter.drawRect(picture.adjusted(0, 0, -1, -1));
    }
}

void PinWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint global = event->globalPosition().toPoint();
    dragStartGeometry_ = geometry();
    dragStartGlobal_ = global;
    draggingEdge_ = edgeAt(event->position().toPoint());
    if (draggingEdge_ == SideNone)
        offset_ = global - frameGeometry().topLeft();
    dragging_ = true;
    if (draggingEdge_ != SideNone)
        applyEdgeCursor(draggingEdge_);
    else
        setCursor(Qt::ClosedHandCursor);
}

void PinWindow::mouseMoveEvent(QMouseEvent *event) {
    const QPoint global = event->globalPosition().toPoint();
    if (!dragging_) {
        // The cursor is the whole hint: an edge that can be dragged has to say so
        // before the button goes down, not after.
        applyEdgeCursor(edgeAt(event->position().toPoint()));
        return;
    }
    if (draggingEdge_ != SideNone) {
        const QPoint shift = global - dragStartGlobal_;
        QRect wanted = dragStartGeometry_;
        if (draggingEdge_ & SideLeft)
            wanted.setLeft(wanted.left() + shift.x());
        if (draggingEdge_ & SideRight)
            wanted.setRight(wanted.right() + shift.x());
        if (draggingEdge_ & SideTop)
            wanted.setTop(wanted.top() + shift.y());
        if (draggingEdge_ & SideBottom)
            wanted.setBottom(wanted.bottom() + shift.y());
        resizeTo(wanted.size());
        move(wanted.topLeft());
        return;
    }
    move(global - offset_);
}

void PinWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = false;
    draggingEdge_ = SideNone;
    applyEdgeCursor(edgeAt(event->position().toPoint()));
}

QRect PinWindow::pictureRect() const {
    QRect area = rect();
    if (decoration_ <= 0 || image_.isNull() || image_.width() <= 0 || image_.height() <= 0)
        return area;
    // The picture was stretched to fill the window, halo included, so the margin the
    // halo takes up here is the same fraction of the window it is of the picture.
    const int across = qRound(decoration_ * double(area.width()) / image_.width());
    const int down = qRound(decoration_ * double(area.height()) / image_.height());
    // Never let the margin eat the picture: a pin whose halo is wider than it is tall
    // would otherwise be outlined around nothing at all.
    const int marginX = std::min(across, area.width() / 2 - 1);
    const int marginY = std::min(down, area.height() / 2 - 1);
    return area.adjusted(marginX, marginY, -marginX, -marginY);
}

int PinWindow::edgeAt(QPoint point) const {
    // The edge a drag grabs is the edge that is drawn, which is the picture's and not
    // the window's: a shadow is margin, and grabbing margin to resize a picture would
    // be grabbing a place the picture is not.
    const QRect picture = pictureRect();
    // A pin dragged down to nothing could never be grabbed again, so the strip that
    // counts as an edge never eats the whole picture.
    const int grab = std::min(kEdgeGrab, std::min(picture.width(), picture.height()) / 3);
    int edge = SideNone;
    if (point.x() <= picture.left() + grab)
        edge |= SideLeft;
    else if (point.x() >= picture.right() - grab)
        edge |= SideRight;
    if (point.y() <= picture.top() + grab)
        edge |= SideTop;
    else if (point.y() >= picture.bottom() - grab)
        edge |= SideBottom;
    return edge;
}

void PinWindow::applyEdgeCursor(int edge) {
    switch (edge) {
    case SideLeft | SideTop:
    case SideRight | SideBottom:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case SideRight | SideTop:
    case SideLeft | SideBottom:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case SideLeft:
    case SideRight:
        setCursor(Qt::SizeHorCursor);
        break;
    case SideTop:
    case SideBottom:
        setCursor(Qt::SizeVerCursor);
        break;
    default:
        setCursor(Qt::OpenHandCursor);
        break;
    }
}

void PinWindow::resizeTo(QSize size) {
    const QSize logical(std::max(kMinimumSide, size.width()), std::max(kMinimumSide, size.height()));
    const QSize base = baseLogical();
    if (base.isEmpty())
        return;
    // Dragging an edge is a change of zoom, so the wheel carries on from wherever the
    // drag left it. Which number is used is the one the edge that is being dragged
    // actually moves: pulling the bottom edge only changes the height.
    const bool sideways = (draggingEdge_ & (SideLeft | SideRight)) != 0;
    const double ratio = sideways ? double(logical.width()) / base.width()
                                  : double(logical.height()) / base.height();
    setZoom(ratio);
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    // A double click puts the pin away, the same as Escape does. It is the gesture a
    // window this size is closed by, and copying is already on Ctrl+C and on the menu.
    if (event->button() == Qt::LeftButton)
        close();
}

void PinWindow::wheelEvent(QWheelEvent *event) {
    // Every notch is a tenth of the size, which reaches both ends of the range
    // without needing many turns.
    setZoom(zoom_ * (event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1));
    event->accept();
}

void PinWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (event->key() == Qt::Key_0) {
        fitToScreen();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested(image_);
        return;
    }
    if (event->matches(QKeySequence::Save)) {
        emit saveRequested(image_);
        return;
    }
    QWidget::keyPressEvent(event);
}

QMenu *PinWindow::createContextMenu() {
    auto *menu = new QMenu(this);
    // 批注 is the reason the program exists, so it leads even here, where the picture
    // has already been taken off the screen.
    menu->addAction(tr("批注"), this, [this] { emit annotateRequested(editableImage()); });
    menu->addSeparator();
    menu->addAction(tr("复制图像"), this, [this] { emit copyRequested(image_); });
    menu->addAction(tr("保存图片"), this, [this] { emit saveRequested(image_); });
    menu->addAction(tr("文字识别"), this, [this] { emit ocrRequested(image_); });
    menu->addSeparator();
    auto *turn = menu->addMenu(tr("旋转"));
    turn->addAction(tr("向左旋转 90°"), this, [this] { rotate(-1); });
    turn->addAction(tr("向右旋转 90°"), this, [this] { rotate(1); });
    // Turning the picture over is what a reference shot of a diagram needs, and a pin
    // is where a reference shot lives.
    turn->addSeparator();
    turn->addAction(tr("水平翻转"), this, [this] { flip(true); });
    turn->addAction(tr("垂直翻转"), this, [this] { flip(false); });
    auto *transparency = menu->addMenu(tr("透明度"));
    for (const int percent : {0, 25, 50, 75, 90}) {
        auto *action = transparency->addAction(QStringLiteral("%1%").arg(100 - percent));
        action->setCheckable(true);
        action->setChecked(qRound(transparency_ * 100) == percent);
        connect(action, &QAction::triggered, this, [this, percent] { setTransparency(percent / 100.0); });
    }
    auto *shadow = menu->addAction(tr("阴影"));
    shadow->setCheckable(true);
    shadow->setChecked(style_.shadow);
    // A picture dropped on the screen out of nowhere has no shadow to turn off: only a
    // pin of a capture carries the pixels and the style it was taken with.
    shadow->setEnabled(canToggleShadow());
    connect(shadow, &QAction::triggered, this, [this](bool on) { setShadowEnabled(on); });
    menu->addSeparator();
    auto *zoom = menu->addMenu(tr("缩放"));
    zoom->addAction(tr("适应屏幕"), this, [this] { fitToScreen(); });
    zoom->addAction(tr("原始大小"), this, [this] { setZoom(1.0); });
    if (!placement_.isEmpty()) {
        // Only offered when there is somewhere to go back to; a pin that was centred
        // has no original position to speak of.
        menu->addAction(tr("回到截图位置"), this, [this] {
            setZoom(placedZoom_);
            move(placement_.topLeft());
        });
    }
    menu->addSeparator();
    menu->addAction(tr("关闭"), this, &QWidget::close);
    return menu;
}

void PinWindow::contextMenuEvent(QContextMenuEvent *event) {
    std::unique_ptr<QMenu> menu(createContextMenu());
    menu->exec(event->globalPos());
}

void PinWindow::closeEvent(QCloseEvent *event) {
    // The owner keeps the windows in a list and has to be told, whether the close
    // came from the menu, from Escape, or from the window manager.
    emit closed(this);
    QWidget::closeEvent(event);
}
} // namespace h2d
