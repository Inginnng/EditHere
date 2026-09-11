#include "ui.h"
#include <QApplication>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QStyleHints>
#include <QToolButton>
namespace h2d {
void paintTransparency(QPainter &painter, const QRect &area) {
    QRect visible = area;
    if (painter.hasClipping())
        visible = visible.intersected(painter.clipBoundingRect().toAlignedRect());
    if (visible.isEmpty())
        return;
    painter.save();
    painter.setClipRect(visible, Qt::IntersectClip);
    painter.fillRect(visible, isDarkTheme() ? QColor("#25262b") : QColor("#ffffff"));
    const QColor alternate(isDarkTheme() ? "#34363d" : "#e7e8ed");
    // Paint only damaged/visible tiles. Keep the canvas origin and vector edges at fractional DPI.
    const int left = (visible.left() / 14) * 14, top = (visible.top() / 14) * 14;
    for (int y = top; y <= visible.bottom(); y += 14)
        for (int x = left; x <= visible.right(); x += 14)
            if ((x / 14 + y / 14) % 2 == 0)
                painter.fillRect(x, y, 14, 14, alternate);
    painter.restore();
}
namespace {
ThemeMode currentTheme = ThemeMode::Light;
bool darkTheme = false;
bool applyingTheme = false;
bool themeInitialized = false;

void refreshTheme() {
    if (applyingTheme)
        return;
    applyingTheme = true;
    darkTheme =
        currentTheme == ThemeMode::Dark ||
        (currentTheme == ThemeMode::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const auto tone = [](const char *light, const char *dark) { return QColor(darkTheme ? dark : light); };
    QPalette palette;
    palette.setColor(QPalette::Window, tone("#fbfbfd", "#202126"));
    palette.setColor(QPalette::WindowText, tone("#232326", "#ededf2"));
    palette.setColor(QPalette::Base, tone("#ffffff", "#282a30"));
    palette.setColor(QPalette::AlternateBase, tone("#f5f5f7", "#303239"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Button, tone("#eeeef1", "#35373f"));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Light, tone("#ffffff", "#484b55"));
    palette.setColor(QPalette::Midlight, tone("#f4f4f7", "#3d4049"));
    palette.setColor(QPalette::Mid, tone("#c8c8d0", "#535660"));
    palette.setColor(QPalette::Dark, tone("#898990", "#17181c"));
    palette.setColor(QPalette::Shadow, tone("#b9b9c0", "#101114"));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Accent, accent());
    palette.setColor(QPalette::Link, accent());
    palette.setColor(QPalette::LinkVisited, tone("#7656d9", "#b79aff"));
    palette.setColor(QPalette::PlaceholderText, tone("#898990", "#a0a2ae"));
    palette.setColor(QPalette::ToolTipBase, tone("#292930", "#e9e9ef"));
    palette.setColor(QPalette::ToolTipText, tone("#ffffff", "#232326"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, tone("#a0a0aa", "#7b7e8a"));
    palette.setColor(QPalette::Disabled, QPalette::Base, tone("#f5f5f7", "#24262c"));
    palette.setColor(QPalette::Disabled, QPalette::Button, tone("#eeeef1", "#2d2f36"));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, tone("#c8c8d0", "#484b55"));
    qApp->setPalette(palette);

    QString css = QStringLiteral(R"(
QWidget { color:@text@; }
QDialog, QMessageBox, QWidget#settingsDialog { background:@window@; }
QWidget#editorShell { background:@window@; border:1px solid @border@; border-radius:15px; }
QWidget#imageWell { background:@well@; }
QPushButton { background:@button@; border:1px solid transparent; border-radius:8px; padding:7px 13px; min-height:20px; }
QPushButton:hover { background:@hover@; }
QPushButton:pressed { background:@pressed@; }
QPushButton:checked { background:@selected@; color:@accent@; }
QPushButton:disabled { background:@disabledBg@; color:@disabled@; }
QPushButton:focus { outline:0; border-color:@focus@; }
QPushButton[primary="true"] { background:@accent@; color:white; }
QPushButton[primary="true"]:hover { background:@accentHover@; }
QPushButton[primary="true"]:pressed { background:@accentPressed@; }
QPushButton[primary="true"]:disabled { background:@disabledBg@; color:@disabled@; }
QPushButton[tool="true"] { background:transparent; padding:4px; min-width:24px; min-height:24px; }
QPushButton[tool="true"]:hover { background:@hover@; }
QPushButton[tool="true"]:checked, QPushButton#componentTool:checked { background:@selected@; color:@accent@; }
QPushButton[tool="true"]:disabled { background:transparent; color:@disabled@; }
QPushButton#explodeButton:checked { background:@purple@; color:white; }
QPushButton#explodeButton:checked:hover { background:@purpleHover@; }
QPushButton#explodeButton:disabled { background:@disabledBg@; color:@disabled@; }
QLabel[muted="true"] { color:@muted@; }
QLabel#brand { font-weight:600; font-size:13px; }
QLabel[error="true"] { color:@error@; }
QLabel#settingsSection {font-weight:600;padding-top:6px;}
QTextEdit,QPlainTextEdit { background:@surface@; border:1px solid @border@; border-radius:9px; padding:9px; selection-background-color:@accent@; selection-color:white; color:@text@; }
QTextEdit:focus,QPlainTextEdit:focus { border-color:@accent@; }
QLineEdit,QKeySequenceEdit,QDoubleSpinBox,QSpinBox,QComboBox { background:@surface@; color:@text@; border:1px solid @border@; border-radius:7px; padding:6px 8px; min-height:20px; selection-background-color:@accent@; selection-color:white; }
QLineEdit:focus,QKeySequenceEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus { border-color:@accent@; }
QLineEdit:disabled,QKeySequenceEdit:disabled,QDoubleSpinBox:disabled,QSpinBox:disabled,QComboBox:disabled { color:@disabled@; background:@disabledBg@; }
QKeySequenceEdit QLineEdit { border:0; border-radius:0; padding:0; background:transparent; }
QComboBox { padding-right:26px; }
QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:top right; width:24px; }
QComboBox QAbstractItemView { background:@surface@; color:@text@; border:1px solid @border@; selection-background-color:@accent@; selection-color:white; outline:0; }
QCheckBox,QRadioButton { spacing:7px; }
QCheckBox:disabled,QRadioButton:disabled { color:@disabled@; }
QCheckBox::indicator,QRadioButton::indicator { width:16px; height:16px; }
QGroupBox { border:1px solid @border@; border-radius:9px; margin-top:12px; padding-top:12px; }
QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left; padding:0 5px; left:10px; color:@muted@; }
QTabWidget::pane { border:1px solid @border@; border-radius:9px; background:@surface@; }
QTabBar::tab { background:transparent; color:@muted@; padding:10px 18px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { color:@accent@; border-bottom-color:@accent@; }
QTabBar::tab:hover { background:@hover@; }
QListWidget,QListView,QTreeView,QTableView { background:@surface@; alternate-background-color:@alternate@; color:@text@; border:1px solid @border@; border-radius:8px; selection-background-color:@selected@; selection-color:@text@; outline:0; }
QListWidget::item,QListView::item { padding:7px 10px; border-radius:5px; }
QListWidget::item:hover,QListView::item:hover { background:@hover@; }
QHeaderView::section { background:@button@; color:@muted@; border:0; border-bottom:1px solid @border@; padding:7px; }
QWidget#notesPanel,QWidget#layoutInspector { background:@surface@; border-left:1px solid @border@; }
QWidget#noteContainer { background:@surface@; }
QFrame#noteCard { background:@surface@; border:1px solid @border@; border-radius:10px; }
QFrame#noteCard[selected="true"] { background:@cardSelected@; border-color:@focus@; }
QLabel#badge { background:@accent@; color:white; border-radius:12px; font-weight:600; }
QScrollArea { border:0; background:transparent; }
QScrollBar:vertical { background:transparent; width:7px; margin:2px; }
QScrollBar:horizontal { background:transparent; height:7px; margin:2px; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:@scroll@; border-radius:3px; min-height:24px; min-width:24px; }
QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover { background:@muted@; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical,QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal { background:transparent; }
QMenu { background:@window@; color:@text@; padding:6px; border:1px solid @border@; }
QMenu::item { padding:8px 24px; border-radius:5px; }
QMenu::item:selected { background:@accent@; color:white; }
QMenu::item:disabled { color:@disabled@; }
QMenu::separator { height:1px; margin:5px 8px; background:@border@; }
QToolTip { background:@tooltip@; color:@tooltipText@; padding:5px 8px; border:0; }
)");
    const QList<std::pair<QString, QColor>> colors{
        {"text", palette.color(QPalette::WindowText)},
        {"window", palette.color(QPalette::Window)},
        {"surface", palette.color(QPalette::Base)},
        {"alternate", palette.color(QPalette::AlternateBase)},
        {"border", tone("#dddde5", "#41434d")},
        {"well", tone("#efeff2", "#18191e")},
        {"button", palette.color(QPalette::Button)},
        {"hover", tone("#e3e3e9", "#41444e")},
        {"pressed", tone("#d6d6df", "#4a4e5a")},
        {"selected", tone("#e5efff", "#243d5e")},
        {"cardSelected", tone("#f3f7ff", "#27364c")},
        {"disabledBg", palette.color(QPalette::Disabled, QPalette::Button)},
        {"disabled", palette.color(QPalette::Disabled, QPalette::Text)},
        {"muted", palette.color(QPalette::PlaceholderText)},
        {"accent", accent()},
        {"error", tone("#bd252f", "#ff8991")},
        {"accentHover", tone("#0870df", "#3297ff")},
        {"accentPressed", tone("#005fc7", "#0070de")},
        {"focus", tone("#9cc9ff", "#4c83bd")},
        {"purple", tone("#7656d9", "#8061df")},
        {"purpleHover", tone("#6947ce", "#9172ed")},
        {"scroll", tone("#c8c8d0", "#595c68")},
        {"tooltip", palette.color(QPalette::ToolTipBase)},
        {"tooltipText", palette.color(QPalette::ToolTipText)}};
    for (const auto &[name, color] : colors)
        css.replace("@" + name + "@", color.name());
    qApp->setStyleSheet(css);
    for (auto widget : QApplication::allWidgets()) {
        if (auto button = qobject_cast<QPushButton *>(widget)) {
            const auto name = button->property("glyphName").toString();
            if (!name.isEmpty())
                button->setIcon(glyph(name));
        }
        if (auto input = qobject_cast<QKeySequenceEdit *>(widget))
            for (auto clear : input->findChildren<QToolButton *>())
                clear->setIcon(glyph("close"));
        widget->update();
    }
    applyingTheme = false;
}
} // namespace

bool isDarkTheme() {
    return darkTheme;
}
QColor accent() {
    return QColor(darkTheme ? "#0a84ff" : "#007aff");
}
void applyTheme(ThemeMode mode) {
    currentTheme = mode;
    if (!themeInitialized) {
        themeInitialized = true;
        qApp->setStyle("Fusion");
#ifdef Q_OS_MAC
        qApp->setFont(QFont(".AppleSystemUIFont", 12));
#else
        qApp->setFont(QFont("Microsoft YaHei UI", 9));
#endif
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, qApp, [](Qt::ColorScheme) {
            if (currentTheme == ThemeMode::System)
                refreshTheme();
        });
    }
    refreshTheme();
}
QIcon glyph(const QString &name, QColor color) {
    if (!color.isValid())
        color = QColor(darkTheme ? "#d4d5dd" : "#606069");
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
    b->setProperty("glyphName", name);
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
