#include "overlay.h"
#include "detector.h"
#include "ui.h"
#include <QApplication>
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
Overlay::Overlay(ScreenFrame frame, QWidget *parent) : QWidget(parent), frame_(std::move(frame)) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle("EditHere · 选择截图区域");
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
void Overlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (rect().contains(local)) {
        cursor_ = pixelPoint(local);
        magnifierVisible_ = true; update();
    }
}
void Overlay::leaveEvent(QEvent *event) {
    if (!drawing_) { hoverTimer_.stop(); magnifierVisible_ = false; update(); }
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
        if(!front) { auto target=manualTarget(); target["label"]="整个屏幕"; return {{frame_.image.rect(),target}}; }
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
    keyboardOffset_ = {};
    setCursor(Qt::CrossCursor);
    picker_.reset();
    update();
}
void Overlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(), frame_.image);
    QRect active = selected_;
    if (!drawing_ && active.isEmpty() && picker_.current())
        active = picker_.current()->bounds;
    QPainterPath mask;
    mask.addRect(rect());
    if (!active.isEmpty())
        mask.addRect(localRect(active));
    mask.setFillRule(Qt::OddEvenFill);
    p.fillPath(mask, QColor(16, 18, 24, 105));
    if (!active.isEmpty()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(accent(), 2));
        p.drawRect(localRect(active));
    }
    if (!finished_) {
        QPointF at(double(cursor_.x()) * width() / frame_.image.width(), double(cursor_.y()) * height() / frame_.image.height());
        for (const auto &pen : {QPen(Qt::white, 0)}) {
            p.setPen(pen);
            p.drawLine(at + QPointF(-9,0), at + QPointF(9,0));
            p.drawLine(at + QPointF(0,-9), at + QPointF(0,9));
        }
        const QSizeF panelSize(210, 172);
        const double gap = 24, margin = 8;
        double x = at.x() + gap, y = at.y() + gap;
        if (x + panelSize.width() > width() - margin)
            x = at.x() - gap - panelSize.width();
        if (y + panelSize.height() > height() - margin)
            y = at.y() - gap - panelSize.height();
        x = std::clamp(x, margin, std::max(margin, width() - panelSize.width() - margin));
        y = std::clamp(y, margin, std::max(margin, height() - panelSize.height() - margin));
        const QRectF panel(QPointF(x, y), panelSize);
        const QRectF area(panel.topLeft() + QPointF(6, 6), QSizeF(198, 126));
        p.setPen(QPen(QColor(255,255,255,90),1));
        p.setBrush(QColor(25,28,34,245));
        p.drawRoundedRect(panel,10,10);
        p.save();
        p.setClipRect(area);
        p.fillRect(area,QColor(45,48,55));
        p.setRenderHint(QPainter::SmoothPixmapTransform,false);
        const double cell=10;
        const QPoint sample(std::clamp(cursor_.x(),0,frame_.image.width()-1), std::clamp(cursor_.y(),0,frame_.image.height()-1));
        const QPointF center=area.center();
        const QPointF logicalSample((sample.x()+.5)*width()/frame_.image.width(),(sample.y()+.5)*height()/frame_.image.height());
        const double factor=cell*frame_.image.width()/width();
        p.translate(center); p.scale(factor,factor); p.translate(-logicalSample);
        p.drawImage(rect(),frame_.image);
        p.fillPath(mask,QColor(16,18,24,105));
        if(!active.isEmpty()) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(accent(),2)); p.drawRect(localRect(active));
        }
        p.restore();
        p.save(); p.setClipRect(area); p.setRenderHint(QPainter::Antialiasing,false);
        p.setPen(QPen(QColor(255,255,255,70),0));
        for(double x=center.x()-cell/2; x>=area.left(); x-=cell) p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
        for(double x=center.x()+cell/2; x<=area.right(); x+=cell) p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
        for(double y=center.y()-cell/2; y>=area.top(); y-=cell) p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        for(double y=center.y()+cell/2; y<=area.bottom(); y+=cell) p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white,0));
        p.drawRect(QRectF(center-QPointF(5,5),QSizeF(10,10)));
        p.restore();
        p.setFont(QFont("Microsoft YaHei",9));
        p.setPen(Qt::white);
        p.drawText(QRectF(panel.topLeft() + QPointF(6,134), QSizeF(198,32)),Qt::AlignCenter,
                   QString("像素 %1, %2 · 1000% · 1格=1px").arg(sample.x()).arg(sample.y()));
    }
    if (selected_.isEmpty() && !drawing_) {
        QString text = picker_.current() ? QString("%1  ·  %2 / %3  ·  滚轮 ↑ 更大 ↓ 更小")
                                               .arg(picker_.current()->target["label"].toString().left(30))
                                               .arg(picker_.level())
                                               .arg(picker_.count())
                                         : "拖动截图 · 单击选块 · 松手进入批注 · Esc 取消";
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
void Overlay::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) {
        if (selected_.isEmpty())
            emit cancelled();
        else
            resetSelection();
        return;
    }
    if (e->button() != Qt::LeftButton || finished_)
        return;
    emit selectionBegan();
    start_ = cursor_ = pixelPoint(e->position());
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
    if (!drawing_) { native_.clear(); picker_.reset(); }
    if (!drawing_) { magnifierVisible_ = true; }
    cursor_ = pixelPoint(e->position()) + (drawing_ ? keyboardOffset_ : QPoint());
    cursor_.setX(std::clamp(cursor_.x(), 0, frame_.image.width()));
    cursor_.setY(std::clamp(cursor_.y(), 0, frame_.image.height()));
    if (drawing_)
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
        selected_ = dragRect(start_, end, frame_.image.size());
    finish(false);
}
void Overlay::wheelEvent(QWheelEvent *e) {
    if (drawing_ || finished_ || !selected_.isEmpty())
        return;
    cursor_ = pixelPoint(e->position());
    picker_.update(candidates(), cursor_);
    if (e->angleDelta().y() != 0)
        picker_.step(e->angleDelta().y() > 0 ? 1 : -1);
    update();
    e->accept();
}
void Overlay::keyPressEvent(QKeyEvent *e) {
    if (drawing_ && (e->key()==Qt::Key_Left || e->key()==Qt::Key_Right || e->key()==Qt::Key_Up || e->key()==Qt::Key_Down)) {
        QPoint next = cursor_ + QPoint(e->key()==Qt::Key_Right ? 1 : e->key()==Qt::Key_Left ? -1 : 0,
                                       e->key()==Qt::Key_Down ? 1 : e->key()==Qt::Key_Up ? -1 : 0);
        next.setX(std::clamp(next.x(),0,frame_.image.width()));
        next.setY(std::clamp(next.y(),0,frame_.image.height()));
        keyboardOffset_ += next-cursor_;
        cursor_=next;
        nudged_=true;
        selected_=dragRect(start_,cursor_,frame_.image.size());
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
