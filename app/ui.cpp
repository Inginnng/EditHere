#include "ui.h"
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
namespace h2d {
QColor accent() {
    return QColor("#007aff");
}
void applyTheme() {
    qApp->setStyle("Fusion");
#ifdef Q_OS_MAC
    qApp->setFont(QFont(".AppleSystemUIFont", 12));
#else
    qApp->setFont(QFont("Microsoft YaHei UI", 9));
#endif
    qApp->setStyleSheet(R"(
QWidget { color:#232326; } QDialog { background:#fbfbfd; }
QWidget#editorShell { background:#fbfbfd; border:1px solid #ddddE4; border-radius:15px; }
QWidget#imageWell { background:#efeff2; }
QPushButton { background:#eeeeF1; border:0; border-radius:8px; padding:8px 14px; min-height:20px; }
QPushButton:hover { background:#e3e3e9; } QPushButton:pressed { background:#d6d6df; }
QPushButton:disabled { color:#b9b9c0; } QPushButton:focus { outline:0; border:1px solid #9cc9ff; }
QPushButton[primary="true"] { background:#007aff; color:white; } QPushButton[primary="true"]:hover { background:#0870df; }
QPushButton[tool="true"] { background:transparent; padding:5px; min-width:24px; min-height:24px; }
QPushButton[tool="true"]:hover { background:#e9e9ef; } QPushButton[tool="true"]:checked { background:#e5efff; }
QLabel[muted="true"] { color:#898990; } QLabel#brand { font-weight:600; font-size:13px; }
QTextEdit,QPlainTextEdit { background:white; border:1px solid #dddde5; border-radius:9px; padding:9px; selection-background-color:#c9e3ff; color:#292930; }
QTextEdit:focus,QPlainTextEdit:focus { border:1px solid #007aff; }
QWidget#notesPanel { background:white; border-left:1px solid #e5e5ea; }
QWidget#noteContainer { background:white; }
QFrame#noteCard { background:white; border:1px solid #e4e4e9; border-radius:10px; }
QFrame#noteCard[selected="true"] { background:#f3f7ff; border:1px solid #bbd7ff; }
QLabel#badge { background:#007aff; color:white; border-radius:12px; font-weight:600; }
QScrollArea { border:0; background:transparent; }
QScrollBar:vertical { background:transparent; width:7px; margin:2px; }
QScrollBar::handle:vertical { background:#c8c8d0; border-radius:3px; min-height:24px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QMenu { background:#fbfbfd; padding:6px; border:1px solid #dcdce3; }
QMenu::item { padding:8px 24px; border-radius:5px; } QMenu::item:selected { background:#007aff; color:white; }
QToolTip { background:#292930; color:white; padding:5px 8px; border:0; }
)");
}
QIcon glyph(const QString &name, QColor color) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (name == "point") {
        p.drawEllipse(QPointF(12, 12), 7, 7);
        p.drawEllipse(QPointF(12, 12), 2, 2);
    } else if (name == "rect")
        p.drawRoundedRect(QRectF(4, 5, 16, 14), 1, 1);
    else if (name == "smart") {
        for (auto r : QVector<QRectF>{{3, 3, 6, 6}, {15, 3, 6, 6}, {3, 15, 6, 6}, {15, 15, 6, 6}}) {
            p.drawLine(r.topLeft(), r.topRight());
            p.drawLine(r.topLeft(), r.bottomLeft());
        }
        p.drawLine(8, 12, 16, 12);
        p.drawLine(12, 8, 12, 16);
    } else if (name == "select") {
        QPolygonF a{{6, 3}, {18, 13}, {12, 14}, {9, 20}, {6, 3}};
        p.drawPolygon(a);
    } else if (name == "undo" || name == "redo") {
        if (name == "redo") {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawLine(4, 9, 10, 4);
        p.drawLine(4, 9, 10, 14);
        QPainterPath path;
        path.moveTo(4, 9);
        path.lineTo(14, 9);
        path.cubicTo(23, 9, 23, 21, 11, 20);
        p.drawPath(path);
    } else if (name == "close") {
        p.drawLine(7, 7, 17, 17);
        p.drawLine(17, 7, 7, 17);
    } else if (name == "copy") {
        p.drawRoundedRect(QRectF(8, 8, 12, 13), 2, 2);
        p.drawLine(4, 16, 4, 4);
        p.drawLine(4, 4, 15, 4);
    } else if (name == "save") {
        p.drawLine(12, 3, 12, 15);
        p.drawLine(7, 10, 12, 15);
        p.drawLine(17, 10, 12, 15);
        p.drawLine(4, 15, 4, 20);
        p.drawLine(4, 20, 20, 20);
        p.drawLine(20, 20, 20, 15);
    } else if (name == "notes") {
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        p.drawLine(14, 4, 14, 20);
    } else if (name == "edit") {
        p.drawLine(5, 18, 7, 12);
        p.drawLine(7, 12, 17, 3);
        p.drawLine(17, 3, 21, 7);
        p.drawLine(21, 7, 11, 16);
        p.drawLine(11, 16, 5, 18);
    } else if (name == "trash") {
        p.drawLine(5, 7, 19, 7);
        p.drawLine(9, 4, 15, 4);
        p.drawLine(7, 7, 8, 21);
        p.drawLine(8, 21, 16, 21);
        p.drawLine(16, 21, 17, 7);
        p.drawLine(11, 10, 11, 17);
        p.drawLine(14, 10, 14, 17);
    } else if (name == "more") {
        p.setBrush(color);
        for (int x : {5, 12, 19})
            p.drawEllipse(QPointF(x, 12), 1, 1);
    } else if (name == "pin") {
        p.drawLine(8, 3, 16, 3);
        p.drawLine(9, 3, 9, 9);
        p.drawLine(15, 3, 15, 9);
        p.drawLine(9, 9, 6, 14);
        p.drawLine(15, 9, 18, 14);
        p.drawLine(6, 14, 18, 14);
        p.drawLine(12, 14, 12, 22);
    } else if (name == "check") {
        p.drawLine(4, 12, 10, 18);
        p.drawLine(10, 18, 20, 6);
    } else if (name == "crop") {
        p.drawLine(7, 2, 7, 17);
        p.drawLine(7, 17, 22, 17);
        p.drawLine(2, 7, 17, 7);
        p.drawLine(17, 7, 17, 22);
    } else if (name == "capture") {
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
        p.drawLine(8, 9, 10, 9);
        p.drawLine(8, 9, 8, 11);
        p.drawLine(16, 15, 14, 15);
        p.drawLine(16, 15, 16, 13);
    } else if (name == "plus" || name == "minus") {
        p.drawLine(5, 12, 19, 12);
        if (name == "plus")
            p.drawLine(12, 5, 12, 19);
    }
    pixmap.setDevicePixelRatio(2);
    return QIcon(pixmap);
}
QPushButton *iconButton(const QString &name, const QString &label, QWidget *parent) {
    auto b = new QPushButton(parent);
    b->setIcon(glyph(name));
    b->setIconSize({20, 20});
    b->setToolTip(label);
    b->setAccessibleName(label);
    b->setProperty("tool", true);
    b->setFixedSize(34, 34);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QPushButton *textButton(const QString &text, bool primary, QWidget *parent) {
    auto b = new QPushButton(text, parent);
    b->setProperty("primary", primary);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QLabel *mutedLabel(const QString &text, QWidget *parent) {
    auto l = new QLabel(text, parent);
    l->setProperty("muted", true);
    return l;
}
DragBar::DragBar(QWidget *parent) : QWidget(parent) {
    setCursor(Qt::OpenHandCursor);
}
void DragBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        offset_ = e->globalPosition().toPoint() - window()->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}
void DragBar::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_)
        window()->move(e->globalPosition().toPoint() - offset_);
}
void DragBar::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
}
} // namespace h2d
