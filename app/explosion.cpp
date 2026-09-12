#include "explosion.h"
#include "ui.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <exception>
namespace h2d {
namespace {
QVector<QPair<QRectF, QRectF>> movementRegions(const LayoutState &state) {
    QVector<QPair<QRectF, QRectF>> result;
    const auto read = [](QJsonObject r) {
        return QRectF(QPointF(r["x1"].toDouble(), r["y1"].toDouble()),
                      QPointF(r["x2"].toDouble(), r["y2"].toDouble()));
    };
    QJsonArray changes;
    try {
        changes = exportLayoutChanges(state);
    } catch (const std::exception &) {
        // An invalid unsaved layout must remain inspectable. Export validation
        // reports the error; visual arrows must never prevent opening the editor.
        return {};
    }
    for (const auto &value : changes) {
        const auto change = value.toObject();
        const auto from = read(change["from"].toObject()), to = read(change["to"].toObject());
        if (QLineF(from.center(), to.center()).length() > .01)
            result.append({from, to});
    }
    return result;
}
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
void paintMovement(QPainter &p, const QPair<QRectF, QRectF> &movement,
                   double zoom, bool hovered, double phase) {
    const auto start = movement.first.center() * zoom;
    const auto finish = movement.second.center() * zoom;
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
        for (const auto &r : {movement.first, movement.second})
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

namespace {
QVector<QPointF> controlPoints(QRectF r) {
    return {r.topLeft(),     {r.center().x(), r.top()},    r.topRight(),   {r.right(), r.center().y()},
            r.bottomRight(), {r.center().x(), r.bottom()}, r.bottomLeft(), {r.left(), r.center().y()}};
}
bool inside(QRectF r, QPointF p) {
    return p.x() >= r.left() && p.x() < r.right() && p.y() >= r.top() && p.y() < r.bottom();
}
QRectF region(QPointF a, QPointF b) {
    return QRectF(a, b).normalized();
}
} // namespace
LayoutCanvas::LayoutCanvas(QImage original, LayoutState state, QWidget *parent)
    : QWidget(parent), original_(std::move(original)), state_(std::move(state)) {
    setObjectName("layoutCanvas");
    setAccessibleName("大爆炸组件画布");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(32);
    connect(hoverTimer_, &QTimer::timeout, this, [this] {
        hoverPhase_ += .13;
        update();
    });
    rebuildMovements();
    setZoom(1);
}
void LayoutCanvas::setState(LayoutState state) {
    dragging_ = drawing_ = drawingMode_ = false;
    state_ = std::move(state);
    rebuildMovements();
    before_ = {};
    undo_.clear();
    redo_.clear();
    handle_ = -1;
    setCursor(Qt::ArrowCursor);
    clearSelection();
    setZoom(zoom_);
}
void LayoutCanvas::setAnnotations(QVector<Note> notes) {
    annotations_ = std::move(notes);
    annotationState_ = state_;
    update();
}
void LayoutCanvas::setAnnotationsVisible(bool visible) {
    annotationsVisible_ = visible;
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    update();
}
void LayoutCanvas::rebuildMovements() {
    movements_ = movementRegions(state_);
    hoveredMovement_ = -1;
}
int LayoutCanvas::hitMovement(QPointF screen) const {
    if (annotationsVisible_)
        for (int i = movements_.size() - 1; i >= 0; --i)
            if (segmentDistance(screen, movements_[i].first.center() * zoom_,
                                movements_[i].second.center() * zoom_) < 8)
                return i;
    return -1;
}
void LayoutCanvas::updateAnnotationHover(QPointF screen) {
    QString nextNote;
    if (annotationsVisible_)
        for (const auto &note : displayedAnnotations())
            if (!note.isGlobal && QLineF(screen, noteAnchor(note)).length() < 17) {
                nextNote = note.id;
                break;
            }
    const int nextMovement = nextNote.isEmpty() ? hitMovement(screen) : -1;
    if (nextNote != hoveredNote_ || nextMovement != hoveredMovement_)
        hoverPhase_ = 0;
    hoveredNote_ = nextNote;
    hoveredMovement_ = nextMovement;
    if (!hoveredNote_.isEmpty() || hoveredMovement_ >= 0) {
        if (!hoverTimer_->isActive())
            hoverTimer_->start();
    } else
        hoverTimer_->stop();
}
QVector<Note> LayoutCanvas::displayedAnnotations() const {
    return annotationState_ == state_ ? annotations_ : remapNotes(annotations_, annotationState_, state_);
}
QPointF LayoutCanvas::noteAnchor(const Note &note) const {
    QPointF anchor = QPointF(note.isPoint ? note.point : note.rect.topLeft()) * zoom_;
    if (!note.isPoint)
        anchor += QPointF(18, 18);
    anchor.setX(std::clamp(anchor.x(), 14.0, std::max(14.0, width() - 14.0)));
    anchor.setY(std::clamp(anchor.y(), 14.0, std::max(14.0, height() - 14.0)));
    return anchor;
}
void LayoutCanvas::annotateSelection() {
    if (selected_.isEmpty())
        return;
    const QRect area = selectionBounds().toAlignedRect().intersected(QRect(QPoint(0, 0), state_.canvas));
    if (!area.isEmpty())
        emit annotationRequested(area, mapToGlobal((QPointF(area.center()) * zoom_).toPoint()));
}
void LayoutCanvas::cancelInteraction() {
    if (dragging_) {
        state_ = before_;
        rebuildMovements();
    }
    dragging_ = drawing_ = drawingMode_ = false;
    handle_ = -1;
    setCursor(Qt::ArrowCursor);
    clearSelection();
}
void LayoutCanvas::setZoom(double value) {
    zoom_ = std::clamp(value, .03, 4.0);
    setFixedSize(std::max(1, qRound(state_.canvas.width() * zoom_)),
                 std::max(1, qRound(state_.canvas.height() * zoom_)));
    update();
}
QPointF LayoutCanvas::pixel(QPointF p) const {
    return {std::clamp(p.x() / zoom_, 0.0, double(state_.canvas.width())),
            std::clamp(p.y() / zoom_, 0.0, double(state_.canvas.height()))};
}
void LayoutCanvas::setGuides(bool visible) {
    guides_ = visible;
    update();
}
void LayoutCanvas::select(QString id) {
    selected_ = std::move(id);
    hover_.clear();
    dragging_ = drawing_ = false;
    emit selectionChanged();
    update();
    emit hintChanged(selected_.isEmpty()
                         ? "悬停滚轮切换所有区域 · 单击确认 · 拖动可手动分块"
                         : "拖动移动 · 边缘调整宽高 · 角点等比缩放 · 滚轮缩放 · Esc 返回选块");
}
void LayoutCanvas::clearSelection() {
    if (dragging_) {
        state_ = before_;
        rebuildMovements();
    }
    drawing_ = dragging_ = false;
    hover_.clear();
    choices_.clear();
    hoverAnchor_ = {-1000, -1000};
    select({});
}
void LayoutCanvas::setDrawing(bool enabled) {
    drawingMode_ = enabled;
    clearSelection();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    emit hintChanged(enabled ? "拖动画框创建一个可调整的区域" : "悬停滚轮切换所有区域 · 单击确认");
}
void LayoutCanvas::updateHover(QPointF point) {
    auto previous = hover_;
    auto next = layoutChoices(state_, point);
    const bool nearby = QLineF(hoverAnchor_, point).length() * zoom_ <= 8;
    choices_ = std::move(next);
    level_ = 0;
    if (nearby)
        for (int i = 0; i < choices_.size(); ++i)
            if (choices_[i].id == previous) {
                level_ = i;
                break;
            }
    if (!nearby)
        hoverAnchor_ = point;
    hover_ = choices_.isEmpty() ? QString() : choices_[level_].id;
    if (selected_.isEmpty()) {
        emit hintChanged(choices_.isEmpty() ? "拖动可手动划分区域"
                                            : QString("%1 · %2 / %3 · 滚轮切换范围，单击确认")
                                                  .arg(choices_[level_].label)
                                                  .arg(level_ + 1)
                                                  .arg(choices_.size()));
    }
}
void LayoutCanvas::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    paintTransparency(p, event->rect());
    p.save();
    p.scale(zoom_, zoom_);
    paintLayout(p, original_, state_);
    p.restore();
    p.setRenderHint(QPainter::Antialiasing);
    auto screenRect = [&](QRectF r) {
        return QRectF(r.x() * zoom_, r.y() * zoom_, r.width() * zoom_, r.height() * zoom_);
    };
    if (guides_ && selected_.isEmpty()) {
        p.setPen(QPen(QColor(0, 122, 255, 65), .7));
        p.setBrush(Qt::NoBrush);
        for (const auto &group : state_.groups)
            p.drawRect(screenRect(layoutBounds(state_, group.id)));
    }
    QString active = selected_.isEmpty() ? hover_ : selected_;
    if (!active.isEmpty()) {
        auto r = screenRect(layoutBounds(state_, active));
        p.setPen(QPen(accent(), 1.5, selected_.isEmpty() ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(QColor(0, 122, 255, 12));
        p.drawRect(r);
        if (!selected_.isEmpty()) {
            p.setBrush(Qt::white);
            for (auto point : controlPoints(r))
                p.drawRect(QRectF(point - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
    if (drawing_) {
        p.setPen(QPen(accent(), 1.5, Qt::DashLine));
        p.setBrush(QColor(0, 122, 255, 20));
        p.drawRect(screenRect(region(press_, end_)));
    }
    if (annotationsVisible_)
        for (int i = 0; i < movements_.size(); ++i)
            paintMovement(p, movements_[i], zoom_, i == hoveredMovement_, hoverPhase_);
    int number = 0;
    for (const auto &note : displayedAnnotations()) {
        ++number;
        if (!annotationsVisible_ || note.isGlobal)
            continue;
        const bool hovered = note.id == hoveredNote_;
        const double pulse = hovered ? .5 + .5 * std::sin(hoverPhase_) : 0;
        if (!note.isPoint) {
            p.setPen(QPen(accent(), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRect(screenRect(note.rect));
        }
        const QPointF anchor = noteAnchor(note);
        if (hovered) {
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
    }
}
void LayoutCanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) {
        setDrawing(false);
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        emit zoomRequested(std::abs(zoom_ - 1) < .01 ? 0 : 1);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    setFocus();
    const auto notes = displayedAnnotations();
    for (auto it = notes.crbegin(); it != notes.crend(); ++it) {
        if (annotationsVisible_ && !it->isGlobal && QLineF(event->position(), noteAnchor(*it)).length() < 17) {
            emit noteEditRequested(it->id, event->globalPosition().toPoint());
            return;
        }
    }
    const int movement = hitMovement(event->position());
    if (movement >= 0) {
        emit movementAnnotationRequested(movements_[movement].first, movements_[movement].second);
        return;
    }
    press_ = end_ = pixel(event->position());
    handle_ = -1;
    if (drawingMode_) {
        drawing_ = true;
        return;
    }
    if (!selected_.isEmpty()) {
        auto bounds = selectionBounds();
        auto points = controlPoints(bounds);
        for (int i = 0; i < points.size(); ++i)
            if (QLineF(event->position(), points[i] * zoom_).length() < 9) {
                handle_ = i;
                break;
            }
        if (handle_ >= 0 || inside(bounds, press_)) {
            before_ = state_;
            initial_ = bounds;
            dragging_ = true;
            return;
        }
    }
    updateHover(press_);
    if (!hover_.isEmpty()) {
        QString chosen = hover_;
        select(chosen);
        before_ = state_;
        initial_ = selectionBounds();
        dragging_ = true;
    } else {
        select({});
        drawing_ = true;
    }
}
void LayoutCanvas::mouseMoveEvent(QMouseEvent *event) {
    end_ = pixel(event->position());
    if (dragging_) {
        state_ = before_;
        auto destination = handle_ < 0
                               ? constrainLayoutRect(initial_.translated(end_ - press_), state_.canvas)
                               : resizeLayoutRect(initial_, end_ - press_, handle_, state_.canvas);
        transformLayoutGroup(state_, selected_, destination);
        emit selectionChanged();
    } else if (!drawing_) {
        updateAnnotationHover(event->position());
        updateHover(end_);
        int handle = -1;
        if (!selected_.isEmpty()) {
            auto points = controlPoints(selectionBounds());
            for (int i = 0; i < points.size(); ++i)
                if (QLineF(event->position(), points[i] * zoom_).length() < 9) {
                    handle = i;
                    break;
                }
        }
        Qt::CursorShape shape = drawingMode_ ? Qt::CrossCursor : Qt::ArrowCursor;
        if (handle == 0 || handle == 4)
            shape = Qt::SizeFDiagCursor;
        else if (handle == 2 || handle == 6)
            shape = Qt::SizeBDiagCursor;
        else if (handle == 1 || handle == 5)
            shape = Qt::SizeVerCursor;
        else if (handle == 3 || handle == 7)
            shape = Qt::SizeHorCursor;
        else if (!selected_.isEmpty() && inside(selectionBounds(), end_))
            shape = Qt::SizeAllCursor;
        if (!hoveredNote_.isEmpty() || hoveredMovement_ >= 0)
            shape = Qt::PointingHandCursor;
        setCursor(shape);
    }
    update();
}
void LayoutCanvas::commit(const LayoutState &before) {
    if (state_ == before)
        return;
    undo_.append(before);
    if (undo_.size() > 60)
        undo_.removeFirst();
    redo_.clear();
    rebuildMovements();
    emit changed();
    emit selectionChanged();
    update();
}
void LayoutCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton)
        return;
    if (dragging_) {
        dragging_ = false;
        commit(before_);
    } else if (drawing_) {
        drawing_ = false;
        end_ = pixel(event->position());
        if (QLineF(press_, end_).length() * zoom_ > 5) {
            auto before = state_;
            try {
                auto id = addLayoutRegion(state_, region(press_, end_));
                if (!id.isEmpty()) {
                    drawingMode_ = false;
                    select(id);
                    commit(before);
                    annotateSelection();
                } else
                    emit hintChanged("这个区域没有图像内容，请框选已有组件");
            } catch (const std::exception &e) {
                emit hintChanged(QString::fromUtf8(e.what()));
            }
        }
    }
    update();
}
void LayoutCanvas::transformSelection(QRectF destination) {
    if (selected_.isEmpty())
        return;
    auto before = state_;
    transformLayoutGroup(state_, selected_, destination);
    commit(before);
}
void LayoutCanvas::wheelEvent(QWheelEvent *event) {
    if (dragging_ || drawing_)
        return;
    const int delta = event->angleDelta().y();
    if (!delta)
        return;
    const QPointF point = pixel(event->position());
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        emit zoomRequested(zoom_ * (delta > 0 ? 1.12 : 1 / 1.12));
    } else if (!selected_.isEmpty() && inside(selectionBounds(), point)) {
        transformSelection(scaleLayoutRect(selectionBounds(), delta > 0 ? 1.05 : 1 / 1.05, state_.canvas));
    } else {
        updateHover(point);
        const bool specific = std::any_of(choices_.cbegin(), choices_.cend(), [&](const LayoutChoice &choice) {
            for (const auto &group : state_.groups)
                if (group.id == choice.id)
                    return group.origin != "canvas" &&
                           !(group.label == "色块区域" && backgroundBounds(choice.bounds, state_.canvas));
            return false;
        });
        if (specific) {
            level_ = std::clamp(level_ + (delta > 0 ? 1 : -1), 0, int(choices_.size()) - 1);
            hover_ = choices_[level_].id;
            emit hintChanged(QString("%1 · %2 / %3 · 滚轮切换范围，单击确认")
                                 .arg(choices_[level_].label)
                                 .arg(level_ + 1)
                                 .arg(choices_.size()));
        } else
            emit zoomRequested(zoom_ * (delta > 0 ? 1.12 : 1 / 1.12));
    }
    event->accept();
    update();
}
void LayoutCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        setDrawing(false);
        event->accept();
        return;
    }
    QPointF delta;
    if (event->key() == Qt::Key_Left)
        delta = {-1, 0};
    else if (event->key() == Qt::Key_Right)
        delta = {1, 0};
    else if (event->key() == Qt::Key_Up)
        delta = {0, -1};
    else if (event->key() == Qt::Key_Down)
        delta = {0, 1};
    if (!delta.isNull() && !selected_.isEmpty()) {
        transformSelection(selectionBounds().translated(delta));
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void LayoutCanvas::leaveEvent(QEvent *) {
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    if (!dragging_ && !drawing_) {
        hover_.clear();
        update();
    }
}
void LayoutCanvas::focusOutEvent(QFocusEvent *event) {
    hoveredNote_.clear();
    hoveredMovement_ = -1;
    hoverTimer_->stop();
    // A lost release event must not keep a preview attached to the pointer.
    // Ordinary focus changes to the inspector preserve the selected component.
    if (dragging_ || drawing_)
        cancelInteraction();
    QWidget::focusOutEvent(event);
}
void LayoutCanvas::undo() {
    if (dragging_ || drawing_) {
        clearSelection();
        return;
    }
    if (undo_.isEmpty())
        return;
    redo_.append(state_);
    state_ = undo_.takeLast();
    rebuildMovements();
    clearSelection();
    emit changed();
}
void LayoutCanvas::redo() {
    if (dragging_ || drawing_) {
        clearSelection();
        return;
    }
    if (redo_.isEmpty())
        return;
    undo_.append(state_);
    state_ = redo_.takeLast();
    rebuildMovements();
    clearSelection();
    emit changed();
}
LayoutInspector::LayoutInspector(LayoutCanvas *canvas, QWidget *parent) : QWidget(parent), canvas_(canvas) {
    setObjectName("layoutInspector");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto side = new QVBoxLayout(this);
    side->setContentsMargins(12, 10, 12, 10);
    side->setSpacing(7);
    auto headingRow = new QHBoxLayout;
    headingRow->setSpacing(5);
    auto heading = new QLabel("组件调整", this);
    heading->setProperty("sectionTitle", true);
    headingRow->addWidget(heading);
    headingRow->addStretch();
    auto guides = new QCheckBox("显示分解框", this);
    guides->setObjectName("layoutGuides");
    guides->setToolTip("显示分解框");
    guides->setChecked(true);
    headingRow->addWidget(guides);
    annotate_ = iconButton("plus", "为当前组件添加批注", this);
    annotate_->setObjectName("annotateComponent");
    annotate_->setFixedSize(26, 26);
    headingRow->addWidget(annotate_);
    clear_ = iconButton("close", "取消选择", this);
    clear_->setObjectName("clearLayoutSelection");
    clear_->setFixedSize(26, 26);
    headingRow->addWidget(clear_);
    side->addLayout(headingRow);
    selection_ = mutedLabel("单击选择一个区域", this);
    selection_->setStyleSheet("font-size:11px;");
    side->addWidget(selection_);
    fieldsPanel_ = new QWidget(this);
    auto grid = new QGridLayout(fieldsPanel_);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(5);
    QStringList labels{"X", "Y", "宽", "高", "缩放"},
        names{"layoutX", "layoutY", "layoutWidth", "layoutHeight", "layoutScale"};
    for (int i = 0; i < 5; ++i) {
        auto cell = new QWidget(fieldsPanel_);
        auto row = new QHBoxLayout(cell);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(5);
        auto label = new QLabel(labels[i], cell);
        label->setFixedWidth(i == 4 ? 30 : 14);
        label->setStyleSheet("font-size:11px;");
        row->addWidget(label);
        auto input = new QDoubleSpinBox(cell);
        input->setObjectName(names[i]);
        input->setDecimals(2);
        input->setRange(i < 2 ? 0 : .01, i == 4 ? 100000 : 32767);
        input->setSingleStep(i == 4 ? 5 : 1);
        input->setKeyboardTracking(false);
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setSuffix(i == 4 ? " %" : " px");
        input->setFixedHeight(28);
        input->setMinimumWidth(70);
        input->setStyleSheet("font-size:11px;padding:3px 5px;");
        fields_.append(input);
        row->addWidget(input, 1);
        grid->addWidget(cell, i / 2, i % 2);
        connect(input, &QDoubleSpinBox::valueChanged, this, [this, input] {
            if (!updating_)
                input->setProperty("edited", true);
        });
        connect(input, &QDoubleSpinBox::editingFinished, this, [this, i] { applyField(i); });
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    side->addWidget(fieldsPanel_);
    connect(guides, &QCheckBox::toggled, canvas_, &LayoutCanvas::setGuides);
    connect(clear_, &QPushButton::clicked, canvas_, &LayoutCanvas::clearSelection);
    connect(annotate_, &QPushButton::clicked, canvas_, &LayoutCanvas::annotateSelection);
    connect(canvas_, &LayoutCanvas::changed, this, &LayoutInspector::refresh);
    connect(canvas_, &LayoutCanvas::selectionChanged, this, &LayoutInspector::refresh);
    refresh();
}
void LayoutInspector::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        canvas_->cancelInteraction();
        canvas_->setFocus();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void LayoutInspector::refresh() {
    updating_ = true;
    const auto id = canvas_->selected();
    const auto r = canvas_->selectionBounds();
    if (id != fieldSelection_) {
        fieldSelection_ = id;
        scaleBase_ = r;
    }
    for (auto input : fields_)
        input->setEnabled(!id.isEmpty());
    clear_->setEnabled(!id.isEmpty());
    annotate_->setEnabled(!id.isEmpty());
    fieldsPanel_->setVisible(!id.isEmpty());
    selection_->setVisible(!id.isEmpty());
    clear_->setVisible(!id.isEmpty());
    annotate_->setVisible(!id.isEmpty());
    selection_->setText("单击选择一个区域");
    for (const auto &group : canvas_->state().groups)
        if (group.id == id) {
            selection_->setText(group.label);
            break;
        }
    if (!id.isEmpty()) {
        fields_[0]->setValue(r.x());
        fields_[1]->setValue(r.y());
        fields_[2]->setValue(r.width());
        fields_[3]->setValue(r.height());
        fields_[4]->setValue(scaleBase_.width() > 0 ? r.width() / scaleBase_.width() * 100 : 100);
    }
    for (auto input : fields_)
        input->setProperty("edited", false);
    updating_ = false;
}
void LayoutInspector::applyField(int field) {
    if (updating_ || canvas_->selected().isEmpty() || !fields_[field]->property("edited").toBool())
        return;
    auto r = canvas_->selectionBounds();
    double value = fields_[field]->value();
    if (field == 0)
        r.moveLeft(value);
    if (field == 1)
        r.moveTop(value);
    if (field == 2)
        r.setWidth(value);
    if (field == 3)
        r.setHeight(value);
    if (field == 4) {
        const double current = r.width() / scaleBase_.width() * 100;
        r = scaleLayoutRect(r, value / std::max(.001, current), canvas_->state().canvas);
    }
    canvas_->transformSelection(constrainLayoutRect(r, canvas_->state().canvas));
    refresh();
}
ExplosionWave::ExplosionWave(QWidget *parent) : QWidget(parent), animation_(new QVariantAnimation(this)) {
    setObjectName("explosionWave");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    animation_->setObjectName("explosionWaveAnimation");
    animation_->setDuration(1760);
    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);
    animation_->setEasingCurve(QEasingCurve::Linear);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress_ = value.toReal();
        update();
    });
    connect(animation_, &QVariantAnimation::finished, this, &QWidget::hide);
    hide();
}
void ExplosionWave::start() {
    animation_->stop();
    progress_ = 0;
    show();
    raise();
    animation_->start();
}
void ExplosionWave::hideEvent(QHideEvent *event) {
    animation_->stop();
    QWidget::hideEvent(event);
}
void ExplosionWave::paintEvent(QPaintEvent *) {
    if (width() < 2 || height() < 2 || progress_ <= 0 || progress_ >= 1)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(rect());
    QPainterPath outer;
    outer.addRoundedRect(bounds, 8, 8);
    p.setClipPath(outer);
    const auto smooth = [](qreal value) {
        const qreal t = std::clamp(value, 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    const qreal envelope = smooth(progress_ / .09) * smooth((1 - progress_) / .28);
    const qreal strength = envelope * (isDarkTheme() ? .82 : 1.0);
    static const QEasingCurve travelCurve = [] {
        QEasingCurve curve(QEasingCurve::BezierSpline);
        curve.addCubicBezierSegment(QPointF(.45, .02), QPointF(.25, 1), QPointF(1, 1));
        return curve;
    }();
    const QPointF origin(bounds.right() - 8, bounds.top() + 8);
    const qreal diagonal = QLineF(origin, bounds.bottomLeft()).length();
    const qreal travel = travelCurve.valueForProgress(progress_);
    const qreal radius = 9 + travel * (diagonal + 120);
    // Keep the initial drop circular and legible before it grows into a broad ripple.
    const qreal spread = std::min(radius * .24, std::clamp(std::min(width(), height()) * .065, 24.0, 58.0));
    const qreal lag = spread * 1.4 + 6;
    const qreal extent = radius + spread * 3.5;
    const auto gaussian = [](qreal x) { return std::exp(-x * x * .5); };
    const auto tint = [](qreal value) {
        static const QVector<QColor> colors{QColor(250, 211, 223), QColor(210, 164, 245),
                                     QColor(153, 184, 255), QColor(148, 234, 245)};
        const qreal position = std::clamp(value, 0.0, 1.0) * 3;
        const int first = std::min(int(position), 2);
        const qreal mix = position - first;
        return QColor(qRound(colors[first].red() + (colors[first + 1].red() - colors[first].red()) * mix),
                      qRound(colors[first].green() + (colors[first + 1].green() - colors[first].green()) * mix),
                      qRound(colors[first].blue() + (colors[first + 1].blue() - colors[first].blue()) * mix));
    };
    // A continuous radial gradient has constant-width soft edges at every radius.
    // Qt rasterizes it in one pass; no per-frame stacks or full-size blur surfaces.
    QRadialGradient ripple(origin, extent);
    ripple.setColorAt(0, Qt::transparent);
    const qreal inner = std::max(0.0, radius - lag - spread * 3.5);
    for (int i = 0; i <= 96; ++i) {
        const qreal distance = inner + (extent - inner) * i / 96;
        const qreal mainAlpha = .24 * gaussian((distance - radius) / spread);
        const qreal echoAlpha = .075 * gaussian((distance - radius + lag) / (spread * .43));
        QColor color = tint(.5 + (distance - radius) / (spread * 3.5));
        color.setAlphaF(mainAlpha + echoAlpha * (1 - mainAlpha));
        ripple.setColorAt(distance / extent, color);
    }
    ripple.setColorAt(1, Qt::transparent);
    p.setOpacity(strength);
    p.fillRect(bounds, ripple);
    p.setBrush(Qt::NoBrush);
    QLinearGradient pearl(origin + QPointF(-radius, 0), origin + QPointF(0, radius));
    pearl.setColorAt(0, QColor(237, 226, 255, 90));
    pearl.setColorAt(.5, QColor(205, 228, 255, 115));
    pearl.setColorAt(1, QColor(212, 251, 255, 100));
    p.setPen(QPen(QBrush(pearl), 1.1));
    p.drawEllipse(origin, radius, radius);
    if (radius > lag + 2) {
        p.setOpacity(strength * .32);
        p.setPen(QPen(QBrush(pearl), .8));
        p.drawEllipse(origin, radius - lag, radius - lag);
    }

    // The border is a thin liquid film. Its meniscus uses the SAME distance field
    // as the circular front, so each contact remains attached around all four edges.
    const qreal edgeSpread = std::clamp(spread * 1.4, 12.0, 78.0);
    const qreal amplitude = std::min(16.0, std::min(width(), height()) * .055) * smooth(radius / 65);
    const auto inset = [&](QPointF point) {
        const qreal q = (QLineF(origin, point).length() - radius) / edgeSpread;
        const qreal crest = amplitude * gaussian(q * 1.8);
        const qreal behind = std::max(0.0, -q);
        const qreal rebound = q < 0 ? amplitude * .34 * std::sin(behind * 4.5) * std::exp(-behind * 1.8) : 0;
        return std::max(1.2, 2.0 + envelope * (crest + rebound));
    };
    const qreal corner = std::min(22.0, std::min(width(), height()) * .12);
    const qreal right = bounds.right(), bottom = bounds.bottom();
    const int horizontalSteps = std::max(1, int((right - 2 * corner) / 5));
    const int verticalSteps = std::max(1, int((bottom - 2 * corner) / 5));
    QPainterPath meniscus;
    meniscus.moveTo(corner, inset(QPointF(corner, 2)));
    for (int i = 1; i <= horizontalSteps; ++i) {
        const qreal x = corner + (right - 2 * corner) * i / horizontalSteps;
        meniscus.lineTo(x, inset(QPointF(x, 2)));
    }
    meniscus.quadTo(right - 2, 2, right - inset(QPointF(right - 2, corner)), corner);
    for (int i = 1; i <= verticalSteps; ++i) {
        const qreal y = corner + (bottom - 2 * corner) * i / verticalSteps;
        meniscus.lineTo(right - inset(QPointF(right - 2, y)), y);
    }
    meniscus.quadTo(right - 2, bottom - 2, right - corner, bottom - inset(QPointF(right - corner, bottom - 2)));
    for (int i = 1; i <= horizontalSteps; ++i) {
        const qreal x = right - corner - (right - 2 * corner) * i / horizontalSteps;
        meniscus.lineTo(x, bottom - inset(QPointF(x, bottom - 2)));
    }
    meniscus.quadTo(2, bottom - 2, inset(QPointF(2, bottom - corner)), bottom - corner);
    for (int i = 1; i <= verticalSteps; ++i) {
        const qreal y = bottom - corner - (bottom - 2 * corner) * i / verticalSteps;
        meniscus.lineTo(inset(QPointF(2, y)), y);
    }
    meniscus.quadTo(2, 2, corner, inset(QPointF(corner, 2)));
    meniscus.closeSubpath();

    const qreal rimExtent = radius + edgeSpread * 3;
    QRadialGradient contact(origin, rimExtent);
    contact.setColorAt(0, Qt::transparent);
    const qreal rimInner = std::max(0.0, radius - edgeSpread * 3);
    for (int i = 0; i <= 64; ++i) {
        const qreal distance = rimInner + (rimExtent - rimInner) * i / 64;
        const qreal q = (distance - radius) / edgeSpread;
        QColor color = tint(.58 + q * .23);
        color.setAlphaF(.72 * gaussian(q * 1.2));
        contact.setColorAt(distance / rimExtent, color);
    }
    contact.setColorAt(1, Qt::transparent);
    QPainterPath film = outer;
    film.addPath(meniscus);
    film.setFillRule(Qt::OddEvenFill);
    p.setOpacity(strength * .56);
    p.fillPath(film, contact);
    p.setBrush(Qt::NoBrush);
    for (const auto &[lineWidth, alpha] :
         {std::pair<qreal, qreal>{10, .065}, {4, .14}, {1.15, .85}}) {
        p.setOpacity(strength * alpha);
        p.setPen(QPen(QBrush(contact), lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(meniscus);
    }
}

} // namespace h2d
