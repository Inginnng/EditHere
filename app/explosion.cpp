#include "explosion.h"
#include "ui.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace h2d {
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
    setZoom(1);
}
void LayoutCanvas::setState(LayoutState state) {
    dragging_ = drawing_ = drawingMode_ = false;
    state_ = std::move(state);
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
    if (dragging_)
        state_ = before_;
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
    if (dragging_)
        state_ = before_;
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
void LayoutCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#ffffff"));
    const int check = 14;
    for (int y = 0; y < height(); y += check)
        for (int x = 0; x < width(); x += check)
            if ((x / check + y / check) % 2 == 0)
                p.fillRect(x, y, check, check, QColor("#e7e8ed"));
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
    int number = 0;
    for (const auto &note : displayedAnnotations()) {
        if (!note.isPoint) {
            p.setPen(QPen(accent(), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRect(screenRect(note.rect));
        }
        const QPointF anchor = noteAnchor(note);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(accent());
        p.drawEllipse(anchor, 13, 13);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(anchor.x() - 13, anchor.y() - 13, 26, 26), Qt::AlignCenter,
                   QString::number(++number));
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
        if (QLineF(event->position(), noteAnchor(*it)).length() < 17) {
            emit noteEditRequested(it->id, event->globalPosition().toPoint());
            return;
        }
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
        for (const auto &note : displayedAnnotations())
            if (QLineF(event->position(), noteAnchor(note)).length() < 17) {
                shape = Qt::PointingHandCursor;
                break;
            }
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
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        emit zoomRequested(zoom_ * (delta > 0 ? 1.12 : 1 / 1.12));
    } else if (!selected_.isEmpty()) {
        transformSelection(scaleLayoutRect(selectionBounds(), delta > 0 ? 1.05 : 1 / 1.05, state_.canvas));
    } else {
        updateHover(pixel(event->position()));
        if (!choices_.isEmpty()) {
            level_ = std::clamp(level_ + (delta > 0 ? 1 : -1), 0, int(choices_.size()) - 1);
            hover_ = choices_[level_].id;
            emit hintChanged(QString("%1 · %2 / %3 · 滚轮切换范围，单击确认")
                                 .arg(choices_[level_].label)
                                 .arg(level_ + 1)
                                 .arg(choices_.size()));
        }
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
    if (!dragging_ && !drawing_) {
        hover_.clear();
        update();
    }
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
    clearSelection();
    emit changed();
}
LayoutInspector::LayoutInspector(LayoutCanvas *canvas, QWidget *parent) : QWidget(parent), canvas_(canvas) {
    setObjectName("layoutInspector");
    setStyleSheet("QWidget#layoutInspector {background:white;border-left:1px solid #e5e5ea;}");
    auto side = new QVBoxLayout(this);
    side->setContentsMargins(18, 18, 18, 16);
    side->setSpacing(12);
    auto heading = new QLabel("组件调整", this);
    heading->setStyleSheet("font-weight:600;font-size:15px;");
    side->addWidget(heading);
    auto tools = new QHBoxLayout;
    manual_ = textButton("手动分块", false, this);
    manual_->setCheckable(true);
    manual_->setObjectName("manualRegion");
    tools->addWidget(manual_);
    auto guides = new QCheckBox("显示区域", this);
    guides->setObjectName("layoutGuides");
    guides->setChecked(true);
    tools->addWidget(guides);
    side->addLayout(tools);
    selection_ = mutedLabel("单击选择一个区域", this);
    selection_->setWordWrap(true);
    selection_->setMinimumHeight(32);
    side->addWidget(selection_);
    auto grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    QStringList labels{"X", "Y", "宽度", "高度", "等比缩放"},
        names{"layoutX", "layoutY", "layoutWidth", "layoutHeight", "layoutScale"};
    for (int i = 0; i < 5; ++i) {
        auto input = new QDoubleSpinBox(this);
        input->setObjectName(names[i]);
        input->setDecimals(2);
        input->setRange(i < 2 ? 0 : .01, i == 4 ? 100000 : 32767);
        input->setSingleStep(i == 4 ? 5 : 1);
        input->setKeyboardTracking(false);
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setSuffix(i == 4 ? " %" : " px");
        input->setMinimumHeight(34);
        input->setMinimumWidth(112);
        input->setStyleSheet(
            "QDoubleSpinBox {background:white;border:1px solid #dddde4;border-radius:7px;padding:4px;}"
            "QDoubleSpinBox:focus {border-color:#007aff;}"
            "QDoubleSpinBox:disabled {color:#a0a0aa;background:#f5f5f7;}");
        fields_.append(input);
        grid->addWidget(new QLabel(labels[i], this), i, 0);
        grid->addWidget(input, i, 1);
        connect(input, &QDoubleSpinBox::valueChanged, this, [this, input] {
            if (!updating_)
                input->setProperty("edited", true);
        });
        connect(input, &QDoubleSpinBox::editingFinished, this, [this, i] { applyField(i); });
    }
    grid->setColumnStretch(1, 1);
    side->addLayout(grid);
    annotate_ = textButton("添加批注", true, this);
    annotate_->setObjectName("annotateComponent");
    annotate_->setStyleSheet("QPushButton:disabled {background:#eeeeF1;color:#b9b9c0;}");
    annotate_->setToolTip("为当前组件的位置和范围添加批注");
    side->addWidget(annotate_);
    clear_ = textButton("取消选择", false, this);
    clear_->setObjectName("clearLayoutSelection");
    side->addWidget(clear_);
    auto help = mutedLabel(
        "悬停滚轮选范围，选中滚轮缩放。\n拖边调整宽高，拖角等比缩放。\n点击编号编辑批注，Esc 取消选择。",
        this);
    help->setWordWrap(true);
    side->addSpacing(4);
    side->addWidget(help);
    side->addStretch();
    connect(manual_, &QPushButton::clicked, canvas_, &LayoutCanvas::setDrawing);
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
    manual_->setChecked(canvas_->drawingMode());
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
    animation_->setDuration(1100);
    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);
    animation_->setEasingCurve(QEasingCurve::InOutSine);
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
    if (width() < 1 || height() < 1)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal opacity = std::clamp(std::min(progress_ / .12, (1 - progress_) / .2), 0.0, 1.0);
    p.setOpacity(opacity);
    p.setClipRect(rect());
    QConicalGradient rim(rect().center(), 35 - progress_ * 280);
    rim.setColorAt(0, QColor(80, 161, 255, 65));
    rim.setColorAt(.25, QColor(183, 119, 255, 65));
    rim.setColorAt(.5, QColor(255, 124, 168, 65));
    rim.setColorAt(.75, QColor(97, 231, 232, 65));
    rim.setColorAt(1, QColor(80, 161, 255, 65));
    p.setPen(QPen(QBrush(rim), 5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5), 10, 10);

    // The wave's normal runs from the upper-right corner toward the lower-left.
    const qreal span = std::hypot(qreal(width()), qreal(height())) + 160;
    const qreal travel = (width() + height()) / std::sqrt(2.0);
    const qreal position = -75 + progress_ * (travel + 150);
    p.translate(width(), 0);
    p.rotate(135);
    QPainterPath wave;
    wave.moveTo(position - 16, -span);
    wave.cubicTo(position - 44, -span * .35, position + 38, span * .3, position - 12, span);
    QLinearGradient spectrum(0, -span * .65, 0, span * .65);
    spectrum.setColorAt(0, QColor("#6fe7f7"));
    spectrum.setColorAt(.26, QColor("#7fa3ff"));
    spectrum.setColorAt(.5, QColor("#ce91ff"));
    spectrum.setColorAt(.74, QColor("#ff9cbc"));
    spectrum.setColorAt(1, QColor("#ffe2a9"));
    p.setBrush(Qt::NoBrush);
    for (const auto &[width, alpha] :
         {std::pair<qreal, qreal>{68, .035}, {38, .07}, {18, .16}, {6, .5}, {1.8, .88}}) {
        p.setOpacity(opacity * alpha);
        p.setPen(QPen(QBrush(spectrum), width, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(wave);
    }
}
} // namespace h2d
