#include "overlay.h"
#include "capturetoolbar.h"
#include "detector.h"
#include "i18n.h"
#include "ui.h"
#include "fonts.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QShowEvent>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
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
enum Slot { kPixelSlot, kRgbSlot };
double rectOverlapRatio(QRect a, QRect b) {
    const QRect inter = a.intersected(b);
    if (inter.isEmpty())
        return 0.0;
    const qint64 interArea = qint64(inter.width()) * inter.height();
    const qint64 unionArea = qint64(a.width()) * a.height() + qint64(b.width()) * b.height() - interArea;
    return unionArea > 0 ? double(interArea) / unionArea : 0.0;
}
double rectContainedRatio(QRect inner, QRect outer) {
    const qint64 innerArea = qint64(inner.width()) * inner.height();
    if (innerArea <= 0)
        return 0.0;
    const QRect inter = inner.intersected(outer);
    const qint64 interArea = qint64(inter.width()) * inter.height();
    return double(interArea) / innerArea;
}
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
    requestDetection();
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
    connect(bar_, &CaptureToolbar::annotateRequested, this,
            [this] { emit accepted(selected_, documentCandidates()); });
    connect(bar_, &CaptureToolbar::scrollRequested, this, [this] { emit scrollRequested(selected_); });
    connect(bar_, &CaptureToolbar::markupToolChanged, this, [this] {
        draftMarkup_.reset();
        setFocus();
        updateCursor();
        update();
    });
    connect(bar_, &CaptureToolbar::undoMarkupRequested, this, &Overlay::undoMarkup);
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
    connect(bar_, &CaptureToolbar::moved, this, [this] {
        if (captureNotice_ && captureNotice_->isVisible())
            placeCaptureNotice();
        update();
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

void Overlay::requestDetection() {
    const quint64 generation = ++detectionGeneration_;
    auto *worker = new QFutureWatcher<QVector<Candidate>>(this);
    connect(worker, &QFutureWatcher<QVector<Candidate>>::finished, this,
            [this, worker, generation] {
        if (generation == detectionGeneration_) {
            visual_ = worker->result();
            auto full = manualTarget();
            full["label"] = QT_TRANSLATE_NOOP("EditHere", "整个屏幕");
            visual_.append({QRect(QPoint(0, 0), frame_.image.size()), full});
            if (selected_.isEmpty()) {
                picker_.update(candidates(), cursor_, false, shouldPreferWindow());
                update();
            }
        }
        worker->deleteLater();
    });
    worker->setFuture(QtConcurrent::run([image = frame_.image] { return detectBlocks(image); }));
}

Overlay::~Overlay() {
    // The inspection child is a child object, so QObject destroys it only after every member of
    // this class is gone — but its finished handler reads debounce_, picker_ and calls update().
    // When the window is destroyed while that child still runs (its 1200 ms deadline has not
    // passed yet), the handler used to run against a half-destroyed window: the macOS runner
    // died with SIGSEGV at the end of a ui test (REG-115). Cut the signals first, then reap.
    QProcess *probe = probe_;
    probe_ = nullptr;
    if (!probe)
        return;
    probe->disconnect(this);
    if (probe->state() != QProcess::NotRunning) {
        probe->kill();
        probe->waitForFinished(1000);
    }
}

void Overlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (rect().contains(local)) {
        pointer_ = local;
        cursor_ = pixelPoint(local);
        updateCursor();
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
        if(!front) {
            QVector<Candidate> result;
            for(auto candidate:visual_) {
                const QRect clipped = candidate.bounds.intersected(frame_.image.rect());
                if(clipped.width() < 12 || clipped.height() < 10 ||
                   rectOverlapRatio(clipped, frame_.image.rect()) >= 0.82)
                    continue;
                candidate.bounds = clipped;
                result.append(candidate);
            }
            auto target=manualTarget(); target["label"]=QT_TRANSLATE_NOOP("EditHere", "整个屏幕");
            result.append({frame_.image.rect(),target});
            return result;
        }
        QVector<Candidate> result;
        for(auto candidate:native_) {
            if(rectContainedRatio(candidate.bounds, front->bounds) < 0.85)
                continue;
            const QRect clipped = candidate.bounds.intersected(front->bounds);
            if(clipped.width() < 12 || clipped.height() < 10 || clipped == front->bounds ||
               rectOverlapRatio(clipped, front->bounds) >= 0.92)
                continue;
            candidate.target["clipped"] = candidate.target["clipped"].toBool() || clipped != candidate.bounds;
            candidate.bounds = clipped;
            result.append(candidate);
        }
        const int nativeCount = result.size();
        for(auto candidate:visual_) {
            if(rectContainedRatio(candidate.bounds, front->bounds) < 0.88)
                continue;
            const QRect clipped = candidate.bounds.intersected(front->bounds);
            if(clipped.width() < 12 || clipped.height() < 10 || clipped == front->bounds ||
               rectOverlapRatio(clipped, front->bounds) >= 0.82)
                continue;
            bool shadowedByNative = false;
            for(int i = 0; i < nativeCount; ++i) {
                if(rectOverlapRatio(clipped, result.at(i).bounds) >= 0.85) {
                    shadowedByNative = true;
                    break;
                }
            }
            if(shadowedByNative)
                continue;
            candidate.bounds = clipped;
            result.append(candidate);
        }
        result.append(*front);
        if(front->bounds != frame_.image.rect() &&
           rectOverlapRatio(front->bounds, frame_.image.rect()) < 0.95) {
            auto screenTarget = manualTarget();
            screenTarget["label"] = QT_TRANSLATE_NOOP("EditHere", "整个屏幕");
            result.append({frame_.image.rect(), screenTarget});
        }
        return result;
    }
    auto result = native_;
    result += visual_;
    return result;
}
QVector<Candidate> Overlay::documentCandidates() const {
    auto result = candidates();
    for (const auto &edit : markup_) {
        if (!edit.mosaic.isNull())
            result.removeIf([&edit](const Candidate &candidate) { return candidate.bounds.intersects(edit.area); });
    }
    return result;
}

bool Overlay::shouldPreferWindow() const {
    if (!frame_.windowScopeAvailable)
        return false;
    const Candidate *front = nullptr;
    for (const auto &window : frame_.frontWindows) {
        if (window.bounds.contains(cursor_)) {
            front = &window;
            break;
        }
    }
    if (!front || front->target.value("source").toString() != QLatin1String("window"))
        return false;
    const QRect fb = front->bounds;
    if (cursor_.y() - fb.top() <= 48 || cursor_.x() - fb.left() <= 14 ||
        fb.right() - cursor_.x() <= 14 || fb.bottom() - cursor_.y() <= 14)
        return true;
    const qint64 winArea = std::max<qint64>(1, qint64(fb.width()) * fb.height());
    qint64 smallestSubArea = std::numeric_limits<qint64>::max();
    bool hasSubElement = false;
    for (const auto &cand : native_) {
        if (!cand.bounds.contains(cursor_) || rectContainedRatio(cand.bounds, fb) < 0.85)
            continue;
        const QRect clipped = cand.bounds.intersected(fb);
        if (clipped.width() < 12 || clipped.height() < 10 || clipped == fb ||
            rectOverlapRatio(clipped, fb) >= 0.92)
            continue;
        hasSubElement = true;
        smallestSubArea = std::min(smallestSubArea, qint64(clipped.width()) * clipped.height());
    }
    for (const auto &cand : visual_) {
        if (!cand.bounds.contains(cursor_) || rectContainedRatio(cand.bounds, fb) < 0.88)
            continue;
        const QRect clipped = cand.bounds.intersected(fb);
        if (clipped.width() < 12 || clipped.height() < 10 || clipped == fb ||
            rectOverlapRatio(clipped, fb) >= 0.82)
            continue;
        hasSubElement = true;
        smallestSubArea = std::min(smallestSubArea, qint64(clipped.width()) * clipped.height());
    }
    if (!hasSubElement)
        return true;
    return double(smallestSubArea) / winArea > 0.65;
}
void Overlay::resetSelection() {
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
    if (bar_ != nullptr)
        bar_->resetUserPosition();
    clearMarkup();
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
    // Selection decorations belong on the screen. The magnifier samples the original
    // grab separately so the outline cannot cover the pixels being aimed at.
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
        p.fillPath(mask, captureChromeTokens().scrim);
    }
    if (active.isEmpty() || picking_)
        return;
    QRectF outline = localRect(active);
    if (ready_ && (!markup_.isEmpty() || draftMarkup_)) {
        p.save();
        p.setClipRect(outline);
        p.scale(1.0 / sx, 1.0 / sy);
        drawMarkup(p, true);
        p.restore();
    }
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
    p.setPen(QPen(accent(), 1.5));
    p.drawRoundedRect(outline, radius, radius);
    // Compact PixPin-grade dimension readout while dragging or hovering a candidate/window.
    if (!ready_) {
        QString dimText = QStringLiteral("%1 × %2").arg(active.width()).arg(active.height());
        if (!drawing_ && selected_.isEmpty()) {
            if (const auto current = picker_.current()) {
                if (current->target.value("source").toString() == QLatin1String("window")) {
                    QString winHint;
                    for (const auto &cw : frame_.captureWindows) {
                        if (cw.bounds == current->bounds) {
                            winHint = !cw.windowTitle.isEmpty() ? cw.windowTitle : cw.appName;
                            break;
                        }
                    }
                    if (winHint.isEmpty())
                        winHint = tr("窗口");
                    if (winHint.size() > 22)
                        winHint = winHint.left(21) + QStringLiteral("…");
                    dimText += QStringLiteral(" · %1").arg(winHint);
                }
            }
        }
        QFont dimFont(QStringLiteral("Consolas"));
        dimFont.setStyleHint(QFont::Monospace);
        dimFont.setPointSizeF(9.0);
        dimFont.setWeight(QFont::DemiBold);
        p.setFont(dimFont);
        const int badgeW = p.fontMetrics().horizontalAdvance(dimText) + 14;
        const int badgeH = 22;
        const qreal bx = std::clamp(outline.left(), 8.0, std::max(8.0, double(width()) - badgeW - 8.0));
        const qreal minY = captureInstructionHeight + 4;
        const qreal by = std::clamp(outline.top() - badgeH - 6.0, minY,
                                    std::max(minY, double(height()) - badgeH - 8.0));
        const QRectF dimRect(bx, by, badgeW, badgeH);
        const bool overlapsBar = bar_ != nullptr && bar_->isVisible() &&
                                 dimRect.intersects(QRectF(bar_->geometry()).adjusted(-6, -6, 6, 6));
        if (!overlapsBar) {
            const auto t = captureChromeTokens();
            paintFloatingPanel(p, dimRect, t.controlRadius);
            p.setPen(t.text);
            p.drawText(dimRect, Qt::AlignCenter, dimText);
        }
    }
    if (ready_ && bar_->markupTool() == CaptureMarkupTool::None)
        paintCaptureHandles(p, outline);
}

void Overlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawScene(p, rect());
    // The magnifier and the colour readout are up while a region is being found, and
    // again once it has settled whenever the pointer is inside it: pointing at the
    // picture that was just taken is exactly when a colour is wanted, so reading one
    // should not cost a keystroke. C still turns it into a mode that copies on click.
    const bool overSettledRegion =
        ready_ && !selected_.isEmpty() && selected_.contains(cursor_);
    if (!finished_ && (!ready_ || picking_ ||
        (overSettledRegion && bar_->markupTool() == CaptureMarkupTool::None))) {
        QPointF at(double(cursor_.x()) * width() / frame_.image.width(),
                   double(cursor_.y()) * height() / frame_.image.height());
        const Qt::CursorShape shape = cursor().shape();
        const bool adjusting = shape == Qt::SizeFDiagCursor || shape == Qt::SizeBDiagCursor ||
                               shape == Qt::SizeVerCursor || shape == Qt::SizeHorCursor ||
                               shape == Qt::SizeAllCursor;
        // The native resize/move cursor is the aim during a selection adjustment.
        // Painting our crosshair beneath it would still make a handle look like a
        // place to draw a new selection.
        if (!adjusting) {
            const QPen pen(QColor(255, 255, 255, 210), 1);
            p.setPen(pen);
            p.drawLine(at + QPointF(-9, 0), at + QPointF(9, 0));
            p.drawLine(at + QPointF(0, -9), at + QPointF(0, 9));
        }
        drawMagnifier(p, at);
    }
    const auto t = captureChromeTokens();
    QFont hintFont(h2d::cjkFontFamily());
    hintFont.setPointSizeF(9.5);
    p.setFont(hintFont);
    const int maxTextWidth = std::max(1, width() - 60);
    const QString text = p.fontMetrics().elidedText(captureHint(), Qt::ElideRight, maxTextWidth);
    const int w = p.fontMetrics().horizontalAdvance(text) + 28;
    const QRectF hint((width() - w) / 2.0, 8, w, 32);
    paintFloatingPanel(p, hint, t.cardRadius);
    p.setPen(t.text);
    p.drawText(hint, Qt::AlignCenter, text);
}

