#include "overlay.h"
#include "detector.h"
#include "ui.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QtConcurrent>
#include <cmath>
namespace h2d {
Overlay::Overlay(ScreenFrame frame, QWidget *parent) : QWidget(parent), frame_(std::move(frame)) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle("Help2Design · 选择截图区域");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
    setGeometry(frame_.logicalGeometry);
    setAttribute(Qt::WA_DeleteOnClose, false);
    toolbar_ = new QWidget(this);
    toolbar_->setStyleSheet("QWidget { background:#fbfbfd; color:#25252a; border-radius:10px; }");
    auto row = new QHBoxLayout(toolbar_);
    row->setContentsMargins(10, 7, 10, 7);
    row->setSpacing(6);
    size_ = mutedLabel({}, toolbar_);
    row->addWidget(size_);
    auto reset = textButton("重选", false, toolbar_), copy = iconButton("copy", "复制截图", toolbar_),
         cancel = iconButton("close", "取消截图", toolbar_), accept = textButton("开始批注", true, toolbar_);
    row->addWidget(reset);
    row->addWidget(copy);
    row->addWidget(cancel);
    row->addWidget(accept);
    connect(reset, &QPushButton::clicked, this, &Overlay::resetSelection);
    connect(cancel, &QPushButton::clicked, this, &Overlay::cancelled);
    connect(copy, &QPushButton::clicked, this, [this] { finish(true); });
    connect(accept, &QPushButton::clicked, this, [this] { finish(false); });
    toolbar_->hide();
    debounce_.setSingleShot(true);
    debounce_.setInterval(150);
    connect(&debounce_, &QTimer::timeout, this, &Overlay::requestProbe);
    auto worker = new QFutureWatcher<QVector<Candidate>>(this);
    connect(worker, &QFutureWatcher<QVector<Candidate>>::finished, this, [this, worker] {
        visual_ = worker->result();
        auto full = manualTarget();
        full["label"] = "整个屏幕";
        visual_.append({QRect(QPoint(0, 0), frame_.image.size()), full});
        if (selected_.isEmpty()) {
            picker_.update(candidates(), cursor_);
            update();
        }
        worker->deleteLater();
    });
    worker->setFuture(QtConcurrent::run([image = frame_.image] { return detectBlocks(image); }));
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
    auto result = native_;
    result += visual_;
    return result;
}
void Overlay::resetSelection() {
    selected_ = {};
    drawing_ = adjusting_ = false;
    toolbar_->hide();
    picker_.reset();
    update();
}
void Overlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(), frame_.image);
    QRect active = selected_;
    if (active.isEmpty() && picker_.current())
        active = picker_.current()->bounds;
    QPainterPath mask;
    mask.addRect(rect());
    if (!active.isEmpty())
        mask.addRect(localRect(active));
    mask.setFillRule(Qt::OddEvenFill);
    p.fillPath(mask, QColor(16, 18, 24, 105));
    if (!active.isEmpty()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent(), 1.5));
        p.drawRect(localRect(active));
        if (!selected_.isEmpty()) {
            p.setBrush(Qt::white);
            for (auto h : handles(active)) {
                QRectF point = localRect(QRect(h, QSize(1, 1)));
                p.drawRect(QRectF(point.x() - 3.5, point.y() - 3.5, 7, 7));
            }
        }
    }
    if (selected_.isEmpty()) {
        QString text = picker_.current() ? QString("%1  ·  %2 / %3  ·  滚轮 ↑ 更大 ↓ 更小")
                                               .arg(picker_.current()->target["label"].toString().left(30))
                                               .arg(picker_.level())
                                               .arg(picker_.count())
                                         : "拖动截图 · 单击选块 · Esc 取消";
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
void Overlay::paintSelection() {
    update();
    if (selected_.isEmpty() || drawing_ || adjusting_) {
        toolbar_->hide();
        return;
    }
    size_->setText(QString("%1 × %2").arg(selected_.width()).arg(selected_.height()));
    toolbar_->adjustSize();
    QRectF r = localRect(selected_);
    int x =
        std::clamp(qRound(r.right()) - toolbar_->width(), 8, std::max(8, width() - toolbar_->width() - 8));
    int y = qRound(r.bottom()) + 12;
    if (y + toolbar_->height() > height() - 8)
        y = std::max(8, qRound(r.top()) - toolbar_->height() - 12);
    toolbar_->move(x, y);
    toolbar_->show();
    toolbar_->raise();
}
void Overlay::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) {
        if (selected_.isEmpty())
            emit cancelled();
        else
            resetSelection();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    emit selectionBegan();
    start_ = cursor_ = pixelPoint(e->position());
    handle_ = -1;
    if (!selected_.isEmpty()) {
        auto hs = handles(selected_);
        for (int i = 0; i < hs.size(); i++) {
            auto r = localRect(QRect(hs[i], QSize(1, 1)));
            if (QLineF(e->position(), r.topLeft()).length() < 9) {
                handle_ = i;
                break;
            }
        }
        if (handle_ >= 0 || containsPixel(selected_, start_)) {
            adjusting_ = true;
            beforeDrag_ = selected_;
            toolbar_->hide();
            return;
        }
    }
    picker_.update(candidates(), start_);
    drawing_ = true;
    selected_ = {};
    toolbar_->hide();
    update();
}
void Overlay::mouseMoveEvent(QMouseEvent *e) {
    cursor_ = pixelPoint(e->position());
    if (adjusting_)
        selected_ = moveRect(beforeDrag_, cursor_ - start_, frame_.image.size(), handle_);
    else if (drawing_)
        selected_ = dragRect(start_, cursor_, frame_.image.size());
    else if (selected_.isEmpty()) {
        picker_.update(candidates(), cursor_);
        debounce_.start();
    }
    update();
}
void Overlay::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton)
        return;
    if (adjusting_) {
        adjusting_ = false;
        paintSelection();
        return;
    }
    if (!drawing_)
        return;
    drawing_ = false;
    QPoint end = pixelPoint(e->position());
    if (QLineF(QPointF(start_), QPointF(end)).length() * width() / frame_.image.width() < 5 &&
        picker_.current())
        selected_ = picker_.current()->bounds;
    else
        selected_ = dragRect(start_, end, frame_.image.size());
    paintSelection();
}
void Overlay::mouseDoubleClickEvent(QMouseEvent *) {
    if (!selected_.isEmpty())
        finish(false);
}
void Overlay::wheelEvent(QWheelEvent *e) {
    if (drawing_ || adjusting_ || !selected_.isEmpty())
        return;
    cursor_ = pixelPoint(e->position());
    picker_.update(candidates(), cursor_);
    picker_.step(e->angleDelta().y() > 0 ? 1 : -1);
    update();
    e->accept();
}
void Overlay::keyPressEvent(QKeyEvent *e) {
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
    debounce_.stop();
    if (probe_)
        probe_->kill();
    if (copy)
        emit copyRequested(area);
    else
        emit accepted(area, candidates());
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
            [this, process](int code, QProcess::ExitStatus) {
                if (code == 0) {
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
