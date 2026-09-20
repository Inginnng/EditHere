#include "canvas.h"
#include "ui.h"
#include <QFocusEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <exception>
namespace h2d {
namespace {
bool backgroundBounds(QRectF bounds, QSize canvas) {
    const double fraction = bounds.width() * bounds.height() /
                            std::max(1.0, double(canvas.width()) * canvas.height());
    return fraction >= .94 || (fraction >= .65 &&
           (bounds.width() >= canvas.width() * .95 || bounds.height() >= canvas.height() * .95));
}
double segmentDistance(QPointF point, QPointF start, QPointF finish) {
    const auto vector = finish - start;
    const double lengthSquared = QPointF::dotProduct(vector, vector);
    const double t = lengthSquared > .0001
        ? std::clamp(QPointF::dotProduct(point - start, vector) / lengthSquared, 0.0, 1.0) : 0;
    return QLineF(point, start + vector * t).length();
}
void paintMovement(QPainter &p, const MovementMarker &movement,
                   double zoom, bool hovered, double phase) {
    const auto start = movement.source.center() * zoom;
    const auto finish = movement.destination.center() * zoom;
    const auto line = QLineF(start, finish);
    if (line.length() < .5)
        return;
    const auto unit = (finish - start) / line.length();
    const QPointF normal(-unit.y(), unit.x());
    const double pulse = .5 + .5 * std::sin(phase);
    if (hovered) {
        p.setPen(QPen(QColor(0, 122, 255, 18 + qRound(pulse * 16)), 3,
                      Qt::SolidLine, Qt::RoundCap));
        p.drawLine(line);
        p.setPen(QPen(QColor(0, 122, 255, 65 + qRound(pulse * 20)), .8, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        for (const auto &r : {movement.source, movement.destination})
            p.drawRect(QRectF(r.topLeft() * zoom, r.size() * zoom));
    }
    p.setPen(QPen(QColor(255, 255, 255, hovered ? 155 : 65), hovered ? 2.7 : 2.2,
                  Qt::SolidLine, Qt::RoundCap));
    p.drawLine(line);
    auto color = accent();
    color.setAlpha(hovered ? 220 : 95);
    p.setPen(QPen(color, hovered ? 1.35 : 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(line);
    const double head = std::min(7.5, std::max(3.5, line.length() * .35));
    p.drawPolyline(QPolygonF{finish - unit * head + normal * head * .5,
                            finish, finish - unit * head - normal * head * .5});
    p.setBrush(QColor(255, 255, 255, hovered ? 220 : 95));
    p.drawEllipse(start, hovered ? 2.5 : 2.2, hovered ? 2.5 : 2.2);
}
} // namespace

Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("图片批注画布");
    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(32);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        hoverPhase_ += .13;
        update();
    });
}
void Canvas::setDocument(Document *doc) {
    doc_ = doc;
    layoutPreview_ = false;
    layoutImage_ = {};
    selected_.clear();
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    picker_.reset();
    drawing_ = moving_ = panning_ = false;
    stopMiddlePan();
    rebuildDisplay();
    setZoom(zoom_);
}
void Canvas::setMode(Mode mode) {
    mode_ = mode;
    drawing_ = moving_ = panning_ = false;
    stopMiddlePan();
    picker_.reset();
    setCursor(mode == Adjust ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}
void Canvas::setZoom(double value) {
    zoom_ = std::clamp(value, .03, 4.0);
    if (doc_)
        setFixedSize(std::max(1, qRound(doc_->image.width() * zoom_)),
                     std::max(1, qRound(doc_->image.height() * zoom_)));
    update();
}
void Canvas::select(const QString &id) {
    selected_ = id;
    emit selectionChanged(id);
    update();
}
void Canvas::refresh() {
    rebuildDisplay();
    picker_.reset();
    update();
}
void Canvas::setLayoutPreview(bool enabled) {
    const bool preview = enabled && doc_ && doc_->layout.has_value();
    if (layoutPreview_ != preview) {
        drawing_ = moving_ = panning_ = false;
        stopMiddlePan();
        picker_.reset();
    }
    layoutPreview_ = preview;
    rebuildDisplay();
    update();
}
void Canvas::setAnnotationsVisible(bool visible) {
    annotationsVisible_ = visible;
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    update();
}
void Canvas::rebuildDisplay() {
    layoutImage_ = {};
    displayCandidates_.clear();
    movements_.clear();
    if (!doc_)
        return;
    if (doc_->layout)
        try {
            movements_ = movementMarkers(*doc_->layout, doc_->notes);
        } catch (const std::exception &) {
            // Keep an invalid unsaved layout inspectable; export reports its validation error.
        }
    if (!layoutPreview_ || !doc_->layout) {
        displayCandidates_ = doc_->candidates;
        if (doc_->layout)
            for (const auto &group : doc_->layout->groups)
                if (group.origin == "manual") {
                    auto target = manualTarget();
                    target["method"] = "manual-region";
                    target["label"] = group.label;
                    displayCandidates_.append({layoutBounds(*doc_->layout, group.id).toAlignedRect(), target});
                }
        return;
    }
    const auto &layout = *doc_->layout;
    layoutImage_ = renderLayout(doc_->image, layout);
    QHash<QString, QRectF> destinations;
    for (const auto &piece : layout.pieces)
        destinations.insert(piece.id, piece.destination);
    displayCandidates_.reserve(layout.groups.size());
    for (const auto &group : layout.groups) {
        QRectF bounds;
        for (const auto &id : group.pieces) {
            const auto piece = destinations.value(id);
            bounds = bounds.isEmpty() ? piece : bounds.united(piece);
        }
        // Round outward to include every visible pixel of a resized component.
        const int x1 = std::clamp(int(std::floor(bounds.x())), 0, doc_->image.width());
        const int y1 = std::clamp(int(std::floor(bounds.y())), 0, doc_->image.height());
        const int x2 = std::clamp(int(std::ceil(bounds.x() + bounds.width())), 0, doc_->image.width());
        const int y2 = std::clamp(int(std::ceil(bounds.y() + bounds.height())), 0, doc_->image.height());
        const QRect rectangle(x1, y1, x2 - x1, y2 - y1);
        if (rectangle.isEmpty())
            continue;
        auto target = manualTarget();
        target["source"] = "vision";
        target["label"] = group.label;
        target["method"] = group.origin == "manual" ? "manual-region" : "layout-region";
        displayCandidates_.append({rectangle, target});
    }
}
QPoint Canvas::toImage(QPointF p) const {
    return {std::clamp(qRound(p.x() / zoom_), 0, doc_->image.width()),
            std::clamp(qRound(p.y() / zoom_), 0, doc_->image.height())};
}
QPointF Canvas::noteAnchor(const Note &note) const {
    for (const auto &movement : movements_)
        if (movement.noteIndex >= 0 && movement.noteIndex < doc_->notes.size() &&
            doc_->notes[movement.noteIndex].id == note.id)
            return movementMarkerAnchor(movement, zoom_, size());
    QPointF anchor = QPointF(note.isPoint ? note.point : note.rect.topLeft()) * zoom_;
    if (!note.isPoint)
        anchor += QPointF(18, 18);
    anchor.setX(std::clamp(anchor.x(), 14.0, std::max(14.0, width() - 14.0)));
    anchor.setY(std::clamp(anchor.y(), 14.0, std::max(14.0, height() - 14.0)));
    return anchor;
}
int Canvas::hit(QPointF p, bool rectangles) const {
    if (!annotationsVisible_ || !doc_)
        return -1;
    for (int i = doc_->notes.size() - 1; i >= 0; --i) {
        const auto &note = doc_->notes[i];
        if (!note.isGlobal && QLineF(p, noteAnchor(note)).length() < 17)
            return i;
    }
    if (rectangles)
        for (int i = doc_->notes.size() - 1; i >= 0; --i) {
            const auto &note = doc_->notes[i];
            if (!note.isGlobal && !note.isPoint && containsPixel(note.rect, toImage(p)))
                return i;
        }
    return -1;
}
int Canvas::hitMovement(QPointF screen) const {
    if (annotationsVisible_)
        for (int i = movements_.size() - 1; i >= 0; --i)
            if (segmentDistance(screen, movements_[i].source.center() * zoom_,
                                movements_[i].destination.center() * zoom_) < 8 ||
                (movements_[i].noteIndex >= 0 &&
                 QLineF(screen, movementMarkerAnchor(movements_[i], zoom_, size())).length() < 17))
                return i;
    return -1;
}
void Canvas::updateAnnotationHover(QPointF screen) {
    const int note = hit(screen, false);
    const QString nextNote = note >= 0 ? doc_->notes[note].id : QString();
    const int nextMovement = note < 0 ? hitMovement(screen) : -1;
    if (nextNote != hoveredNote_ || nextMovement != hoveredMovement_)
        hoverPhase_ = 0;
    hoveredNote_ = nextNote;
    hoveredMovement_ = nextMovement;
    if (!hoveredNote_.isEmpty() || hoveredMovement_ >= 0) {
        if (!hoverTimer_->isActive())
            hoverTimer_->start();
        setCursor(Qt::PointingHandCursor);
    } else {
        hoverTimer_->stop();
        setCursor(mode_ == Adjust ? Qt::ArrowCursor : Qt::CrossCursor);
    }
}
void Canvas::paintEvent(QPaintEvent *event) {
    if (!doc_)
        return;
    QPainter p(this);
    paintScene(p,event->rect());
    if(magnifierEnabled_ && pointerInside_ && !middlePanning_ && !panning_ && mode_!=Point) {
        const QRect visible=visibleRegion().boundingRect();
        if(visible.width()<220 || visible.height()<170) return;
        QPointF at=pointer_+QPointF(24,24);
        if(at.x()+210>visible.right()) at.setX(pointer_.x()-234);
        if(at.y()+162>visible.bottom()) at.setY(pointer_.y()-186);
        at.setX(std::clamp(at.x(),double(visible.left()),double(visible.right()-210)));
        at.setY(std::clamp(at.y(),double(visible.top()),double(visible.bottom()-162)));
        QRectF panel(at,QSizeF(210,162)), area(at+QPointF(6,6),QSizeF(198,126));
        p.setPen(Qt::NoPen); p.setBrush(QColor(25,28,34)); p.drawRoundedRect(panel,9,9);
        p.save(); p.setClipRect(area); p.fillRect(area,QColor(45,48,55));
        const QPoint pixel=toImage(pointer_);
        const QPointF sample=(QPointF(pixel)+QPointF(.5,.5))*zoom_;
        p.translate(area.center()); p.scale(10/zoom_,10/zoom_); p.translate(-sample);
        paintScene(p,rect());
        p.restore();
        p.save(); p.setClipRect(area); p.setRenderHint(QPainter::Antialiasing,false);
        p.setPen(QPen(QColor(255,255,255,70),0));
        for(int offset=-100;offset<=100;offset+=10) {
            double x=area.center().x()+offset-5,y=area.center().y()+offset-5;
            p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
            p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        }
        p.setPen(QPen(Qt::white,0)); p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(area.center()-QPointF(5,5),QSizeF(10,10))); p.restore();
        p.setPen(Qt::white); p.setFont(QFont("Microsoft YaHei",9));
        p.drawText(QRectF(at+QPointF(6,134),QSizeF(198,22)),Qt::AlignCenter,
                   QString("像素 %1, %2 · 1格=1px").arg(pixel.x()).arg(pixel.y()));
    }
}
void Canvas::paintScene(QPainter &p, QRect exposed) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform,false);
    if (layoutPreview_) {
        paintTransparency(p, exposed);
        p.drawImage(rect(), layoutImage_);
    } else
        p.drawImage(rect(), doc_->image);
    auto outline = [&](QRect r, QColor color, bool fill, bool dashed) {
        QRectF scaled(r.x() * zoom_, r.y() * zoom_, r.width() * zoom_, r.height() * zoom_);
        p.setPen(QPen(color, 2, dashed ? Qt::DashLine : Qt::SolidLine));
        QColor wash = color;
        wash.setAlpha(18);
        p.setBrush(fill ? QBrush(wash) : Qt::NoBrush);
        p.drawRect(scaled);
    };
    if (doc_->layout && (mode_ == Smart || mode_ == Rectangle))
        for (const auto &group : doc_->layout->groups)
            if (group.origin == "manual")
                outline(layoutBounds(*doc_->layout, group.id).toAlignedRect(), QColor(0, 122, 255, 75), false, true);
    if (mode_ == Smart && !drawing_ && picker_.current())
        outline(picker_.current()->bounds, accent(), true, true);
    if (annotationsVisible_)
        for (int i = 0; i < movements_.size(); ++i)
            paintMovement(p, movements_[i], zoom_, i == hoveredMovement_, hoverPhase_);
    int number = 0;
    for (const auto &stored : doc_->notes) {
        ++number;
        if (!annotationsVisible_ || stored.isGlobal)
            continue;
        Note n = moving_ && stored.id == original_.id ? preview_ : stored;
        bool chosen = n.id == selected_;
        const bool hovered = n.id == hoveredNote_;
        const double pulse = hovered ? .5 + .5 * std::sin(hoverPhase_) : 0;
        if (!n.isPoint)
            outline(n.rect, accent(), chosen, false);
        const QPointF anchor = noteAnchor(n);
        if (chosen || hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 122, 255, 38 + qRound(pulse * 14)));
            p.drawEllipse(anchor, 18 + pulse * 2, 18 + pulse * 2);
        }
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(accent());
        p.drawEllipse(anchor, 13 + pulse * 1.5, 13 + pulse * 1.5);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(anchor.x() - 13, anchor.y() - 13, 26, 26), Qt::AlignCenter,
                   QString::number(number));
        if (mode_ == Adjust && chosen && !n.isPoint) {
            p.setPen(QPen(accent(), 1));
            p.setBrush(Qt::white);
            for (QPoint handle : handles(n.rect)) {
                QPointF pos = QPointF(handle) * zoom_;
                p.drawRect(QRectF(pos.x() - 4, pos.y() - 4, 8, 8));
            }
        }
    }
    if (drawing_ && (mode_ == Rectangle || (end_ - start_).manhattanLength() * zoom_ > 5))
        outline(dragRect(start_, end_, doc_->image.size()), accent(), true, true);
}
void Canvas::stopMiddlePan(bool suppressMouse) {
    middlePanning_ = false;
    suppressMouse_ = suppressMouse;
    setCursor(space_ ? Qt::OpenHandCursor : mode_ == Adjust ? Qt::ArrowCursor : Qt::CrossCursor);
}
void Canvas::mousePressEvent(QMouseEvent *e) {
    pointer_=e->position(); pointerInside_=true;
    if (!doc_)
        return;
    if ((suppressMouse_ && e->buttons() == e->button()) ||
        (middlePanning_ && e->button() == Qt::MiddleButton &&
         e->buttons() == Qt::MiddleButton))
        stopMiddlePan();
    if (middlePanning_ || suppressMouse_ ||
        (e->button() != Qt::MiddleButton && e->buttons().testFlag(Qt::MiddleButton))) {
        stopMiddlePan(e->buttons() != Qt::NoButton);
        e->accept();
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        setFocus();
        drawing_ = moving_ = panning_ = false;
        pending_.reset();
        hoveredNote_.clear();
        hoveredMovement_ = -1;
        hoverTimer_->stop();
        middlePanning_ = e->buttons() == Qt::MiddleButton;
        suppressMouse_ = !middlePanning_;
        middlePanLast_ = e->globalPosition().toPoint();
        if (middlePanning_)
            setCursor(Qt::ClosedHandCursor);
        update();
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton) {
        drawing_ = moving_ = false;
        emit contextRequested(e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    setFocus();
    if (space_ || (e->modifiers() & Qt::AltModifier)) {
        panning_ = true;
        panStart_ = e->globalPosition().toPoint();
        windowStart_ = window()->pos();
        return;
    }
    QPoint point = toImage(e->position());
    const int badge = hit(e->position(), false);
    if (badge >= 0) {
        select(doc_->notes[badge].id);
        emit editRequested(doc_->notes[badge], false, e->globalPosition().toPoint());
        return;
    }
    const int movement = hitMovement(e->position());
    if (movement >= 0) {
        const int noteIndex = movements_[movement].noteIndex;
        if (noteIndex >= 0 && noteIndex < doc_->notes.size()) {
            select(doc_->notes[noteIndex].id);
            emit editRequested(doc_->notes[noteIndex], false, e->globalPosition().toPoint());
            return;
        }
        emit movementAnnotationRequested(movements_[movement].source, movements_[movement].destination);
        return;
    }
    int index = hit(e->position(), mode_ == Adjust);
    handle_ = -1;
    if (mode_ == Adjust && annotationsVisible_)
        for (int i = 0; i < doc_->notes.size(); i++) {
            const auto &n = doc_->notes[i];
            if (n.id == selected_ && !n.isPoint && !n.isGlobal) {
                auto hs = handles(n.rect);
                for (int h = 0; h < hs.size(); h++)
                    if (QLineF(e->position(), QPointF(hs[h]) * zoom_).length() < 9) {
                        index = i;
                        handle_ = h;
                    }
            }
        }
    if (index >= 0) {
        const auto &note = doc_->notes[index];
        select(note.id);
        if (mode_ != Adjust) {
            emit editRequested(note, false, e->globalPosition().toPoint());
            return;
        }
        original_ = preview_ = note;
        moving_ = true;
        start_ = point;
    } else if (mode_ == Adjust) {
        select({});
        panning_ = true;
        panStart_ = e->globalPosition().toPoint();
        windowStart_ = window()->pos();
    } else {
        picker_.update(displayCandidates_, point);
        pending_ = picker_.current();
        if (mode_ == Rectangle) {
            pending_.reset();
            for (const auto &candidate : displayCandidates_)
                if (candidate.target["method"] == "manual-region" && containsPixel(candidate.bounds, point) &&
                    (!pending_ || candidate.bounds.width() * candidate.bounds.height() <
                                  pending_->bounds.width() * pending_->bounds.height()))
                    pending_ = candidate;
        }
        drawing_ = true;
        start_ = end_ = point;
    }
    update();
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    pointer_=e->position(); pointerInside_=true; update();
    if (!doc_)
        return;
    if (middlePanning_ || suppressMouse_) {
        if (!middlePanning_ || e->buttons() != Qt::MiddleButton)
            stopMiddlePan(e->buttons() != Qt::NoButton);
        else {
            // Scrolling changes local coordinates, so use the pointer's screen position.
            const QPoint current = e->globalPosition().toPoint();
            const QPoint delta = current - middlePanLast_;
            middlePanLast_ = current;
            if (!delta.isNull())
                emit panRequested(delta);
        }
        e->accept();
        return;
    }
    if (panning_) {
        if (!window()->isFullScreen() && !window()->isMaximized())
            window()->move(windowStart_ + e->globalPosition().toPoint() - panStart_);
        return;
    }
    QPoint p = toImage(e->position());
    if (!moving_ && !drawing_)
        updateAnnotationHover(e->position());
    if (moving_) {
        preview_ = original_;
        QPoint delta = p - start_;
        if (original_.isPoint)
            preview_.point = {std::clamp(original_.point.x() + delta.x(), 0, doc_->image.width() - 1),
                              std::clamp(original_.point.y() + delta.y(), 0, doc_->image.height() - 1)};
        else
            preview_.rect = moveRect(original_.rect, delta, doc_->image.size(), handle_);
        update();
    } else if (drawing_) {
        end_ = p;
        update();
    } else if (mode_ == Smart) {
        picker_.update(displayCandidates_, p);
        updateHint();
        update();
    }
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (middlePanning_ || suppressMouse_ || e->button() == Qt::MiddleButton) {
        stopMiddlePan(e->buttons() != Qt::NoButton);
        e->accept();
        return;
    }
    if (e->button() != Qt::LeftButton || !doc_)
        return;
    if (panning_) {
        panning_ = false;
        return;
    }
    if (moving_) {
        moving_ = false;
        if (original_.point != preview_.point || original_.rect != preview_.rect) {
            preview_.updatedAt = timestamp();
            emit geometryChanged(preview_);
        }
        update();
        return;
    }
    if (!drawing_)
        return;
    drawing_ = false;
    end_ = toImage(e->position());
    Note n;
    if (mode_ == Rectangle) {
        const auto area = dragRect(start_, end_, doc_->image.size());
        if ((end_ - start_).manhattanLength() * zoom_ <= 5 && pending_ &&
            pending_->target["method"] == "manual-region") {
            n.isPoint = false;
            n.rect = pending_->bounds;
            n.target = pending_->target;
            emit editRequested(n, true, e->globalPosition().toPoint());
        } else if (!area.isEmpty() && (end_ - start_).manhattanLength() * zoom_ > 5)
            emit regionRequested(area);
        update();
        return;
    }
    if (mode_ == Smart && (end_ - start_).manhattanLength() * zoom_ > 5) {
        n.isPoint = false;
        n.rect = dragRect(start_, end_, doc_->image.size());
        if (n.rect.isEmpty()) {
            update();
            return;
        }
    } else if (mode_ == Smart && pending_) {
        n.isPoint = false;
        n.rect = pending_->bounds;
        n.target = pending_->target;
    } else {
        n.point = {std::clamp(start_.x(), 0, doc_->image.width() - 1),
                   std::clamp(start_.y(), 0, doc_->image.height() - 1)};
    }
    update();
    emit editRequested(n, true, e->globalPosition().toPoint());
}
void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && !middlePanning_ && !suppressMouse_ &&
        doc_ && mode_ == Adjust) {
        moving_ = drawing_ = false;
        int i = hit(e->position(), true);
        if (i >= 0)
            emit editRequested(doc_->notes[i], false, e->globalPosition().toPoint());
    }
}
void Canvas::wheelEvent(QWheelEvent *e) {
    if (!doc_ || drawing_ || moving_ || middlePanning_ || suppressMouse_) {
        e->accept();
        return;
    }
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y();
    if (!delta) {
        e->ignore();
        return;
    }
    if (mode_ == Smart && !(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::MetaModifier)) {
        picker_.update(displayCandidates_, toImage(e->position()));
        const QPoint point = toImage(e->position());
        const bool specific = std::any_of(displayCandidates_.cbegin(), displayCandidates_.cend(),
            [&](const Candidate &candidate) {
                if (!containsPixel(candidate.bounds, point))
                    return false;
                const auto method = candidate.target["method"].toString();
                if (method == "manual-region" || method.startsWith("table-"))
                    return true;
                const bool entireImage = QRectF(candidate.bounds).contains(
                    QRectF(doc_->image.rect()).adjusted(1, 1, -1, -1));
                return !entireImage && !(method == "color-region" &&
                                        backgroundBounds(candidate.bounds, doc_->image.size())) &&
                       !(method == "layout-region" && candidate.target["label"] == "色块区域" &&
                         backgroundBounds(candidate.bounds, doc_->image.size()));
            });
        if (specific)
            picker_.step(delta > 0 ? 1 : -1);
        else
            emit zoomRequested(zoom_ * (delta > 0 ? 1.12 : 1 / 1.12), e->position());
        updateHint();
        update();
    } else
        emit zoomRequested(zoom_ * (delta > 0 ? 1.12 : 1 / 1.12), e->position());
    e->accept();
}
void Canvas::updateHint() {
    auto c = picker_.current();
    emit hintChanged(c ? QString("%1 · %2 / %3 · 滚轮切换大小")
                             .arg(c->target["label"].toString())
                             .arg(picker_.level())
                             .arg(picker_.count())
                       : "单击批注 · 拖动框选");
}
void Canvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) {
        space_ = true;
        if (!middlePanning_)
            setCursor(Qt::OpenHandCursor);
        e->accept();
    } else if (e->key() == Qt::Key_Escape) {
        drawing_ = moving_ = panning_ = false;
        stopMiddlePan();
        update();
        e->accept();
    } else
        QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) {
        space_ = false;
        if (!middlePanning_)
            setCursor(mode_ == Adjust ? Qt::ArrowCursor : Qt::CrossCursor);
    } else
        QWidget::keyReleaseEvent(e);
}
void Canvas::leaveEvent(QEvent *) {
    pointerInside_=false; update();
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    if (!drawing_ && !moving_) {
        picker_.reset();
        update();
    }
}
void Canvas::focusOutEvent(QFocusEvent *e) {
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    space_ = false;
    drawing_ = moving_ = panning_ = false;
    stopMiddlePan();
    update();
    QWidget::focusOutEvent(e);
}
} // namespace h2d
