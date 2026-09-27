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
// The magnifier panel. It is laid out from these rather than from a layout class,
// because it is painted rather than assembled and the two have to agree exactly.
constexpr double kPanelWidth = 216;
constexpr double kPanelHeight = 268;
constexpr double kPanelPad = 8;
constexpr double kZoomHeight = 126;
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
    connect(bar_, &CaptureToolbar::pickRequested, this, &Overlay::startPicking);
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
    selected_ = {};
    drawing_ = false;
    ready_ = false;
    picking_ = false;
    handle_ = -1;
    keyboardOffset_ = {};
    setCursor(Qt::CrossCursor);
    picker_.reset();
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

void Overlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(), scaledFrame());
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
    if (!active.isEmpty() && !picking_) {
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
        if (ready_) {
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
    // The magnifier and the colour readout are up while a region is being found, and
    // again once it has settled whenever the pointer is inside it: pointing at the
    // picture that was just taken is exactly when a colour is wanted, so reading one
    // should not cost a keystroke. C still turns it into a mode that copies on click.
    const bool overSettledRegion =
        ready_ && !selected_.isEmpty() && selected_.contains(cursor_);
    if (!finished_ && (!ready_ || picking_ || overSettledRegion)) {
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
    const QSizeF panelSize(kPanelWidth, kPanelHeight);
    const double gap = 24, margin = 8;
    double x = at.x() + gap, y = at.y() + gap;
    if (x + panelSize.width() > width() - margin)
        x = at.x() - gap - panelSize.width();
    if (y + panelSize.height() > height() - margin)
        y = at.y() - gap - panelSize.height();
    x = std::clamp(x, margin, std::max(margin, width() - panelSize.width() - margin));
    y = std::clamp(y, margin, std::max(margin, height() - panelSize.height() - margin));
    const QRectF panel(QPointF(x, y), panelSize);
    const double inner = panelSize.width() - kPanelPad * 2;
    const QRectF area(QPointF(x + kPanelPad, y + kPanelPad), QSizeF(inner, kZoomHeight));
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
    const double cell=10;
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
    p.drawImage(area, frame_.image, source);
    // The blue frame of the recognised block is part of what the reader is aiming at,
    // so it is drawn into the enlargement as well: the box has to be magnified with
    // the pixels it belongs to, not left at the size it has on the screen.
    const auto toPanel = [&](const QRectF &box) {
        return QRectF(area.left() + (box.left() - source.left()) * (factor / sx),
                      area.top() + (box.top() - source.top()) * (factor / sy),
                      box.width() * (factor / sx), box.height() * (factor / sy));
    };
    p.setBrush(Qt::NoBrush);
    if (!picking_ && selected_.isEmpty()) {
        const auto found = picker_.current();
        if (found) {
            p.setPen(QPen(accent(), 2));
            p.drawRect(toPanel(QRectF(found->bounds)));
        }
    }
    if (!selected_.isEmpty()) {
        p.setPen(QPen(accent(), 2));
        p.drawRect(toPanel(QRectF(selected_)));
    }
    p.restore();
    p.save();
    p.setClipRect(area);
    p.setRenderHint(QPainter::Antialiasing,false);
    p.setPen(QPen(QColor(255,255,255,70),0));
    for(double gx=center.x()-cell/2; gx>=area.left(); gx-=cell) p.drawLine(QPointF(gx,area.top()),QPointF(gx,area.bottom()));
    for(double gx=center.x()+cell/2; gx<=area.right(); gx+=cell) p.drawLine(QPointF(gx,area.top()),QPointF(gx,area.bottom()));
    for(double gy=center.y()-cell/2; gy>=area.top(); gy-=cell) p.drawLine(QPointF(area.left(),gy),QPointF(area.right(),gy));
    for(double gy=center.y()+cell/2; gy<=area.bottom(); gy+=cell) p.drawLine(QPointF(area.left(),gy),QPointF(area.right(),gy));
    // The one pixel the readout is about is boxed, not left to the grid to imply.
    p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white,0));
    p.drawRect(QRectF(center-QPointF(5,5),QSizeF(10,10)));
    p.restore();
    const double firstRow = y + kPanelPad + kZoomHeight + 4;
    const auto slotRect = [&](int slot) {
        return QRectF(x + kPanelPad, firstRow + slot * kRowHeight, inner, kRowHeight);
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
    const QRectF note(x + kPanelPad, firstRow + kSlotCount * kRowHeight + 2, inner, 16);
    p.drawText(note, Qt::AlignVCenter, tr("C 复制颜色值 · Shift 切换格式"));
}

void Overlay::mousePressEvent(QMouseEvent *e) {
    if (busy_)
        return;
    if (e->button() == Qt::RightButton) {
        if (picking_) {
            leavePicking();
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
            setCursor(Qt::SizeAllCursor);
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
    const QPoint pixel = pixelPoint(e->position());
    if (handle_ == kMoving) {
        const QPoint shift = pixel - dragStart_;
        if (shift != QPoint()) {
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
    // teaches before anything else.
    if (ready_ && !selected_.isEmpty()) {
        emit copyRequested(selected_);
        e->accept();
    }
}
void Overlay::wheelEvent(QWheelEvent *e) {
    if (drawing_ || finished_ || ready_ || busy_ || !selected_.isEmpty())
        return;
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
            // One pixel at a time either way, with Shift pulling the side in and Ctrl
            // pushing it out; the plain arrow moves the whole region.
            if (shift)
                stretchSelection(Qt::Key(e->key()), -1);
            else if (ctrl)
                stretchSelection(Qt::Key(e->key()), 1);
            else
                nudgeSelection(e->key() == Qt::Key_Left ? -1 : e->key() == Qt::Key_Right ? 1 : 0,
                               e->key() == Qt::Key_Up ? -1 : e->key() == Qt::Key_Down ? 1 : 0);
            e->accept();
            return;
        }
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
        if (e->key() == Qt::Key_L) {
            emit scrollRequested(selected_);
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
    if (drawing_ && (e->key()==Qt::Key_Left || e->key()==Qt::Key_Right || e->key()==Qt::Key_Up || e->key()==Qt::Key_Down)) {
        QPoint next = cursor_ + QPoint(e->key()==Qt::Key_Right ? 1 : e->key()==Qt::Key_Left ? -1 : 0,
                                       e->key()==Qt::Key_Down ? 1 : e->key()==Qt::Key_Up ? -1 : 0);
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

void Overlay::restoreLastSelection() {
    if (selections_.isEmpty())
        return;
    applySelection(selections_.first(), false);
}

void Overlay::applySelection(QRect area, bool move) {
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

void Overlay::nudgeSelection(int dx, int dy) {
    if (selected_.isEmpty() || (dx == 0 && dy == 0))
        return;
    const QRect moved = selected_.translated(dx, dy);
    // Nudging against an edge of the screen stops there rather than sliding the region
    // half off it.
    if (!QRect(QPoint(0, 0), frame_.image.size()).contains(moved))
        return;
    applySelection(moved, true);
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
