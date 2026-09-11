#include "explosion.h"
#include "ui.h"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
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
ExplosionDialog::ExplosionDialog(const QImage &original, LayoutState state, QWidget *parent)
    : QDialog(parent) {
    setObjectName("explosionDialog");
    setWindowTitle("大爆炸 · 调整组件");
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint | Qt::WindowTitleHint);
    setMinimumSize(800, 500);
    resize(1220, 820);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);
    auto header = new QHBoxLayout;
    auto title = new QLabel("大爆炸", this);
    title->setStyleSheet("font-size:20px;font-weight:600;");
    header->addWidget(title);
    header->addSpacing(12);
    count_ = mutedLabel({}, this);
    header->addWidget(count_);
    header->addStretch();
    manual_ = textButton("手动分块", false, this);
    manual_->setCheckable(true);
    manual_->setObjectName("manualRegion");
    header->addWidget(manual_);
    undo_ = iconButton("undo", "撤销", this);
    redo_ = iconButton("redo", "重做", this);
    header->addWidget(undo_);
    header->addWidget(redo_);
    auto guides = new QCheckBox("显示区域", this);
    guides->setChecked(true);
    header->addWidget(guides);
    root->addLayout(header);
    auto middle = new QHBoxLayout;
    scroll_ = new QScrollArea(this);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setStyleSheet("QScrollArea {background:#ededf1;border-radius:12px;}");
    canvas_ = new LayoutCanvas(original, std::move(state));
    scroll_->setWidget(canvas_);
    middle->addWidget(scroll_, 1);
    auto panel = new QWidget(this);
    panel->setFixedWidth(244);
    panel->setStyleSheet("QWidget {background:#fbfbfd;}");
    auto side = new QVBoxLayout(panel);
    side->setContentsMargins(14, 12, 4, 12);
    auto heading = new QLabel("组件属性", panel);
    heading->setStyleSheet("font-weight:600;font-size:14px;");
    side->addWidget(heading);
    selection_ = mutedLabel("单击选择一个区域", panel);
    selection_->setWordWrap(true);
    side->addWidget(selection_);
    auto grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    QStringList labels{"X", "Y", "宽度", "高度", "等比缩放"},
        names{"layoutX", "layoutY", "layoutWidth", "layoutHeight", "layoutScale"};
    for (int i = 0; i < 5; ++i) {
        auto input = new QDoubleSpinBox(panel);
        input->setObjectName(names[i]);
        input->setDecimals(2);
        input->setRange(i < 2 ? 0 : .01, i == 4 ? 100000 : 32767);
        input->setSingleStep(i == 4 ? 5 : 1);
        input->setKeyboardTracking(false);
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setSuffix(i == 4 ? " %" : " px");
        input->setMinimumHeight(34);
        input->setStyleSheet(
            "QDoubleSpinBox {background:white;border:1px solid #ddddE4;border-radius:7px;padding:4px;}");
        fields_.append(input);
        grid->addWidget(new QLabel(labels[i], panel), i, 0);
        grid->addWidget(input, i, 1);
        connect(input, &QDoubleSpinBox::valueChanged, this, [this, input] {
            if (!updating_)
                input->setProperty("edited", true);
        });
        connect(input, &QDoubleSpinBox::editingFinished, this, [this, i] { applyField(i); });
    }
    side->addLayout(grid);
    clear_ = textButton("取消选择", false, panel);
    clear_->setObjectName("clearLayoutSelection");
    side->addWidget(clear_);
    auto help = mutedLabel(
        "悬停 + 滚轮：切换范围\n选中 + 滚轮：缩放组件\n\n拖边缘：调整宽高\n拖角点：等比例缩放\n方向键：移动 "
        "1 px\n\n空白棋盘格代表透明区域。",
        panel);
    help->setWordWrap(true);
    side->addWidget(help);
    side->addStretch();
    middle->addWidget(panel);
    root->addLayout(middle, 1);
    hint_ = mutedLabel("悬停滚轮切换所有区域 · 单击确认", this);
    hint_->setWordWrap(true);
    root->addWidget(hint_);
    auto footer = new QHBoxLayout;
    auto fitButton = textButton("适应画布", false, this);
    footer->addWidget(fitButton);
    footer->addStretch();
    auto cancel = textButton("取消", false, this), done = textButton("完成调整", true, this);
    done->setObjectName("applyExplosion");
    footer->addWidget(cancel);
    footer->addWidget(done);
    root->addLayout(footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(fitButton, &QPushButton::clicked, this, &ExplosionDialog::fit);
    connect(manual_, &QPushButton::clicked, canvas_, &LayoutCanvas::setDrawing);
    connect(guides, &QCheckBox::toggled, canvas_, &LayoutCanvas::setGuides);
    connect(clear_, &QPushButton::clicked, canvas_, &LayoutCanvas::clearSelection);
    connect(undo_, &QPushButton::clicked, canvas_, &LayoutCanvas::undo);
    connect(redo_, &QPushButton::clicked, canvas_, &LayoutCanvas::redo);
    connect(canvas_, &LayoutCanvas::hintChanged, hint_, &QLabel::setText);
    connect(canvas_, &LayoutCanvas::changed, this, &ExplosionDialog::refresh);
    connect(canvas_, &LayoutCanvas::selectionChanged, this, &ExplosionDialog::refresh);
    connect(canvas_, &LayoutCanvas::zoomRequested, this, [this](double zoom) {
        if (zoom == 0)
            fit();
        else {
            fitted_ = false;
            canvas_->setZoom(zoom);
        }
    });
    auto undoKey = new QShortcut(QKeySequence::Undo, this), redoKey = new QShortcut(QKeySequence::Redo, this);
    connect(undoKey, &QShortcut::activated, canvas_, &LayoutCanvas::undo);
    connect(redoKey, &QShortcut::activated, canvas_, &LayoutCanvas::redo);
    refresh();
}
void ExplosionDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    auto screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    auto available = screen->availableGeometry();
    resize(std::min(1220, available.width() - 40), std::min(820, available.height() - 50));
    QTimer::singleShot(0, this, [this] {
        fit();
        canvas_->setFocus();
    });
}
void ExplosionDialog::resizeEvent(QResizeEvent *e) {
    QDialog::resizeEvent(e);
    if (fitted_)
        QTimer::singleShot(0, this, &ExplosionDialog::fit);
}
void ExplosionDialog::fit() {
    fitted_ = true;
    auto size = scroll_->viewport()->size() - QSize(36, 36);
    canvas_->setZoom(std::min({1.0, double(std::max(80, size.width())) / canvas_->state().canvas.width(),
                               double(std::max(80, size.height())) / canvas_->state().canvas.height()}));
}
void ExplosionDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        canvas_->setDrawing(false);
        canvas_->setFocus();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
void ExplosionDialog::refresh() {
    updating_ = true;
    const auto id = canvas_->selected();
    auto r = canvas_->selectionBounds();
    if (id != fieldSelection_) {
        fieldSelection_ = id;
        scaleBase_ = r;
    }
    for (auto input : fields_)
        input->setEnabled(!id.isEmpty());
    clear_->setEnabled(!id.isEmpty());
    manual_->setChecked(canvas_->drawingMode());
    undo_->setEnabled(canvas_->canUndo());
    redo_->setEnabled(canvas_->canRedo());
    count_->setText(QString("%1 个可选区域").arg(canvas_->state().groups.size()));
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
void ExplosionDialog::applyField(int field) {
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
} // namespace h2d
