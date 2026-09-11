#include "canvas.h"
#include "ui.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace h2d {
Canvas::Canvas(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("图片批注画布");
}
void Canvas::setDocument(Document *doc) {
    doc_ = doc;
    layoutPreview_ = false;
    layoutImage_ = {};
    selected_.clear();
    picker_.reset();
    drawing_ = moving_ = false;
    setZoom(zoom_);
}
void Canvas::setMode(Mode mode) {
    mode_ = mode;
    drawing_ = moving_ = false;
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
    if (layoutPreview_)
        setLayoutPreview(true);
    update();
}
void Canvas::setLayoutPreview(bool enabled) {
    layoutPreview_ = enabled && doc_ && doc_->layout.has_value();
    layoutImage_ = layoutPreview_ ? renderLayout(doc_->image, *doc_->layout) : QImage();
    drawing_ = moving_ = panning_ = false;
    picker_.reset();
    update();
}
QPoint Canvas::toImage(QPointF p) const {
    return {std::clamp(qRound(p.x() / zoom_), 0, doc_->image.width()),
            std::clamp(qRound(p.y() / zoom_), 0, doc_->image.height())};
}
int Canvas::hit(QPointF p, bool rectangles) const {
    for (int i = doc_->notes.size() - 1; i >= 0; --i) {
        const auto &n = doc_->notes[i];
        QPointF anchor = QPointF(n.isPoint ? n.point : n.rect.topLeft()) * zoom_;
        if (!n.isPoint)
            anchor += QPointF(18, 18);
        anchor.setX(std::clamp(anchor.x(), 14.0, std::max(14.0, width() - 14.0)));
        anchor.setY(std::clamp(anchor.y(), 14.0, std::max(14.0, height() - 14.0)));
        if (QLineF(p, anchor).length() < 17)
            return i;
    }
    if (rectangles)
        for (int i = doc_->notes.size() - 1; i >= 0; --i)
            if (!doc_->notes[i].isPoint && containsPixel(doc_->notes[i].rect, toImage(p)))
                return i;
    return -1;
}
void Canvas::paintEvent(QPaintEvent *) {
    if (!doc_)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (layoutPreview_) {
        p.fillRect(rect(), Qt::white);
        for (int y = 0; y < height(); y += 14)
            for (int x = 0; x < width(); x += 14)
                if ((x / 14 + y / 14) % 2 == 0)
                    p.fillRect(x, y, 14, 14, QColor("#e7e8ed"));
        p.drawImage(rect(), layoutImage_);
        return;
    }
    p.drawImage(rect(), doc_->image);
    auto outline = [&](QRect r, QColor color, bool fill, bool dashed) {
        QRectF scaled(r.x() * zoom_, r.y() * zoom_, r.width() * zoom_, r.height() * zoom_);
        p.setPen(QPen(color, 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
        QColor wash = color;
        wash.setAlpha(18);
        p.setBrush(fill ? QBrush(wash) : Qt::NoBrush);
        p.drawRect(scaled);
    };
    if (mode_ == Smart && !drawing_ && picker_.current())
        outline(picker_.current()->bounds, accent(), true, true);
    int number = 0;
    for (const auto &stored : doc_->notes) {
        Note n = moving_ && stored.id == original_.id ? preview_ : stored;
        bool chosen = n.id == selected_;
        if (!n.isPoint)
            outline(n.rect, accent(), chosen, false);
        QPointF anchor = QPointF(n.isPoint ? n.point : n.rect.topLeft()) * zoom_;
        if (!n.isPoint)
            anchor += QPointF(18, 18);
        anchor.setX(std::clamp(anchor.x(), 14.0, std::max(14.0, width() - 14.0)));
        anchor.setY(std::clamp(anchor.y(), 14.0, std::max(14.0, height() - 14.0)));
        if (chosen) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 122, 255, 38));
            p.drawEllipse(anchor, 18, 18);
        }
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(accent());
        p.drawEllipse(anchor, 13, 13);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(anchor.x() - 13, anchor.y() - 13, 26, 26), Qt::AlignCenter,
                   QString::number(++number));
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
void Canvas::mousePressEvent(QMouseEvent *e) {
    if (!doc_)
        return;
    if (e->button() == Qt::RightButton) {
        drawing_ = moving_ = false;
        emit contextRequested(e->globalPosition().toPoint());
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        emit zoomRequested(std::abs(zoom_ - 1) < .01 ? 0 : 1);
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
    if (layoutPreview_)
        return;
    QPoint point = toImage(e->position());
    int index = hit(e->position(), mode_ == Adjust);
    handle_ = -1;
    if (mode_ == Adjust)
        for (int i = 0; i < doc_->notes.size(); i++) {
            const auto &n = doc_->notes[i];
            if (n.id == selected_ && !n.isPoint) {
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
        picker_.update(doc_->candidates, point);
        pending_ = picker_.current();
        drawing_ = true;
        start_ = end_ = point;
    }
    update();
}
void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (layoutPreview_ && !panning_)
        return;
    if (!doc_)
        return;
    if (panning_) {
        window()->move(windowStart_ + e->globalPosition().toPoint() - panStart_);
        return;
    }
    QPoint p = toImage(e->position());
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
        picker_.update(doc_->candidates, p);
        updateHint();
        update();
    }
}
void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (layoutPreview_) {
        panning_ = false;
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
    if (mode_ == Rectangle || (mode_ == Smart && (end_ - start_).manhattanLength() * zoom_ > 5)) {
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
    if (layoutPreview_) {
        emit layoutEditRequested();
        return;
    }
    if (doc_ && mode_ == Adjust) {
        moving_ = drawing_ = false;
        int i = hit(e->position(), true);
        if (i >= 0)
            emit editRequested(doc_->notes[i], false, e->globalPosition().toPoint());
    }
}
void Canvas::wheelEvent(QWheelEvent *e) {
    if (!doc_ || drawing_ || moving_)
        return;
    if (!layoutPreview_ && mode_ == Smart && !(e->modifiers() & Qt::ControlModifier) &&
        !(e->modifiers() & Qt::MetaModifier)) {
        picker_.update(doc_->candidates, toImage(e->position()));
        picker_.step(e->angleDelta().y() > 0 ? 1 : -1);
        updateHint();
        update();
    } else
        emit zoomRequested(zoom_ * (e->angleDelta().y() > 0 ? 1.12 : 1 / 1.12));
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
        setCursor(Qt::OpenHandCursor);
        e->accept();
    } else if (e->key() == Qt::Key_Escape) {
        drawing_ = moving_ = panning_ = false;
        update();
        e->accept();
    } else
        QWidget::keyPressEvent(e);
}
void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) {
        space_ = false;
        setCursor(mode_ == Adjust ? Qt::ArrowCursor : Qt::CrossCursor);
    } else
        QWidget::keyReleaseEvent(e);
}
void Canvas::leaveEvent(QEvent *) {
    if (!drawing_ && !moving_) {
        picker_.reset();
        update();
    }
}
void Canvas::focusOutEvent(QFocusEvent *e) {
    space_ = false;
    drawing_ = moving_ = panning_ = false;
    update();
    QWidget::focusOutEvent(e);
}
} // namespace h2d