QString Overlay::captureHint() const {
    if (picking_) {
        return colourCopied_ ? tr("已复制颜色值 %1").arg(captureColourText(colourAt(cursor_), colourFormat_))
                             : tr("移动到要取色的位置 · C 复制颜色值 · Shift 切换格式 · Esc 结束取色");
    }
    if (ready_ && !selected_.isEmpty()) {
        if (bar_->markupTool() == CaptureMarkupTool::Brush)
            return tr("在选区内拖动画笔 · Ctrl+Z 撤销 · Esc 退出画笔");
        if (bar_->markupTool() == CaptureMarkupTool::Mosaic)
            return tr("在选区内拖出马赛克区域 · Ctrl+Z 撤销 · Esc 退出马赛克");
        return tr("拖动移动选区 · 8 锚点缩放 · 双击或 Ctrl+C 复制 · 回车批注 · C 取色 · Esc 取消");
    }
    if (drawing_)
        return tr("松开鼠标完成选区 · Esc 取消");
    if (const auto current = picker_.current())
        return tr("%1  ·  %2 / %3  ·  滚轮 ↑ 更大 ↓ 更小")
            .arg(localizedLabel(current->target["label"].toString()).left(30))
            .arg(picker_.level())
            .arg(picker_.count());
    return tr("拖动截图 · 单击智能吸附窗口 · 松手后可批注、贴图或取色 · Esc 取消");
}

QRect Overlay::captureToolBounds() const {
    return rect().adjusted(0, captureInstructionHeight, 0, 0);
}

