#include "overlay.h"
#include "capturetoolbar.h"
#include "detector.h"
#include "i18n.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QShowEvent>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent>
#include <cmath>
namespace h2d {
namespace {
// A press inside a settled region moves the whole region instead of resizing it; this
// is what handle_ holds while that is what is going on, so the two kinds of drag can
// be told apart in one member.
constexpr int kMoving = -2;
constexpr double kRowHeight = 18;
// The four colour rows, the pixel the pointer is on, and the two lines that say what
// the keyboard can do. Each one is a slot the panel paints into.
enum Slot { kPixelSlot, kRgbSlot, kHexSlot, kHsvSlot, kHslSlot };
constexpr int kSlotCount = 5;
} // namespace

Overlay::Overlay(ScreenFrame frame, QWidget *parent)
    : QWidget(parent), frame_(frame), liveFrame_(std::move(frame)) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle(tr("EditHere · 选择截图区域"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setGeometry(frame_.logicalGeometry);
    // Where the pointer is to start with. The window is put over the screen the grab
    // came from, so the pointer is somewhere on it; a keypress before the first move of
    // the mouse then starts from there instead of from the corner.
    pointer_ = mapFromGlobal(QCursor::pos());
    setAttribute(Qt::WA_DeleteOnClose, false);
    hoverTimer_.setSingleShot(true);
    hoverTimer_.setInterval(250);
    connect(&hoverTimer_, &QTimer::timeout, this, [this] {
        if (!finished_ && isVisible()) { magnifierVisible_ = true; update(); }
    });
    debounce_.setSingleShot(true);
    debounce_.setInterval(150);
    connect(&debounce_, &QTimer::timeout, this, &Overlay::requestProbe);
    copyNote_.setSingleShot(true);
    copyNote_.setInterval(1100);
    connect(&copyNote_, &QTimer::timeout, this, [this] {
        colourCopied_ = false;
        update();
    });
    auto worker = new QFutureWatcher<QVector<Candidate>>(this);
    connect(worker, &QFutureWatcher<QVector<Candidate>>::finished, this, [this, worker] {
        visual_ = worker->result();
        auto full = manualTarget();
        full["label"] = QT_TRANSLATE_NOOP("EditHere", "整个屏幕");
        visual_.append({QRect(QPoint(0, 0), frame_.image.size()), full});
        if (selected_.isEmpty()) {
            picker_.update(candidates(), cursor_);
            update();
        }
        worker->deleteLater();
    });
    worker->setFuture(QtConcurrent::run([image = frame_.image] { return detectBlocks(image); }));
    // The bar and the column of tools are children of this window, so the keyboard
    // keeps reaching the window and neither can end up behind the screen it belongs to.
    bar_ = new CaptureToolbar(this);
    bar_->hide();
    connect(bar_, &CaptureToolbar::copyRequested, this, [this] { emit copyRequested(selected_); });
    connect(bar_, &CaptureToolbar::pinRequested, this, [this] { emit pinRequested(selected_); });
    connect(bar_, &CaptureToolbar::saveRequested, this, [this] { emit saveRequested(selected_); });
    connect(bar_, &CaptureToolbar::ocrRequested, this, [this] {
        ocrLanguage_ = bar_->ocrLanguage();
        emit ocrRequested(selected_, ocrLanguage_);
    });
    connect(bar_, &CaptureToolbar::scrollRequested, this, [this] { emit scrollRequested(selected_); });
    connect(bar_, &CaptureToolbar::scrollRunRequested, this, [this](bool running) {
        if (running)
            emit scrollRequested(selected_);
        else
            emit scrollStopRequested();
    });
    connect(bar_, &CaptureToolbar::scrollStopRequested, this, &Overlay::scrollStopRequested);
    connect(bar_, &CaptureToolbar::scrollAxisChanged, this, [this](Qt::Orientation axis) {
        if (scrollAxis_ == axis)
            return;
        scrollAxis_ = axis;
        // Half a picture stitched downwards and half sideways is not one picture, so
        // the run that was going is stopped before the new direction takes effect.
        scrollPicture_ = {};
        scrollFrames_ = 0;
        scrollPartial_ = false;
        scrollAxisChanged();
        update();
    });
    connect(bar_, &CaptureToolbar::annotateRequested, this,
            [this] { emit accepted(selected_, candidates()); });
    connect(bar_, &CaptureToolbar::dismissed, this, [this] { emit cancelled(); });
    connect(bar_, &CaptureToolbar::historyRequested, this, &Overlay::historyRequested);
    connect(bar_, &CaptureToolbar::historyStepRequested, this, &Overlay::historyStepRequested);
    connect(bar_, &CaptureToolbar::selectionRequested, this, [this](QRect area) {
        // A remembered rectangle is confined to this screen: the one it was made on may
        // have been a different size, and a region half off the screen is no use.
        applySelection(area, false);
    });
    connect(bar_, &CaptureToolbar::sizeRequested, this, [this](QSize size) {
        // The window decides what the typed numbers mean, because a locked ratio is
        // what turns two numbers into a shape.
        applySelection(captureRectWithSize(selected_, size, ratio_, bar_->customWidth(),
                                           bar_->customHeight()),
                       false);
    });
    connect(bar_, &CaptureToolbar::ratioChanged, this, [this] {
        ratio_ = bar_->ratio();
        applySelection(captureRectWithRatio(selected_, ratio_, bar_->customWidth(), bar_->customHeight()),
                       false);
    });
    sidebar_ = new CaptureSidebar(this);
    sidebar_->hide();
    // A style change alters what the region looks like, not where it is.
    connect(sidebar_, &CaptureSidebar::styleChanged, this, [this](const CaptureStyle &style) {
        style_ = style;
        update();
    });
    connect(sidebar_, &CaptureSidebar::rememberRequested, this, &Overlay::styleRememberRequested);
}

void Overlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (rect().contains(local)) {
        pointer_ = local;
        cursor_ = pixelPoint(local);
        magnifierVisible_ = true; update();
    }
}
void Overlay::leaveEvent(QEvent *event) {
    if (!drawing_ && !picking_) { hoverTimer_.stop(); magnifierVisible_ = false; update(); }
    QWidget::leaveEvent(event);
}
QPoint Overlay::pixelPoint(QPointF p) const {
    return {std::clamp(qRound(p.x() * frame_.image.width() / width()), 0, frame_.image.width()),
            std::clamp(qRound(p.y() * frame_.image.height() / height()), 0, frame_.image.height())};
}
QRectF Overlay::localRect(QRect r) const {
    return {double(r.x()) * width() / frame_.image.width(), double(r.y()) * height() / frame_.image.height(),
            double(r.width()) * width() / frame_.image.width(),
            double(r.height()) * height() / frame_.image.height()};
}
QVector<Candidate> Overlay::candidates() const {
    const Candidate *front=nullptr;
    for(const auto &window:frame_.frontWindows)
        if(window.bounds.contains(cursor_)) { front=&window; break; }
    if(frame_.windowScopeAvailable) {
        if(!front) { auto target=manualTarget(); target["label"]=QT_TRANSLATE_NOOP("EditHere", "整个屏幕"); return {{frame_.image.rect(),target}}; }
        QVector<Candidate> result;
        for(const auto &candidate:native_)
            if(front->bounds.contains(candidate.bounds)) result.append(candidate);
        for(const auto &candidate:visual_)
            if(front->bounds.contains(candidate.bounds) && candidate.bounds!=front->bounds) result.append(candidate);
        result.append(*front);
        return result;
    }
    auto result = native_;
    result += visual_;
    return result;
}
void Overlay::resetSelection() {
    // A long capture belongs to the region it was taken of, so throwing the region away
    // ends the run as well. This is said before anything else, because the owner keeps
    // the frames that were stitched and has to stop adding to them.
    if (scrolling_)
        emit scrollStopRequested();
    selected_ = {};
    drawing_ = false;
    ready_ = false;
    picking_ = false;
    handle_ = -1;
    keyboardOffset_ = {};
    setCursor(Qt::CrossCursor);
    // A fresh start forgets the level the wheel had asked for and the block that was
    // being kept: wanting the row rather than the cell is a wish about this look, not
    // a setting for the next one.
    picker_.forget();
    hideTools();
    update();
}
const QImage &Overlay::scaledFrame() {
    if (frameScaled_.size() != frame_.image.size()) {
        // The grab is in device pixels while the window is laid out in logical ones.
        // Shrinking it to the window size throws away exactly the pixels the user is
        // trying to tell apart and then has them interpolated back, which reads as a
        // blurry screen (REG-072, and the same trap a second time here). Declaring the
        // ratio instead maps one image pixel to one screen pixel.
        frameScaled_ = frame_.image;
        if (!frame_.image.isNull() && width() > 0 && frame_.image.width() != width())
            frameScaled_.setDevicePixelRatio(double(frame_.image.width()) / width());
    }
    return frameScaled_;
}

void Overlay::drawScene(QPainter &p, const QRectF &viewport) {
    // The screen, the film that dims it and the frame around the region are one
    // picture, and the enlargement has to be taken of that picture rather than of the
    // grab underneath it. So the picture is painted here, and whoever wants a piece of
    // it names the piece: the window asks for all of it, the magnifier for the few
    // pixels under the pointer.
    const double sx = double(frame_.image.width()) / width();
    const double sy = double(frame_.image.height()) / height();
    const QRectF source(viewport.left() * sx, viewport.top() * sy, viewport.width() * sx,
                        viewport.height() * sy);
    p.drawImage(viewport, scaledFrame(), source);
    QRect active = selected_;
    if (!drawing_ && active.isEmpty() && picker_.current())
        active = picker_.current()->bounds;
    // While a colour is being picked the screen is shown at full brightness, because
    // a colour judged through a grey film is the wrong colour.
    QPainterPath mask;
    mask.addRect(rect());
    if (!picking_) {
        if (!active.isEmpty())
            mask.addRect(localRect(active));
        mask.setFillRule(Qt::OddEvenFill);
        p.fillPath(mask, QColor(16, 18, 24, 105));
    }
    if (active.isEmpty() || picking_)
        return;
    QRectF outline = localRect(active);
    const double scale = double(width()) / frame_.image.width();
    const double radius = style_.cornerRadius * scale;
    p.setBrush(Qt::NoBrush);
    // The border the region will carry is drawn where it will land, so the choice
    // in the panel can be judged before anything is copied.
    if (style_.border) {
        p.setPen(QPen(style_.borderColor, std::max(1.0, style_.borderWidth * scale)));
        p.drawRoundedRect(outline.adjusted(1, 1, -1, -1), std::max(0.0, radius - 1),
                          std::max(0.0, radius - 1));
    }
    p.setPen(QPen(accent(), 2));
    p.drawRoundedRect(outline, radius, radius);
    // The corners are where the region is resized, and a region being resized in the
    // middle of a long capture would change what the frames are read from without the
    // stitched picture knowing. They are therefore not offered while the run is going;
    // the region can still be dragged along the run to reach the last of the content.
    if (ready_ && !scrollRunning_) {
        // The corners are where the region can be resized, so they are shown, and
        // they are round because a round handle reads as a handle rather than as a
        // speck of dirt on the picture.
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(accent());
        for (const auto &corner : {active.topLeft(), active.topRight(), active.bottomLeft(),
                                   active.bottomRight()}) {
            const QPointF centre = QPointF(double(corner.x()) * width() / frame_.image.width(),
                                           double(corner.y()) * height() / frame_.image.height());
            p.drawEllipse(centre, 4.0, 4.0);
        }
    }
}

void Overlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawScene(p, rect());
    if (scrolling_)
        drawScrollPreview(p);
    // The magnifier and the colour readout are up while a region is being found, and
    // again once it has settled whenever the pointer is inside it: pointing at the
    // picture that was just taken is exactly when a colour is wanted, so reading one
    // should not cost a keystroke. C still turns it into a mode that copies on click.
    const bool overSettledRegion =
        ready_ && !selected_.isEmpty() && selected_.contains(cursor_);
    if (!finished_ && !scrolling_ && (!ready_ || picking_ || overSettledRegion)) {
        QPointF at(double(cursor_.x()) * width() / frame_.image.width(),
                   double(cursor_.y()) * height() / frame_.image.height());
        for (const auto &pen : {QPen(Qt::white, 0)}) {
            p.setPen(pen);
            p.drawLine(at + QPointF(-9,0), at + QPointF(9,0));
            p.drawLine(at + QPointF(0,-9), at + QPointF(0,9));
        }
        drawMagnifier(p, at);
    }
    if (picking_) {
        // The panel carries the instructions, so the only thing left to say up here is
        // how to get out.
        const QString text = colourCopied_ ? tr("已复制颜色值 %1")
                                                 .arg(captureColourText(colourAt(cursor_), colourFormat_))
                                           : tr("移动到要取色的位置 · C 复制颜色值 · Shift 切换颜色格式 · Esc 结束取色");
        p.setFont(QFont("Microsoft YaHei", 10));
        const int w = std::min(width() - 32, p.fontMetrics().horizontalAdvance(text) + 32);
        const QRectF hint((width() - w) / 2, 24, w, 36);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 31, 37, 235));
        p.drawRoundedRect(hint, 10, 10);
        p.setPen(Qt::white);
        p.drawText(hint, Qt::AlignCenter, text);
    } else if (scrolling_) {
        // In long-capture mode the bar carries the state, so the only thing said up here
        // is what the region itself is for now: a window onto a page that is being
        // scrolled or dragged underneath it.
        const QString text =
            scrollRunning_
                ? (scrollAxis_ == Qt::Horizontal
                       ? tr("横向长截图 · 拖动选区扫过要截的内容 · 再按一次长截图停止")
                       : tr("纵向长截图 · 滚轮滚动要截的内容 · 拖动选区可补最后一屏 · 再按一次长截图停止"))
                : tr("长截图已停止 · 按「开始」继续，或按「完成」收下这张图");
        p.setFont(QFont("Microsoft YaHei", 10));
        const int w = std::min(width() - 32, p.fontMetrics().horizontalAdvance(text) + 32);
        const QRectF hint(bar_ != nullptr && bar_->y() < height() / 2 ? QRectF((width() - w) / 2, height() - 60, w, 36)
                                                                     : QRectF((width() - w) / 2, 24, w, 36));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 31, 37, 235));
        p.drawRoundedRect(hint, 10, 10);
        p.setPen(Qt::white);
        p.drawText(hint, Qt::AlignCenter, text);
    } else if (ready_ && !selected_.isEmpty()) {
        const QString text = tr("拖动选区可移动 · 角点可缩放 · 双击或 Ctrl+C 复制 · 回车批注 · C 取色 · Esc 取消");
        p.setFont(QFont("Microsoft YaHei", 10));
        const int w = std::min(width() - 32, p.fontMetrics().horizontalAdvance(text) + 32);
        // The bar sits above the region, so the hint takes the other side when there
        // is room for it.
        const QRectF hint(bar_ != nullptr && bar_->y() < height() / 2 ? QRectF((width() - w) / 2, height() - 60, w, 36)
                                                                     : QRectF((width() - w) / 2, 24, w, 36));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 31, 37, 225));
        p.drawRoundedRect(hint, 10, 10);
        p.setPen(Qt::white);
        p.drawText(hint, Qt::AlignCenter, text);
    } else if (selected_.isEmpty() && !drawing_) {
        QString text = picker_.current() ? tr("%1  ·  %2 / %3  ·  滚轮 ↑ 更大 ↓ 更小")
                                               .arg(localizedLabel(picker_.current()->target["label"].toString()).left(30))
                                               .arg(picker_.level())
                                               .arg(picker_.count())
                                         : tr("拖动截图 · 单击选块 · 松手后可批注、贴图或取色 · Esc 取消");
        p.setFont(QFont("Microsoft YaHei", 10));
        int w = std::min(width() - 32, p.fontMetrics().horizontalAdvance(text) + 32);
        QRectF hint((width() - w) / 2, 24, w, 36);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 31, 37, 225));
        p.drawRoundedRect(hint, 10, 10);
        p.setPen(Qt::white);
        p.drawText(hint, Qt::AlignCenter, text);
    }
}