void Overlay::drawMagnifier(QPainter &p, QPointF at) {
    // The bar and the column of tools are children of this window, so they are painted
    // over the panel; the panel is placed to keep clear of them.
    QVector<QRectF> tools{QRectF(0, 0, width(), captureInstructionHeight)};
    if (bar_ != nullptr && bar_->isVisible())
        tools.append(bar_->geometry());
    if (sidebar_ != nullptr && sidebar_->isVisible())
        tools.append(sidebar_->geometry());
    if (captureNotice_ && captureNotice_->isVisible())
        tools.append(captureNotice_->geometry());
    const QRectF panel = magnifierPlacement(at, QSizeF(width(), height()), tools);
    if (panel.isEmpty())
        return;
    const double x = panel.left(), y = panel.top();
    const double inner = panel.width() - magnifierPanelPadding * 2;
    const QRectF area(QPointF(x + magnifierPanelPadding, y + magnifierPanelPadding),
                      QSizeF(inner, magnifierZoomHeight));
    const auto t = captureChromeTokens();
    paintFloatingPanel(p, panel);
    const QPoint sample(std::clamp(cursor_.x(), 0, frame_.image.width() - 1),
                        std::clamp(cursor_.y(), 0, frame_.image.height() - 1));
    // The panel follows the pointer: the pixel shown is always the one under the
    // cursor, so aiming and reading are the same gesture.
    const QColor colour = colourAt(sample);
    QPainterPath areaClip;
    areaClip.addRoundedRect(area, t.controlRadius, t.controlRadius);
    p.save();
    p.setClipPath(areaClip);
    p.fillRect(area, t.stage);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const double cell = magnifierZoomCell;
    const QPointF center = area.center();
    // Only the strip of the screen the panel shows is handed to the painter. Giving it
    // the whole grab under a scale made it resample the entire screen a second time on
    // every repaint, which on a scaled display is ten milliseconds a frame.
    const QRectF source(sample.x() + 0.5 - (center.x() - area.left()) / cell,
                        sample.y() + 0.5 - (center.y() - area.top()) / cell,
                        area.width() / cell, area.height() / cell);
    p.drawImage(area, frame_.image, source);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(255, 255, 255, 28), 0));
    for (double gx = center.x() - cell / 2; gx >= area.left(); gx -= cell)
        p.drawLine(QPointF(gx, area.top()), QPointF(gx, area.bottom()));
    for (double gx = center.x() + cell / 2; gx <= area.right(); gx += cell)
        p.drawLine(QPointF(gx, area.top()), QPointF(gx, area.bottom()));
    for (double gy = center.y() - cell / 2; gy >= area.top(); gy -= cell)
        p.drawLine(QPointF(area.left(), gy), QPointF(area.right(), gy));
    for (double gy = center.y() + cell / 2; gy <= area.bottom(); gy += cell)
        p.drawLine(QPointF(area.left(), gy), QPointF(area.right(), gy));
    // Neutral dark outer contrast frame + white inner reticle so zoom pixels stay pure
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 145), 1));
    p.drawRect(QRectF(center - QPointF(cell / 2 + 1, cell / 2 + 1), QSizeF(cell + 2, cell + 2)));
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(QRectF(center - QPointF(cell / 2, cell / 2), QSizeF(cell, cell)));
    p.restore();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.panelBorder, 1));
    p.drawRoundedRect(area, t.controlRadius, t.controlRadius);

    const double firstRow = y + magnifierPanelPadding + magnifierZoomHeight + 5;
    const auto slotRect = [&](int slot) {
        return QRectF(x + magnifierPanelPadding, firstRow + slot * kRowHeight, inner, kRowHeight);
    };
    QFont monoFont(QStringLiteral("Consolas"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPointSizeF(8.0);
    p.setFont(monoFont);
    p.setPen(t.textSecondary);
    p.drawText(slotRect(kPixelSlot), Qt::AlignVCenter,
               tr("像素 %1, %2").arg(sample.x()).arg(sample.y()));
    const QVector<ColourFormat> formats = panel.height() == magnifierCompactPanelHeight
        ? QVector<ColourFormat>{colourFormat_}
        : QVector<ColourFormat>{ColourFormat::Rgb, ColourFormat::Hex, ColourFormat::Hsv, ColourFormat::Hsl};
    for (int index = 0; index < formats.size(); ++index) {
        const ColourFormat format = formats.at(index);
        const bool current = format == colourFormat_;
        const QRectF row = slotRect(kRgbSlot + index);
        if (current) {
            p.setPen(Qt::NoPen);
            p.setBrush(t.activeBg);
            p.drawRoundedRect(row.adjusted(0, 1, 0, -1), 4, 4);
            const QRectF swatch(row.left() + 3, row.top() + 3.5, 11, 11);
            p.setBrush(colour);
            p.setPen(QPen(t.divider, 1));
            p.drawRoundedRect(swatch, 3, 3);
        }
        p.setPen(current ? t.text : t.textSecondary);
        p.drawText(row.adjusted(18, 0, -2, 0), Qt::AlignVCenter,
                   captureColourText(colour, format));
    }
    QFont hintFont(h2d::cjkFontFamily());
    hintFont.setPointSizeF(8.0);
    p.setFont(hintFont);
    p.setPen(t.textSecondary);
    const QRectF note(x + magnifierPanelPadding, firstRow + (kRgbSlot + formats.size()) * kRowHeight + 3, inner, 16);
    p.drawText(note, Qt::AlignVCenter, tr("C 复制 · Shift 切换"));
}

void Overlay::mousePressEvent(QMouseEvent *e) {
    if (busy_)
        return;
    if (e->button() == Qt::RightButton) {
        if (picking_) {
            leavePicking();
            return;
        }
        if (bar_->markupTool() != CaptureMarkupTool::None) {
            bar_->setMarkupTool(CaptureMarkupTool::None);
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
    pointer_ = e->position();
    cursor_ = pixel;
    if (picking_) {
        // The colour under the pointer is what the window is for at this moment, and
        // the region is left exactly where it was.
        copyColour();
        leavePicking();
        return;
    }
    if (ready_ && bar_->markupTool() != CaptureMarkupTool::None) {
        if (selected_.contains(pixel))
            beginMarkup(pixel);
        e->accept();
        return;
    }
    if (ready_) {
        // A settled region is moved from the inside, resized from a corner, and
        // replaced by a drag that starts anywhere else.
        handle_ = handleAt(pixel);
        if (handle_ >= 0) {
            dragOrigin_ = selected_;
            dragStart_ = pixel;
            updateCursor();
            update();
            return;
        }
        if (selected_.contains(pixel)) {
            handle_ = kMoving;
            dragOrigin_ = selected_;
            dragStart_ = pixel;
            updateCursor();
            update();
            return;
        }
        ready_ = false;
        hideTools();
    }
    emit selectionBegan();
    clearMarkup();
    bar_->resetUserPosition();
    start_ = cursor_ = pixel;
    keyboardOffset_ = {};
    hoverTimer_.stop();
    magnifierVisible_ = true;
    nudged_ = false;
    setCursor(Qt::BlankCursor);
    setFocus();
    picker_.update(candidates(), start_, false, shouldPreferWindow());
    drawing_ = true;
    selected_ = {};
    update();
}
void Overlay::mouseMoveEvent(QMouseEvent *e) {
    if (busy_)
        return;
    const QPointF local = e->position();
    // Ignore the short-lived cursor-warp echo, which can be rounded by the
    // platform. Ordinary mouse movement must retain sub-DIP precision as well.
    const bool dragging = drawing_ || handle_ != -1 || draftMarkup_.has_value();
    const bool echo = pointerWarpAge_.isValid() && pointerWarpAge_.elapsed() < 200 &&
                      std::abs(local.x() - pointer_.x()) < 1.0 &&
                      std::abs(local.y() - pointer_.y()) < 1.0;
    const bool adopted = dragging || !echo;
    if (adopted)
        pointer_ = local;
    // A warp echo must not reset the candidate chosen with the wheel.
    if (!adopted)
        return;
    const QPoint pixel = pixelPoint(local);
    if (draftMarkup_) {
        cursor_ = pixel;
        updateMarkup(pixel);
        return;
    }
    if (handle_ == kMoving) {
        cursor_ = pixel;
        const QPoint shift = pixel - dragStart_;
        QRect moved = dragOrigin_.translated(shift);
        // Confine the position, rather than clipping away selected pixels at an edge.
        moved.moveLeft(std::clamp(moved.left(), 0, frame_.image.width() - moved.width()));
        moved.moveTop(std::clamp(moved.top(), 0, frame_.image.height() - moved.height()));
        applySelection(moved, true);
        return;
    }
    if (handle_ >= 0) {
        cursor_ = pixel;
        // The opposite corner/edge stays put while a handle is dragged.
        QRect resized = dragOrigin_;
        if (handle_ <= 3) {
            if (handle_ == 0 || handle_ == 2)
                resized.setLeft(pixel.x());
            else
                resized.setRight(pixel.x());
            if (handle_ == 0 || handle_ == 1)
                resized.setTop(pixel.y());
            else
                resized.setBottom(pixel.y());
        } else if (handle_ == 4) {
            resized.setTop(pixel.y());
        } else if (handle_ == 5) {
            resized.setBottom(pixel.y());
        } else if (handle_ == 6) {
            resized.setLeft(pixel.x());
        } else if (handle_ == 7) {
            resized.setRight(pixel.x());
        }
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
        picker_.update(candidates(), cursor_, false, shouldPreferWindow());
        debounce_.start();
    }
    updateCursor();
    update();
}
void Overlay::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (draftMarkup_) {
        finishMarkup(pixelPoint(e->position()));
        return;
    }
    if (handle_ != -1) {
        handle_ = -1;
        pointer_ = e->position();
        cursor_ = pixelPoint(e->position());
        updateCursor();
        update();
        return;
    }
    if (!drawing_)
        return;
    drawing_ = false;
    setCursor(Qt::CrossCursor);
    pointer_ = e->position();
    QPoint end = pixelPoint(e->position()) + keyboardOffset_;
    end.setX(std::clamp(end.x(),0,frame_.image.width()));
    end.setY(std::clamp(end.y(),0,frame_.image.height()));
    cursor_ = end;
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
    if (bar_->markupTool() != CaptureMarkupTool::None) {
        mousePressEvent(e);
        return;
    }
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
    pointer_ = e->position();
    cursor_ = pixelPoint(e->position());
    picker_.update(candidates(), cursor_, false, shouldPreferWindow());
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
    if (ready_ && !picking_) {
        if (e->matches(QKeySequence::Undo)) {
            undoMarkup();
            e->accept();
            return;
        }
        const auto tool = bar_->markupTool();
        if (e->key() == Qt::Key_Escape && tool != CaptureMarkupTool::None) {
            bar_->setMarkupTool(CaptureMarkupTool::None);
            e->accept();
            return;
        }
        if (draftMarkup_) {
            e->accept();
            return;
        }
        if (e->modifiers() == Qt::NoModifier && (e->key() == Qt::Key_B || e->key() == Qt::Key_M)) {
            const auto requested = e->key() == Qt::Key_B ? CaptureMarkupTool::Brush : CaptureMarkupTool::Mosaic;
            bar_->setMarkupTool(tool == requested ? CaptureMarkupTool::None : requested);
            e->accept();
            return;
        }
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
            emit accepted(selected_, documentCandidates());
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_L) {
            bar_->scroll();
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
        picker_.update(candidates(), cursor_, true, shouldPreferWindow());
        debounce_.start();
        update();
        e->accept();
        return;
    }
    if (e->key() == Qt::Key_Tab && selected_.isEmpty() && !drawing_ && !picking_) {
        picker_.update(candidates(), cursor_, false, shouldPreferWindow());
        picker_.toggleWindow();
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
        emit accepted(area, documentCandidates());
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
    const QImage region = selectionContent();
    if (region.isNull())
        return {};
    return composeCapture(region, style_);
}

QImage Overlay::selectionContent() const {
    QImage region = selectionPixels();
    if (region.isNull() || markup_.isEmpty())
        return region;
    region = region.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal ratio = region.devicePixelRatio();
    QPainter painter(&region);
    painter.scale(1 / ratio, 1 / ratio);
    painter.translate(-selected_.topLeft());
    drawMarkup(painter, false);
    painter.end();
    return region;
}

void Overlay::beginMarkup(QPoint pixel) {
    auto &edit = draftMarkup_.emplace();
    edit.start = pixel;
    const auto &options = bar_->markupOptions();
    if (bar_->markupTool() == CaptureMarkupTool::Brush) {
        edit.ink = options.brushColor;
        edit.width = options.brushWidth;
        if (options.highlighter) {
            edit.width *= 2.6;
            edit.ink.setAlphaF(0.38);
        }
        edit.stroke.moveTo(QPointF(pixel) + QPointF(0.5, 0.5));
    } else {
        edit.strength = options.mosaicStrength;
        edit.blur = options.blur;
    }
    update();
}

void Overlay::updateMarkup(QPoint pixel) {
    auto &edit = *draftMarkup_;
    pixel.setX(std::clamp(pixel.x(), selected_.left(), selected_.right()));
    pixel.setY(std::clamp(pixel.y(), selected_.top(), selected_.bottom()));
    if (edit.width > 0) {
        const QPointF at = QPointF(pixel) + QPointF(0.5, 0.5);
        if (edit.stroke.currentPosition() != at)
            edit.stroke.lineTo(at);
    } else {
        edit.area = QRect(edit.start, pixel).normalized();
    }
    update();
}

void Overlay::finishMarkup(QPoint pixel) {
    updateMarkup(pixel);
    auto edit = std::move(*draftMarkup_);
    draftMarkup_.reset();
    if (edit.width <= 0) {
        if (edit.area.width() < 2 || edit.area.height() < 2)
            return;
        const QSize cells(std::max(1, qRound(double(edit.area.width()) / edit.strength)),
                          std::max(1, qRound(double(edit.area.height()) / edit.strength)));
        edit.mosaic = frame_.image.copy(edit.area).scaled(cells, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (edit.mosaic.isNull())
            return;
    }
    markup_.append(std::move(edit));
    bar_->setHasMarkup(true);
}

void Overlay::undoMarkup() {
    if (draftMarkup_)
        draftMarkup_.reset();
    else if (!markup_.isEmpty())
        markup_.removeLast();
    bar_->setHasMarkup(!markup_.isEmpty());
    update();
}

void Overlay::clearMarkup() {
    draftMarkup_.reset();
    markup_.clear();
    bar_->setMarkupTool(CaptureMarkupTool::None);
    bar_->setHasMarkup(false);
}

void Overlay::drawMarkup(QPainter &painter, bool includeDraft) const {
    painter.setRenderHint(QPainter::Antialiasing);
    const auto paint = [&painter](const Markup &edit) {
        if (!edit.mosaic.isNull()) {
            painter.save();
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, edit.blur);
            painter.drawImage(QRectF(edit.area), edit.mosaic, QRectF(edit.mosaic.rect()));
            painter.restore();
        } else if (edit.width > 0) {
            const QPen pen(edit.ink, edit.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            if (edit.stroke.elementCount() == 1)
                painter.drawPoint(edit.stroke.currentPosition());
            else
                painter.drawPath(edit.stroke);
        } else if (!edit.area.isEmpty()) {
            QPen pen(accent(), 1, Qt::DashLine);
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.setBrush(captureChromeTokens().activeBg);
            painter.drawRect(QRectF(edit.area));
        }
    };
    for (const auto &edit : markup_)
        paint(edit);
    if (includeDraft && draftMarkup_)
        paint(*draftMarkup_);
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

void Overlay::setBusy(bool busy, const QString &message) {
    busy_ = busy;
    updateCursor();
    if (bar_ != nullptr) {
        bar_->setBusy(busy, message);
        if (busy)
            showBar();
    }
    if (sidebar_ != nullptr)
        sidebar_->setBusy(busy);
    update();
}

void Overlay::setCaptureNotice(const QString &message) {
    if (message.isEmpty()) {
        if (captureNotice_)
            captureNotice_->hide();
        return;
    }
    if (!captureNotice_) {
        captureNotice_ = new QLabel(this);
        captureNotice_->setObjectName(QStringLiteral("captureStatus"));
        captureNotice_->setWordWrap(true);
        captureNotice_->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    const auto t = captureChromeTokens();
    captureNotice_->setStyleSheet(QStringLiteral(
        "QLabel#captureStatus { color: %1; background: rgba(%2, %3, %4, %5);"
        " border: 1px solid rgba(%6, %7, %8, %9); border-radius: %10px; padding: 10px 12px; }")
        .arg(t.text.name())
        .arg(t.panelBg.red()).arg(t.panelBg.green()).arg(t.panelBg.blue()).arg(t.panelBg.alpha())
        .arg(t.panelBorder.red()).arg(t.panelBorder.green()).arg(t.panelBorder.blue()).arg(t.panelBorder.alpha())
        .arg(t.panelRadius));
    captureNotice_->setText(message);
    placeCaptureNotice();
    captureNotice_->show();
    captureNotice_->raise();
}

void Overlay::placeCaptureNotice() {
    if (!captureNotice_ || !bar_)
        return;
    // A failed action must leave its button under the pointer for a retry. The
    // notice is a sibling of the bar so its text cannot resize or move the tools.
    const int gap = 8;
    captureNotice_->setFixedWidth(std::max(1, std::min(480, width() - 2 * gap)));
    captureNotice_->adjustSize();
    const int x = std::clamp(bar_->x(), gap, std::max(gap, width() - captureNotice_->width() - gap));
    const QRect selection = localRect(selected_).toRect();
    const QRect side = sidebar_ && sidebar_->isVisible() ? sidebar_->geometry() : QRect();
    const QRect bounds = captureToolBounds();
    const QVector<int> positions = {
        std::max({bar_->geometry().bottom(), selection.bottom(), side.bottom()}) + gap,
        std::min(bar_->y(), selection.y()) - captureNotice_->height() - gap,
        bar_->geometry().bottom() + gap,
        bar_->y() - captureNotice_->height() - gap,
        height() - captureNotice_->height() - gap,
        bounds.top() + gap};
    for (int y : positions) {
        const QRect area(QPoint(x, y), captureNotice_->size());
        if (bounds.contains(area) && !area.intersects(bar_->geometry()) && !area.intersects(side)) {
            captureNotice_->move(x, y);
            return;
        }
    }
    captureNotice_->move(x, bounds.top() + gap);
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
    if (bar_)
        bar_->setScrollAvailable(picture.isNull());
    debounce_.stop();
    if (auto *process = probe_) {
        probe_ = nullptr;
        process->disconnect(this);
        process->kill();
        process->deleteLater();
    }
    if (picture.isNull()) {
        // Stepping forward past the newest capture is the screen itself, which is where
        // the window started.
        frame_ = liveFrame_;
    } else {
        frame_.image = picture;
        // A picture that is not the screen cannot be asked which window is under the
        // pointer, so that is switched off rather than answered wrongly.
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
    requestDetection();
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
    if (captureNotice_ && area != selected_)
        captureNotice_->hide();
    selected_ = area;
    ready_ = true;
    updateCursor();
    showBar();
    update();
}

void Overlay::showBar() {
    if (bar_ == nullptr || selected_.isEmpty())
        return;
    const QRect local = localRect(selected_).toRect();
    bar_->show();
    bar_->setSelectionSize(selected_.size());
    // The corner is written down in the pixels of the screen, which is what a capture
    // tool shows and what a bug report asks for.
    bar_->setSelectionOrigin(selected_.topLeft());
    const QRect bounds = captureToolBounds();
    bar_->placeNear(local, bounds);
    bar_->raise();
    if (sidebar_ != nullptr) {
        sidebar_->placeBeside(local, bounds);
        sidebar_->show();
        sidebar_->raise();
    }
    if (captureNotice_ && captureNotice_->isVisible()) {
        placeCaptureNotice();
        captureNotice_->raise();
    }
}

void Overlay::hideTools() {
    if (captureNotice_ != nullptr)
        captureNotice_->hide();
    if (bar_ != nullptr)
        bar_->hide();
    if (sidebar_ != nullptr)
        sidebar_->hide();
}

void Overlay::movePointer(int dx, int dy) {
    if ((dx == 0 && dy == 0) || frame_.image.isNull() || width() <= 0 || height() <= 0)
        return;
    cursor_ = QPoint(std::clamp(cursor_.x() + dx, 0, frame_.image.width() - 1),
                     std::clamp(cursor_.y() + dy, 0, frame_.image.height() - 1));
    if (dx) pointer_.setX(double(cursor_.x()) * width() / frame_.image.width());
    if (dy) pointer_.setY(double(cursor_.y()) * height() / frame_.image.height());
    pointerWarpAge_.restart();
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows" && frame_.nativePixels &&
        frame_.nativeGeometry.size() == frame_.image.size())
        moveCapturePointer(frame_, cursor_);
    else
#endif
        QCursor::setPos(mapToGlobal(pointer_.toPoint()));
    updateCursor();
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
    updateCursor();
    update();
}

void Overlay::updateCursor() {
    Qt::CursorShape shape = Qt::CrossCursor;
    if (!busy_ && !finished_ && !picking_) {
        if (drawing_)
            shape = Qt::BlankCursor;
        else if (bar_->markupTool() != CaptureMarkupTool::None)
            shape = Qt::CrossCursor;
        else if (handle_ == kMoving)
            shape = Qt::SizeAllCursor;
        else {
            const int handle = handle_ >= 0 ? handle_ : handleAt(cursor_);
            if (handle >= 0) {
                if (handle <= 3)
                    shape = handle == 0 || handle == 3 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
                else
                    shape = (handle == 4 || handle == 5) ? Qt::SizeVerCursor : Qt::SizeHorCursor;
            }
        }
    }
    setCursor(shape);
}

int Overlay::handleAt(QPoint pixel) const {
    if (!ready_ || selected_.isEmpty())
        return -1;
    // A handle only a couple of pixels across is a handle nobody can hit, so the
    // corners and edge midpoints are given a reach in screen pixels converted to image pixels.
    const int reach = std::max(3, int(std::lround(7.0 * frame_.image.width() / std::max(1, width()))));
    const QVector<QPoint> anchors{
        selected_.topLeft(), selected_.topRight(), selected_.bottomLeft(), selected_.bottomRight(),
        QPoint(selected_.center().x(), selected_.top()),
        QPoint(selected_.center().x(), selected_.bottom()),
        QPoint(selected_.left(), selected_.center().y()),
        QPoint(selected_.right(), selected_.center().y())};
    for (int index = 0; index < anchors.size(); ++index) {
        const QPoint gap = anchors.at(index) - pixel;
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
    if (probe_ || !selected_.isEmpty() || drawing_ || finished_ || frame_.nativeGeometry.isEmpty() ||
        !frame_.elementProbingAllowed)
        return;
    QPoint pixel = cursor_;
    const QRect elementGeometry = frame_.elementGeometry.isEmpty() ? frame_.nativeGeometry : frame_.elementGeometry;
    QPoint native(elementGeometry.x() +
                      qRound(double(pixel.x()) * elementGeometry.width() / frame_.image.width()),
                  elementGeometry.y() +
                      qRound(double(pixel.y()) * elementGeometry.height() / frame_.image.height()));
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
                            const QRect elementGeometry = frame_.elementGeometry.isEmpty() ? frame_.nativeGeometry : frame_.elementGeometry;
                            double sx = double(frame_.image.width()) / elementGeometry.width(),
                                   sy = double(frame_.image.height()) / elementGeometry.height();
                            int x = int(std::floor((native.x() - elementGeometry.x()) * sx)),
                                y = int(std::floor((native.y() - elementGeometry.y()) * sy));
                            int xx = int(std::ceil((native.x() + native.width() - elementGeometry.x()) *
                                                   sx)),
                                yy = int(std::ceil(
                                    (native.y() + native.height() - elementGeometry.y()) * sy));
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
                        picker_.update(candidates(), cursor_, false, shouldPreferWindow());
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