void Overlay::drawMagnifier(QPainter &p, QPointF at) {
    const QSizeF panelSize(magnifierPanelWidth, magnifierPanelHeight);
    // The bar and the column of tools are children of this window, so they are painted
    // over the panel; the panel is placed to keep clear of them.
    QVector<QRectF> tools;
    if (bar_ != nullptr && bar_->isVisible())
        tools.append(bar_->geometry());
    if (sidebar_ != nullptr && sidebar_->isVisible())
        tools.append(sidebar_->geometry());
    const QRectF panel = magnifierPlacement(at, QSizeF(width(), height()), tools);
    const double x = panel.left(), y = panel.top();
    const double inner = panelSize.width() - magnifierPanelPadding * 2;
    const QRectF area(QPointF(x + magnifierPanelPadding, y + magnifierPanelPadding),
                      QSizeF(inner, magnifierZoomHeight));
    p.setPen(QPen(QColor(255,255,255,90),1));
    p.setBrush(QColor(25,28,34,245));
    p.drawRoundedRect(panel,10,10);
    const QPoint sample(std::clamp(cursor_.x(),0,frame_.image.width()-1),
                        std::clamp(cursor_.y(),0,frame_.image.height()-1));
    // The panel follows the pointer: the pixel shown is always the one under the
    // cursor, so aiming and reading are the same gesture.
    const QColor colour = colourAt(sample);
    p.save();
    p.setClipRect(area);
    p.fillRect(area,QColor(45,48,55));
    p.setRenderHint(QPainter::SmoothPixmapTransform,false);
    const double cell=magnifierZoomCell;
    const QPointF center=area.center();
    const double sx = double(frame_.image.width()) / width();
    const double sy = double(frame_.image.height()) / height();
    const double factor=cell*sx;
    // Only the strip of the screen the panel shows is handed to the painter. Giving it
    // the whole grab under a scale made it resample the entire screen a second time on
    // every repaint, which on a scaled display is ten milliseconds a frame.
    const QRectF source(sample.x() + 0.5 - (center.x() - area.left()) * sx / factor,
                        sample.y() + 0.5 - (center.y() - area.top()) * sy / factor,
                        area.width() * sx / factor, area.height() * sy / factor);
    // The same strip, as a piece of the window. The enlargement is a glass held over
    // the window, not a second look at the grab: the dimming and the blue frame are
    // already on the glass, so they come out enlarged with the pixels they belong to
    // instead of being redrawn afterwards at the size they have on the screen.
    const QRectF window(source.left() / sx, source.top() / sy, source.width() / sx,
                        source.height() / sy);
    const double zoomX = area.width() / window.width();
    const double zoomY = area.height() / window.height();
    const double ratio = std::max(1.0, devicePixelRatioF());
    QImage tile(QSize(int(std::ceil(area.width() * ratio)), int(std::ceil(area.height() * ratio))),
                QImage::Format_ARGB32_Premultiplied);
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter enlarged(&tile);
    enlarged.setRenderHint(QPainter::Antialiasing);
    // A magnified pixel has to stay a square block of one colour; smoothing would
    // invent colours that are not on the screen.
    enlarged.setRenderHint(QPainter::SmoothPixmapTransform, false);
    enlarged.scale(ratio, ratio);
    enlarged.translate(-window.left() * zoomX, -window.top() * zoomY);
    enlarged.scale(zoomX, zoomY);
    drawScene(enlarged, window);
    enlarged.end();
    p.drawImage(area, tile);
    p.restore();
    p.save();
    p.setClipRect(area);
    p.setRenderHint(QPainter::Antialiasing,false);
    p.setPen(QPen(QColor(255,255,255,70),0));
    for(double gx=center.x()-cell/2; gx>=area.left(); gx-=cell) p.drawLine(QPointF(gx,area.top()),QPointF(gx,area.bottom()));
    for(double gx=center.x()+cell/2; gx<=area.right(); gx+=cell) p.drawLine(QPointF(gx,area.top()),QPointF(gx,area.bottom()));
    for(double gy=center.y()-cell/2; gy>=area.top(); gy-=cell) p.drawLine(QPointF(area.left(),gy),QPointF(area.right(),gy));
    for(double gy=center.y()+cell/2; gy<=area.bottom(); gy+=cell) p.drawLine(QPointF(area.left(),gy),QPointF(area.right(),gy));
    // The one pixel the readout is about is boxed, not left to the grid to imply. The
    // box is one enlarged pixel wide, which is what the grid counts in.
    p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white,0));
    p.drawRect(QRectF(center-QPointF(cell/2,cell/2),QSizeF(cell,cell)));
    p.restore();
    const double firstRow = y + magnifierPanelPadding + magnifierZoomHeight + 4;
    const auto slotRect = [&](int slot) {
        return QRectF(x + magnifierPanelPadding, firstRow + slot * kRowHeight, inner, kRowHeight);
    };
    p.setFont(QFont("Microsoft YaHei", 9));
    // The pixel the colour came from, written the way a colour tool writes it.
    p.setPen(QColor(0xc9, 0xcb, 0xd4));
    p.drawText(slotRect(kPixelSlot), Qt::AlignVCenter,
               tr("像素 ( %1 , %2 )").arg(sample.x()).arg(sample.y()));
    // All four ways of writing the colour are on show and the one that a copy would
    // put on the clipboard is the bright one, which is what makes Shift discoverable.
    const QVector<ColourFormat> formats{ColourFormat::Rgb, ColourFormat::Hex, ColourFormat::Hsv,
                                        ColourFormat::Hsl};
    for (int index = 0; index < formats.size(); ++index) {
        const ColourFormat format = formats.at(index);
        const bool current = format == colourFormat_;
        const QRectF row = slotRect(kRgbSlot + index);
        if (current) {
            // A swatch of the colour itself sits in the row that is being copied.
            const QRectF swatch(row.left(), row.top() + 3, 12, 12);
            p.setBrush(colour);
            p.setPen(QPen(QColor(255, 255, 255, 140), 1));
            p.drawRect(swatch);
        }
        p.setPen(current ? QColor(0xf2, 0xf2, 0xf5) : QColor(0x8d, 0x90, 0x9c));
        p.drawText(row.adjusted(18, 0, 0, 0), Qt::AlignVCenter,
                   captureColourText(colour, format));
    }
    p.setPen(QColor(0x8d, 0x90, 0x9c));
    const QRectF note(x + magnifierPanelPadding, firstRow + kSlotCount * kRowHeight + 2, inner, 16);
    p.drawText(note, Qt::AlignVCenter, tr("C 复制颜色值 · Shift 切换格式"));
}

QRect Overlay::scrollPreviewRect() const {
    if (!scrolling_ || selected_.isEmpty())
        return {};
    QVector<QRectF> tools;
    if (bar_ != nullptr && bar_->isVisible())
        tools.append(bar_->geometry());
    if (sidebar_ != nullptr && sidebar_->isVisible())
        tools.append(sidebar_->geometry());
    const QRectF placed =
        scrollPreviewPlacement(localRect(selected_), QSizeF(width(), height()), scrollAxis_, tools);
    return placed.toRect();
}

void Overlay::drawScrollPreview(QPainter &p) {
    const QRect panel = scrollPreviewRect();
    // Nowhere to put it is better than putting it over the region it is a preview of:
    // the region is what the user is reading the page through.
    if (panel.isEmpty() || scrollPicture_.isNull())
        return;
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.setBrush(QColor(25, 28, 34, 245));
    p.drawRoundedRect(QRectF(panel).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    const QRectF inner = QRectF(panel).adjusted(scrollPreviewPadding, scrollPreviewPadding,
                                                -scrollPreviewPadding, -scrollPreviewPadding);
    // The picture is scaled to fit the panel rather than the panel being resized to the
    // picture, which for a capture many screens long would otherwise run off the display.
    const bool sideways = scrollAxis_ == Qt::Horizontal;
    const int pictureLength = sideways ? scrollPicture_.width() : scrollPicture_.height();
    const int pictureCross = sideways ? scrollPicture_.height() : scrollPicture_.width();
    if (pictureLength <= 0 || pictureCross <= 0)
        return;
    // The length is scaled to fill the panel; the cross axis follows at the same scale,
    // so what is shown is the picture's shape rather than a stretched one. The panel is
    // much longer than it is wide, so the length is what usually decides the fit.
    const double fit = std::min(sideways ? inner.width() / pictureLength : inner.height() / pictureLength,
                                sideways ? inner.height() / pictureCross : inner.width() / pictureCross);
    // The new end is the one worth seeing, so the shown window sits at the end of the
    // picture that is growing; a preview pinned to the start would show the oldest part
    // of the page while the run was still going.
    const int shownLength =
        std::min(pictureLength, int(std::floor((sideways ? inner.width() : inner.height()) / fit)));
    const QRect source = sideways
                             ? QRect(0, 0, shownLength, pictureCross)
                             : QRect(0, pictureLength - shownLength, pictureCross, shownLength);
    const QRectF frame = sideways
                             ? QRectF(inner.right() - shownLength * fit,
                                      inner.top() + (inner.height() - pictureCross * fit) / 2,
                                      shownLength * fit, pictureCross * fit)
                             : QRectF(inner.left() + (inner.width() - pictureCross * fit) / 2,
                                      inner.bottom() - shownLength * fit, pictureCross * fit,
                                      shownLength * fit);
    p.save();
    p.setClipRect(inner);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // The parts of the page that are still to come are dimmed, so the bright band in the
    // panel is where the region is looking right now.
    p.setBrush(QColor(255, 255, 255, 26));
    p.setPen(Qt::NoPen);
    p.drawRect(inner);
    p.drawImage(frame, scrollPicture_, source);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.restore();
    // The window the region is looking through, as a share of the whole picture: the
    // panel is how the user sees how much of the page is left above the region.
    const int windowLength = sideways ? selected_.width() : selected_.height();
    if (windowLength > 0 && pictureLength >= windowLength) {
        const double band = windowLength * fit;
        const QRectF here =
            sideways ? QRectF(frame.right() - band, frame.top(), band, frame.height())
                     : QRectF(frame.left(), frame.bottom() - band, frame.width(), band);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent(), 1.5));
        p.drawRect(here);
    }
    p.setFont(QFont("Microsoft YaHei", 9));
    p.setPen(QColor(0x8d, 0x90, 0x9c));
    p.drawText(QRect(panel.left(), panel.bottom() + 2, panel.width(), 16), Qt::AlignHCenter,
               tr("%1 帧").arg(scrollFrames_));
}

void Overlay::mousePressEvent(QMouseEvent *e) {
    if (busy_)
        return;
    if (e->button() == Qt::RightButton) {
        if (picking_) {
            leavePicking();
            return;
        }
        // A right click in long-capture mode stops the run and gives the region back its
        // ordinary tools, which is the same thing the bar's 退出长截图 does. Throwing the
        // region away instead would lose the frames that were just stitched.
        if (scrolling_) {
            emit scrollStopRequested();
            return;
        }
        if (selected_.isEmpty())
            emit cancelled();
        else
            resetSelection();
        return;
    }
    if (e->button() != Qt::LeftButton || finished_)
        return;
    const QPoint pixel = pixelPoint(e->position());
    pointer_ = e->position().toPoint();
    if (picking_) {
        // The colour under the pointer is what the window is for at this moment, and
        // the region is left exactly where it was.
        cursor_ = pixel;
        copyColour();
        leavePicking();
        return;
    }
    if (ready_) {
        // A settled region is moved from the inside, resized from a corner, and
        // replaced by a drag that starts anywhere else.
        handle_ = handleAt(pixel);
        if (handle_ >= 0) {
            dragOrigin_ = selected_;
            dragStart_ = pixel;
            setCursor(handle_ == 0 || handle_ == 3 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            return;
        }
        if (selected_.contains(pixel)) {
            handle_ = kMoving;
            dragOrigin_ = selected_;
            dragStart_ = pixel;
            // A region being dragged sideways during a long capture is how the last of
            // the content is reached, so the cursor says that rather than that the whole
            // region is free to go anywhere.
            setCursor(scrolling_ ? (scrollAxis_ == Qt::Horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor)
                                 : Qt::SizeAllCursor);
            return;
        }
        ready_ = false;
        hideTools();
    }
    emit selectionBegan();
    start_ = cursor_ = pixel;
    keyboardOffset_ = {};
    hoverTimer_.stop();
    magnifierVisible_ = true;
    nudged_ = false;
    setCursor(Qt::BlankCursor);
    setFocus();
    picker_.update(candidates(), start_);
    drawing_ = true;
    selected_ = {};
    update();
}
void Overlay::mouseMoveEvent(QMouseEvent *e) {
    if (busy_)
        return;
    const QPointF local = e->position();
    // A request to put the pointer somewhere is answered with the same place rounded
    // off to the pixels the system can address, and on a scaled display those are finer
    // than a whole pixel of the pointer. Taking that answer as the truth dragged the aim
    // back by the rounding on every press — which is what made an arrow key sometimes do
    // nothing — and the two axes were rounded independently, which is what made one key
    // move the pointer in two directions at once. Anything less than a whole pixel away
    // is that answer rather than the user, and is left alone. A drag is always the user.
    const bool dragging = drawing_ || handle_ != -1;
    const bool adopted = dragging || std::abs(local.x() - pointer_.x()) >= 1.0 ||
                         std::abs(local.y() - pointer_.y()) >= 1.0;
    if (adopted)
        pointer_ = local.toPoint();
    // Everything below is work done on the assumption that the user moved the mouse,
    // and this is not the user. Throwing the block away and looking for it again here
    // dropped the layer the wheel had picked and handed back the smallest block under
    // the pointer, which is why stepping the pointer with an arrow key put the little
    // block straight back.
    if (!adopted)
        return;
    const QPoint pixel = pixelPoint(local);
    if (handle_ == kMoving) {
        const QPoint shift = pixel - dragStart_;
        if (shift != QPoint()) {
            if (scrolling_) {
                // While a long capture is running the region is a window onto the page
                // and can only be moved along the run: a region dragged sideways during
                // a vertical capture would read a frame from a different column of the
                // page, which cannot be stitched onto the one before it. Moving along
                // the run is how the last of the content is reached once the page has
                // stopped scrolling.
                QRect moved = dragOrigin_;
                if (scrollAxis_ == Qt::Horizontal)
                    moved.translate(shift.x(), 0);
                else
                    moved.translate(0, shift.y());
                moved = moved.intersected(QRect(QPoint(0, 0), frame_.image.size()));
                if (!moved.isEmpty() && moved != dragOrigin_) {
                    dragOrigin_ = moved;
                    selected_ = moved;
                    // The bar and the preview panel are placed against the region, so
                    // they travel with it rather than being left pointing at where it
                    // used to be.
                    showBar();
                    // Dragging the region is what moves where the next frame is read
                    // from; a page that has stopped scrolling is not going to move again,
                    // so the rows already placed are left as they are and the new frames
                    // come from where the region has been put.
                    emit scrollRegionMoved(selected_);
                    update();
                }
                dragStart_ = pixel;
                return;
            }
            applySelection(dragOrigin_.translated(shift), true);
            dragStart_ = pixel;
        }
        return;
    }
    if (handle_ >= 0) {
        // The opposite corner is what stays put while a corner is dragged.
        QRect resized = dragOrigin_;
        if (handle_ == 0 || handle_ == 2)
            resized.setLeft(pixel.x());
        else
            resized.setRight(pixel.x());
        if (handle_ == 0 || handle_ == 1)
            resized.setTop(pixel.y());
        else
            resized.setBottom(pixel.y());
        applySelection(resized.normalized(), false);
        return;
    }
    if (!drawing_) { native_.clear(); picker_.reset(); }
    if (adopted)
        cursor_ = pixel + (drawing_ ? keyboardOffset_ : QPoint());
    cursor_.setX(std::clamp(cursor_.x(), 0, frame_.image.width()));
    cursor_.setY(std::clamp(cursor_.y(), 0, frame_.image.height()));
    magnifierVisible_ = drawing_ || picking_ || !ready_ ||
                        (ready_ && !selected_.isEmpty() && selected_.contains(cursor_));
    if (drawing_)
        selected_ = captureRectFromDrag(start_, cursor_, ratio_, bar_->customWidth(), bar_->customHeight());
    else if (!ready_ && selected_.isEmpty()) {
        picker_.update(candidates(), cursor_);
        debounce_.start();
    }
    update();
}
void Overlay::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (handle_ != -1) {
        handle_ = -1;
        setCursor(Qt::CrossCursor);
        return;
    }
    if (!drawing_)
        return;
    drawing_ = false;
    setCursor(Qt::CrossCursor);
    pointer_ = e->position().toPoint();
    QPoint end = pixelPoint(e->position()) + keyboardOffset_;
    end.setX(std::clamp(end.x(),0,frame_.image.width()));
    end.setY(std::clamp(end.y(),0,frame_.image.height()));
    if (!nudged_ && QLineF(QPointF(start_), QPointF(end)).length() * width() / frame_.image.width() < 5 &&
        picker_.current())
        selected_ = picker_.current()->bounds;
    else
        selected_ = captureRectFromDrag(start_, end, ratio_, bar_->customWidth(), bar_->customHeight());
    magnifierVisible_ = false;
    // The region is settled from here on: the pointer moves and resizes it, and the
    // bar and the tool column are what act on it.
    applySelection(selected_, false);
}
void Overlay::mouseDoubleClickEvent(QMouseEvent *e) {
    if (busy_ || e->button() != Qt::LeftButton)
        return;
    // A double click on the region copies it, which is the gesture a capture tool
    // teaches before anything else. It is not offered in long-capture mode: what is
    // under the region is one frame of a page, and copying it would be copying that
    // frame rather than the long picture the run is building.
    if (ready_ && !selected_.isEmpty() && !scrolling_) {
        emit copyRequested(selected_);
        e->accept();
    }
}
void Overlay::wheelEvent(QWheelEvent *e) {
    if (drawing_ || finished_ || ready_ || busy_ || !selected_.isEmpty() || scrolling_)
        return;
    pointer_ = e->position().toPoint();
    cursor_ = pixelPoint(e->position());
    picker_.update(candidates(), cursor_);
    if (e->angleDelta().y() != 0)
        picker_.step(e->angleDelta().y() > 0 ? 1 : -1);
    update();
    e->accept();
}
void Overlay::keyPressEvent(QKeyEvent *e) {
    if (busy_) {
        e->ignore();
        return;
    }
    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    // The arrows aim the pointer. That is what they do in every state except a drag,
    // where they are already stretching the region that is being drawn.
    const bool arrow = e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
                       e->key() == Qt::Key_Up || e->key() == Qt::Key_Down;
    const int stepX = e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0;
    const int stepY = e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0;
    const auto aim = [&] {
        movePointer(stepX, stepY);
        e->accept();
    };
    if (picking_) {
        if (e->key() == Qt::Key_Escape) {
            leavePicking();
            e->accept();
            return;
        }
        // Shift is the one key that changes what the panel says rather than what it
        // does, so it is answered first.
        if (e->key() == Qt::Key_Shift) {
            colourFormat_ = nextColourFormat(colourFormat_);
            update();
            e->accept();
            return;
        }
        // C and Ctrl+C are the same request here: the colour, not the picture.
        if (e->key() == Qt::Key_C) {
            copyColour();
            e->accept();
            return;
        }
        // Reading a colour is aiming at a pixel, so the arrows keep working here too.
        if (arrow)
            return aim();
        e->ignore();
        return;
    }
    // The three keys that reach earlier work are handled before the settled region
    // does, because a remembered rectangle is worth bringing back even before a new
    // one is drawn.
    if (e->key() == Qt::Key_H) {
        if (bar_ != nullptr)
            bar_->showMenu();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_R) {
        restoreLastSelection();
        e->accept();
        return;
    }
    if (ready_ && !selected_.isEmpty()) {
        if (e->key() == Qt::Key_Escape && scrolling_) {
            // Leaving the run is one thing and leaving the capture is another, so the
            // first Escape gives the region back and the second cancels the capture.
            emit scrollStopRequested();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_L) {
            // The same button as the bar's, and the same rule: once to start, once to
            // stop. Everything else is what acts on a still picture, and a long capture
            // is not a still picture.
            if (scrolling_)
                emit scrollStopRequested();
            else
                emit scrollRequested(selected_);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Less || e->key() == Qt::Key_Comma) {
            emit historyStepRequested(-1);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Greater || e->key() == Qt::Key_Period) {
            emit historyStepRequested(1);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Up ||
            e->key() == Qt::Key_Down) {
            // The region is where the user put it, so a plain arrow moves the pointer
            // instead: the enlargement is what the arrows are for once there is a
            // picture to aim at, and a region that slides under them cannot be placed
            // to the pixel. Shift and Ctrl keep pulling and pushing the edge they point
            // at, which is the one thing worth doing to the region itself.
            if (shift)
                stretchSelection(Qt::Key(e->key()), -1);
            else if (ctrl)
                stretchSelection(Qt::Key(e->key()), 1);
            else
                movePointer(stepX, stepY);
            e->accept();
            return;
        }
        // Everything below acts on a still picture, and none of it can be about a region
        // that is a window onto a page being scrolled: the picture those actions would
        // take is one frame of a run, not the run itself.
        if (scrolling_)
            return;
        if (e->matches(QKeySequence::Copy)) {
            emit copyRequested(selected_);
            e->accept();
            return;
        }
        if (e->matches(QKeySequence::Save) || e->key() == Qt::Key_S) {
            emit saveRequested(selected_);
            e->accept();
            return;
        }
        // Ctrl+T and Ctrl+2 are what a capture tool uses for "贴到屏幕上"; P is kept
        // because it is the letter of the word.
        if ((ctrl && (e->key() == Qt::Key_T || e->key() == Qt::Key_2)) || e->key() == Qt::Key_P) {
            emit pinRequested(selected_);
            e->accept();
            return;
        }
        if ((shift && e->key() == Qt::Key_C) || e->key() == Qt::Key_T) {
            emit ocrRequested(selected_, ocrLanguage_);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_C) {
            startPicking();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            emit accepted(selected_, candidates());
            e->accept();
            return;
        }
    }
    if (arrow && selected_.isEmpty() && !drawing_) {
        // Nothing has been taken and nothing is being drawn: the pointer is looking for
        // a block, and the arrows are the only way to put it on one pixel on purpose.
        // The block being offered and the enlargement both follow it, exactly as they
        // do when the mouse moves there.
        movePointer(stepX, stepY);
        // The arrows are how a block already settled is fine-tuned, so the block on
        // offer is kept while the pointer is still inside it. A mouse that travels is
        // not fine-tuning: there the block follows the pointer, which is the whole
        // point of finding blocks by hovering.
        picker_.update(candidates(), cursor_, true);
        debounce_.start();
        update();
        e->accept();
        return;
    }
    if (drawing_ && arrow) {
        QPoint next = cursor_ + QPoint(stepX, stepY);
        next.setX(std::clamp(next.x(),0,frame_.image.width()));
        next.setY(std::clamp(next.y(),0,frame_.image.height()));
        keyboardOffset_ += next-cursor_;
        cursor_=next;
        nudged_=true;
        selected_=captureRectFromDrag(start_,cursor_,ratio_,bar_->customWidth(),bar_->customHeight());
        update();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        emit cancelled();
        e->accept();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        finish(false);
    else if (e->matches(QKeySequence::Copy))
        finish(true);
    else
        QWidget::keyPressEvent(e);
}
void Overlay::finish(bool copy) {
    QRect area = selected_.isEmpty() ? (picker_.current() ? picker_.current()->bounds : QRect()) : selected_;
    if (area.isEmpty() || finished_)
        return;
    finished_ = true;
    hoverTimer_.stop();
    magnifierVisible_ = false;
    debounce_.stop();
    hideTools();
    if (probe_)
        probe_->kill();
    if (copy)
        emit copyRequested(area);
    else
        emit accepted(area, candidates());
}

QImage Overlay::selectionPixels() const {
    if (selected_.isEmpty())
        return {};
    const QRect area = selected_.intersected(QRect(QPoint(0, 0), frame_.image.size()));
    if (area.isEmpty())
        return {};
    return frame_.image.copy(area);
}

QImage Overlay::selectionImage() const {
    const QImage region = selectionPixels();
    if (region.isNull())
        return {};
    return composeCapture(region, style_);
}

QRect Overlay::placementFor(QSize picture) const {
    if (selected_.isEmpty() || picture.isEmpty() || frame_.image.isNull() ||
        frame_.logicalGeometry.isEmpty() || frame_.image.width() <= 0)
        return {};
    // The picture is in the pixels of the screen, the pin has to be placed in the
    // units the window manager uses, and on a scaled display those are not the same.
    const double scale = double(frame_.image.width()) / frame_.logicalGeometry.width();
    if (scale <= 0.0)
        return {};
    const QSize logical(std::max(1, int(std::lround(picture.width() / scale))),
                        std::max(1, int(std::lround(picture.height() / scale))));
    // A shadow reaches past the region by the same amount on every side, so the middle
    // is the one point the picture and the region agree on.
    const QPoint centre(
        frame_.logicalGeometry.x() + qRound((double(selected_.x()) + selected_.width() / 2.0) / scale),
        frame_.logicalGeometry.y() + qRound((double(selected_.y()) + selected_.height() / 2.0) / scale));
    return {QPoint(centre.x() - logical.width() / 2, centre.y() - logical.height() / 2), logical};
}

ScrollTarget Overlay::scrollTarget() const {
    ScrollTarget target;
    if (selected_.isEmpty() || frame_.nativeGeometry.isEmpty() || frame_.image.isNull())
        return target;
    target.screenName = frame_.name;
    target.pixels = selected_;
    // The wheel is aimed at the middle of the region: that is where a page reacts to
    // it, wherever its scrollbar happens to be.
    const QPoint middle = selected_.center();
    target.nativePoint =
        QPoint(frame_.nativeGeometry.x() + qRound(double(middle.x()) * frame_.nativeGeometry.width() /
                                                  frame_.image.width()),
               frame_.nativeGeometry.y() + qRound(double(middle.y()) * frame_.nativeGeometry.height() /
                                                  frame_.image.height()));
    return target;
}

void Overlay::setBusy(bool busy, const QString &message) {
    busy_ = busy;
    if (bar_ != nullptr) {
        bar_->setBusy(busy, message);
        if (busy)
            showBar();
    }
    if (sidebar_ != nullptr)
        sidebar_->setBusy(busy);
    update();
}

void Overlay::setOcrLanguage(OcrLanguageMode language) {
    ocrLanguage_ = language;
    if (bar_ != nullptr)
        bar_->setOcrLanguage(language);
}

void Overlay::setStyle(const CaptureStyle &style) {
    style_ = style;
    if (sidebar_ != nullptr)
        sidebar_->setStyle(style);
    update();
}

void Overlay::setHistory(const QStringList &labels, int index) {
    if (bar_ != nullptr) {
        bar_->setHistory(labels);
        bar_->setHistoryIndex(index);
    }
}

void Overlay::setHistoryIndex(int index) {
    if (bar_ != nullptr)
        bar_->setHistoryIndex(index);
}

void Overlay::showHistoryPicture(const QImage &picture) {
    if (picture.isNull()) {
        // Stepping forward past the newest capture is the screen itself, which is where
        // the window started.
        frame_ = liveFrame_;
    } else {
        frame_.image = picture;
        // A picture that is not the screen cannot be asked which window is under the
        // pointer, and cannot be scrolled, so both of those are switched off rather
        // than answered wrongly.
        frame_.nativeGeometry = {};
        frame_.windowScopeAvailable = false;
        frame_.frontWindows.clear();
    }
    visual_.clear();
    native_.clear();
    // The picture under everything changed, so the scaled copy of it is now a copy of
    // the wrong picture.
    frameScaled_ = {};
    resetSelection();
    update();
}

void Overlay::setSelections(const QVector<QRect> &selections) {
    selections_ = selections;
    if (bar_ != nullptr)
        bar_->setSelections(selections);
}

// --- 长截图 ---

void Overlay::beginScroll(bool running, const QImage &picture, Qt::Orientation axis, bool keep) {
    scrollRunning_ = running;
    if (running) {
        scrolling_ = true;
        scrollAxis_ = axis;
        scrollPicture_ = picture;
        scrollFrames_ = 0;
        scrollPartial_ = false;
    } else if (!keep) {
        // The run is over and the region goes back to being an ordinary capture. The
        // picture and the panel go with it: leaving a stitched long picture beside a
        // region that is no longer a window onto a page would be a picture of nothing.
        scrolling_ = false;
        scrollPicture_ = {};
        scrollFrames_ = 0;
        scrollPartial_ = false;
    }
    if (bar_ != nullptr) {
        bar_->setScrollState(scrolling_, scrollRunning_, scrollAxis_);
        bar_->setScrollStatus(scrollStatus());
        // While the region is a window onto a page the picture-making actions would take
        // a picture of whatever happens to be under the region at that instant, which is
        // a frame of the run rather than the page. 完成 and 关闭 are what is left.
        bar_->setBusy(scrollRunning_, scrollRunning_ ? scrollStatus() : QString());
    }
    if (sidebar_ != nullptr)
        sidebar_->setBusy(scrollRunning_);
    // The region is settled: a long capture is taken of a region, not of a half-drawn
    // one, and dragging it is how the last of the content is reached.
    ready_ = !selected_.isEmpty();
    update();
}

void Overlay::updateScroll(const QImage &picture, int frames, bool partial) {
    scrollPicture_ = picture;
    scrollFrames_ = frames;
    scrollPartial_ = partial;
    if (bar_ != nullptr)
        bar_->setScrollStatus(scrollStatus());
    update();
}

QString Overlay::scrollStatus() const {
    if (!scrolling_)
        return {};
    if (!scrollRunning_ && scrollPicture_.isNull())
        return tr("长截图 · %1 · 按开始采集").arg(scrollAxisLabel(scrollAxis_));
    const int length = scrollAxis_ == Qt::Horizontal ? scrollPicture_.width() : scrollPicture_.height();
    if (!scrollRunning_ && scrollFrames_ == 0 && !scrollPicture_.isNull())
        return tr("长截图 · %1 · 已停止").arg(scrollLengthText(length));
    return tr("长截图 · %1 · %2%3")
        .arg(scrollLengthText(length))
        .arg(scrollRunning_ ? tr("采集中，再次按下停止") : tr("已停止"))
        .arg(scrollPartial_ ? tr(" · 有一处接缝是放宽的") : QString());
}

void Overlay::restoreLastSelection() {
    if (selections_.isEmpty())
        return;
    applySelection(selections_.first(), false);
}

void Overlay::applySelection(QRect area, bool move) {
    // The region a long capture is being taken of is not reshaped underneath the frames
    // that were already read from it: the width of a vertical capture is what every
    // frame has to agree on, and a region that changed shape would leave the older ones
    // with nothing to line up against.
    if (scrolling_)
        return;
    const QRect bounds(QPoint(0, 0), frame_.image.size());
    area = area.intersected(bounds);
    // A region that is being moved keeps its shape; one that is being sized follows the
    // locked ratio, so a corner drag cannot quietly break it.
    if (!move && !area.isEmpty())
        area = captureRectWithRatio(area, ratio_, bar_->customWidth(), bar_->customHeight())
                   .intersected(bounds);
    if (area.isEmpty())
        return;
    selected_ = area;
    ready_ = true;
    showBar();
    update();
}

void Overlay::showBar() {
    if (bar_ == nullptr || selected_.isEmpty())
        return;
    const QRect local = localRect(selected_).toRect();
    bar_->setSelectionSize(selected_.size());
    // The corner is written down in the pixels of the screen, which is what a capture
    // tool shows and what a bug report asks for.
    bar_->setSelectionOrigin(selected_.topLeft());
    bar_->placeNear(local, rect());
    bar_->show();
    bar_->raise();
    if (sidebar_ != nullptr) {
        sidebar_->placeBeside(local, rect());
        sidebar_->show();
        sidebar_->raise();
    }
}

void Overlay::hideTools() {
    if (bar_ != nullptr)
        bar_->hide();
    if (sidebar_ != nullptr)
        sidebar_->hide();
}

void Overlay::movePointer(int dx, int dy) {
    if ((dx == 0 && dy == 0) || frame_.image.isNull() || width() <= 0 || height() <= 0)
        return;
    // One whole step of the pointer, on one axis only. The step is taken here and not
    // in pixels of the grab: on a scaled display a grab pixel is a fraction of a step,
    // so a step taken there and rounded back into steps of the pointer sometimes
    // rounded to where it had started — an arrow key that appeared to do nothing — and
    // sometimes rounded the two axes different ways, which is what made pressing one
    // key move the pointer in two directions at once.
    const QPoint base(std::clamp(pointer_.x(), 0, width() - 1),
                      std::clamp(pointer_.y(), 0, height() - 1));
    pointer_ = QPoint(std::clamp(base.x() + dx, 0, width() - 1),
                      std::clamp(base.y() + dy, 0, height() - 1));
    QCursor::setPos(mapToGlobal(pointer_));
    cursor_ = pixelPoint(pointer_);
    // Before anything has been taken the readout is up wherever the pointer is; after
    // that it is up over the picture that was taken and nowhere else.
    magnifierVisible_ = selected_.isEmpty() || selected_.contains(cursor_);
    update();
}

void Overlay::stretchSelection(Qt::Key key, int amount) {
    if (selected_.isEmpty() || amount == 0)
        return;
    QRect area = selected_;
    switch (key) {
    case Qt::Key_Left:
        area.setLeft(area.left() - amount);
        break;
    case Qt::Key_Right:
        area.setRight(area.right() + amount);
        break;
    case Qt::Key_Up:
        area.setTop(area.top() - amount);
        break;
    case Qt::Key_Down:
        area.setBottom(area.bottom() + amount);
        break;
    default:
        return;
    }
    // A region squeezed to nothing is not a region, so the last pixel is kept.
    if (area.width() < 1 || area.height() < 1)
        return;
    applySelection(area, true);
}

QColor Overlay::colourAt(QPoint pixel) const {
    if (frame_.image.isNull())
        return {};
    const QPoint clamped(std::clamp(pixel.x(), 0, frame_.image.width() - 1),
                         std::clamp(pixel.y(), 0, frame_.image.height() - 1));
    return frame_.image.pixelColor(clamped);
}

void Overlay::copyColour() {
    const QColor colour = colourAt(cursor_);
    if (!colour.isValid())
        return;
    // The text is what goes on the clipboard, written the way the user asked for it.
    QApplication::clipboard()->setText(captureColourText(colour, colourFormat_));
    colourCopied_ = true;
    copyNote_.start();
    update();
}

void Overlay::startPicking() {
    if (selected_.isEmpty() || busy_)
        return;
    // The button and the key are the same request, so pressing either one twice puts
    // the window back the way it was.
    if (picking_) {
        leavePicking();
        return;
    }
    picking_ = true;
    magnifierVisible_ = true;
    setCursor(Qt::CrossCursor);
    update();
}

void Overlay::leavePicking() {
    if (!picking_)
        return;
    picking_ = false;
    colourCopied_ = false;
    copyNote_.stop();
    magnifierVisible_ = false;
    setCursor(Qt::CrossCursor);
    update();
}

int Overlay::handleAt(QPoint pixel) const {
    if (!ready_ || selected_.isEmpty())
        return -1;
    // A handle only a couple of pixels across is a handle nobody can hit, so the
    // corners are given a reach in screen pixels and converted to picture pixels.
    const int reach = std::max(3, int(std::lround(7.0 * frame_.image.width() / std::max(1, width()))));
    const QVector<QPoint> corners{selected_.topLeft(), selected_.topRight(), selected_.bottomLeft(),
                                  selected_.bottomRight()};
    for (int index = 0; index < corners.size(); ++index) {
        const QPoint gap = corners.at(index) - pixel;
        if (std::abs(gap.x()) <= reach && std::abs(gap.y()) <= reach)
            return index;
    }
    return -1;
}
void Overlay::closeEvent(QCloseEvent *e) {
    e->ignore();
    emit cancelled();
}
void Overlay::requestProbe() {
    if (probe_ || !selected_.isEmpty() || drawing_ || finished_ || frame_.nativeGeometry.isEmpty())
        return;
    QPoint pixel = cursor_;
    QPoint native(frame_.nativeGeometry.x() +
                      qRound(double(pixel.x()) * frame_.nativeGeometry.width() / frame_.image.width()),
                  frame_.nativeGeometry.y() +
                      qRound(double(pixel.y()) * frame_.nativeGeometry.height() / frame_.image.height()));
    probe_ = new QProcess(this);
    QProcess *process = probe_;
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    auto timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, pixel](int code, QProcess::ExitStatus) {
                if (code == 0 && pixel == cursor_) {
                    native_.clear();
                    auto json = QJsonDocument::fromJson(process->readAllStandardOutput());
                    for (const auto &item : json.array()) {
                        try {
                            auto object = item.toObject();
                            QRect native = jsonRect(object["bounds"].toObject());
                            double sx = double(frame_.image.width()) / frame_.nativeGeometry.width(),
                                   sy = double(frame_.image.height()) / frame_.nativeGeometry.height();
                            int x = int(std::floor((native.x() - frame_.nativeGeometry.x()) * sx)),
                                y = int(std::floor((native.y() - frame_.nativeGeometry.y()) * sy));
                            int xx = int(std::ceil((native.x() + native.width() - frame_.nativeGeometry.x()) *
                                                   sx)),
                                yy = int(std::ceil(
                                    (native.y() + native.height() - frame_.nativeGeometry.y()) * sy));
                            QRect uncut(x, y, xx - x, yy - y),
                                bounds = uncut.intersected(QRect(QPoint(0, 0), frame_.image.size()));
                            if (bounds.isEmpty())
                                continue;
                            auto target = object["target"].toObject();
                            target["clipped"] = uncut != bounds;
                            bool exists = false;
                            for (const auto &c : native_)
                                if (c.bounds == bounds) {
                                    exists = true;
                                    break;
                                }
                            if (!exists && native_.size() < 400)
                                native_.append({bounds, target});
                        } catch (const std::exception &) {
                        }
                    }
                    if (selected_.isEmpty() && !drawing_) {
                        picker_.update(candidates(), cursor_);
                        update();
                    }
                }
                if (probe_ == process)
                    probe_ = nullptr;
                process->deleteLater();
                if (pixel != cursor_ && !drawing_ && !finished_) debounce_.start();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (probe_ == process)
                probe_ = nullptr;
            process->deleteLater();
        }
    });
    process->start(QCoreApplication::applicationFilePath(),
                   {"--inspect", QString::number(native.x()), QString::number(native.y()),
                    QString::number(QCoreApplication::applicationPid())});
    timeout->start(1200);
}
} // namespace h2d
