#include "ui.h"
#include "fonts.h"
#include "i18n.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIconEngine>
#include <QImage>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWindow>

// Reference the resource from the static library, including in test executables.
static void initializeUiIcons() {
    Q_INIT_RESOURCE(ui_icons);
    Q_INIT_RESOURCE(brand_symbol);
}

namespace h2d {
double wheelZoomFactor(const QWheelEvent &event) {
    const int pixels = event.pixelDelta().y();
    const int angle = event.angleDelta().y();
#ifdef Q_OS_MACOS
    // Cocoa can emit many tiny wheel events for a single trackpad gesture.
    if (event.phase() == Qt::ScrollMomentum) return 1.0;
    const double steps = pixels != 0 ? pixels / 40.0 : angle / 120.0;
    return std::pow(1.06, std::clamp(steps, -1.0, 1.0));
#else
    const int delta = angle != 0 ? angle : pixels;
    return delta > 0 ? 1.12 : delta < 0 ? 1 / 1.12 : 1.0;
#endif
}
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

QString rgbaCss(const QColor &c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

bool isRoundedPopup(const QWidget *widget) {
    if (!widget)
        return false;
    return qobject_cast<const QMenu *>(widget) != nullptr || widget->inherits("QTipLabel") ||
           ((widget->windowFlags() & Qt::WindowType_Mask) == Qt::ToolTip);
}

void prepareTranslucentPopup(QWidget *widget) {
    if (!isRoundedPopup(widget))
        return;
    if (!widget->isVisible()) {
        const Qt::WindowFlags needed = Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
        if ((widget->windowFlags() & needed) != needed)
            widget->setWindowFlags(widget->windowFlags() | needed);
    }
    if (!widget->testAttribute(Qt::WA_TranslucentBackground))
        widget->setAttribute(Qt::WA_TranslucentBackground, true);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget->setAutoFillBackground(false);
    if (auto *label = qobject_cast<QLabel *>(widget)) {
        label->setFrameShape(QFrame::NoFrame);
        label->setMargin(0);
    }
}

class RoundedPopupFilter final : public QObject {
  public:
    explicit RoundedPopupFilter(QObject *parent = nullptr) : QObject(parent) {}
    bool eventFilter(QObject *watched, QEvent *event) override {
        const auto type = event->type();
        if (type == QEvent::Polish || type == QEvent::Show || type == QEvent::WinIdChange) {
            if (auto *widget = qobject_cast<QWidget *>(watched); isRoundedPopup(widget)) {
                if (type == QEvent::Polish) {
                    prepareTranslucentPopup(widget);
                } else {
                    widget->setAutoFillBackground(false);
                    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
                    if (type == QEvent::WinIdChange || widget->testAttribute(Qt::WA_WState_Created))
                        configureRoundedPopupWindow(widget);
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

void refreshTheme() {
    if (applyingTheme)
        return;
    applyingTheme = true;
    if (qApp && !qApp->property("_editherePopupFilterInstalled").toBool()) {
        qApp->setProperty("_editherePopupFilterInstalled", true);
        qApp->installEventFilter(new RoundedPopupFilter(qApp));
    }
    darkTheme =
        currentTheme == ThemeMode::Dark ||
        (currentTheme == ThemeMode::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const auto chrome = captureChromeTokens();
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
    palette.setColor(QPalette::ToolTipBase, QColor(chrome.tooltipBg.rgb()));
    palette.setColor(QPalette::ToolTipText, chrome.tooltipText);
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
QWidget#imageWell[empty="true"] { background:transparent; }
QPushButton { background:@button@; border:1px solid transparent; border-radius:8px; padding:7px 13px; min-height:20px; }QPushButton:hover { background:@hover@; }
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
QPushButton#explodeButton:checked { background:@accent@; color:white; }
QPushButton#explodeButton:checked:hover { background:@accentHover@; }
QPushButton#explodeButton:disabled { background:@disabledBg@; color:@disabled@; }
QLabel[muted="true"] { color:@muted@; }
QLabel#brand { font-weight:600; font-size:13px; }
QLabel[sectionTitle="true"] { font-weight:600; font-size:12px; }
QLabel[error="true"] { color:@error@; }
QLabel#settingsSection {font-weight:600;padding-top:6px;}
QLabel#authorNoteHeading { font-weight:600; font-size:14px; }
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
QWidget#notesPanel { background:@surface@; border-left:1px solid @border@; }
QWidget#layoutInspector { background:@surface@; border:0; }
QWidget#noteContainer { background:@surface@; }
QFrame#noteCard { background:@surface@; border:1px solid @border@; border-radius:10px; }
QFrame#noteCard[selected="true"] { background:@cardSelected@; border-color:@focus@; }
QFrame#noteCard[unresolvedParent="true"] { border:2px solid @error@; }
QFrame#noteCard[childNote="true"] { border-left:3px solid @accent@; }
QWidget#parentPrompt QPushButton { border:1px solid @border@; border-radius:6px; padding:0; font-size:12px; background:@button@; color:@text@; }
QWidget#parentPrompt QPushButton:hover { background:@hover@; }
QLabel#noteBadge { background:@accent@; color:white; border-radius:11px; font-weight:600; font-size:11px; }
QWidget#toolbarSeparator,QWidget#inspectorSeparator { background:@border@; border:0; }
QPushButton#foldNote { color:@accent@; background:transparent; border:0; padding:0 3px; min-height:16px; font-size:11px; }
QPushButton#foldNote:hover { background:@selected@; }
QLabel#noteCoordinates { font-size:10px; color:@muted@; }
QFrame#noteCard QPlainTextEdit { background:transparent; border:1px solid transparent; border-radius:5px; padding:5px; font-size:12px; }
QFrame#noteCard QPlainTextEdit:focus { background:transparent; border-color:transparent; }
QFrame#noteCard QPushButton[tool="true"] { min-width:16px; min-height:16px; padding:2px; }
QLabel#badge { background:@accent@; color:white; border-radius:12px; font-weight:600; }
QScrollArea { border:0; background:transparent; }
QScrollBar:vertical { background:transparent; width:7px; margin:2px; }
QScrollBar:horizontal { background:transparent; height:7px; margin:2px; }
QScrollBar::handle:vertical,QScrollBar::handle:horizontal { background:@scroll@; border-radius:3px; min-height:24px; min-width:24px; }
QScrollBar::handle:vertical:hover,QScrollBar::handle:horizontal:hover { background:@muted@; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0; }
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical,QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal { background:transparent; }
QMenu { background:@menuBg@; color:@text@; padding:6px; border:1px solid @menuBorder@; border-radius:10px; }
QMenu::item { padding:6px 18px 6px 10px; border-radius:6px; }
QMenu::item:selected { background:@accent@; color:white; }
QMenu::item:disabled { color:@disabled@; }
QMenu::separator { height:1px; margin:5px 8px; background:@border@; }
QToolTip { background:@tooltip@; color:@tooltipText@; padding:5px 10px; border:1px solid @tooltipBorder@; border-radius:8px; font-size:12px; }
)");
#ifdef Q_OS_MAC
    // macOS draws the window frame itself, and the traffic lights live in its title
    // bar. A rounded, bordered shell inside that frame would draw a second edge just
    // inside the real one, so a later rule of the same specificity removes it.
    css += QStringLiteral("QWidget#editorShell { border:0; border-radius:0; }\n");
#endif
    const QList<std::pair<QString, QString>> colors{
        {"text", palette.color(QPalette::WindowText).name()},
        {"window", palette.color(QPalette::Window).name()},
        {"surface", palette.color(QPalette::Base).name()},
        {"alternate", palette.color(QPalette::AlternateBase).name()},
        {"border", tone("#dddde5", "#41434d").name()},
        {"well", tone("#efeff2", "#18191e").name()},
        {"button", palette.color(QPalette::Button).name()},
        {"hover", tone("#e3e3e9", "#41444e").name()},
        {"pressed", tone("#d6d6df", "#4a4e5a").name()},
        {"selected", tone("#e5efff", "#243d5e").name()},
        {"cardSelected", tone("#f3f7ff", "#27364c").name()},
        {"disabledBg", palette.color(QPalette::Disabled, QPalette::Button).name()},
        {"disabled", palette.color(QPalette::Disabled, QPalette::Text).name()},
        {"muted", palette.color(QPalette::PlaceholderText).name()},
        {"accent", accent().name()},
        {"error", tone("#bd252f", "#ff8991").name()},
        {"accentHover", tone("#0870df", "#3297ff").name()},
        {"accentPressed", tone("#005fc7", "#0070de").name()},
        {"focus", tone("#9cc9ff", "#4c83bd").name()},
        {"purple", tone("#7656d9", "#8061df").name()},
        {"purpleHover", tone("#6947ce", "#9172ed").name()},
        {"scroll", tone("#c8c8d0", "#595c68").name()},
        {"menuBg", rgbaCss(QColor(chrome.panelBg.red(), chrome.panelBg.green(), chrome.panelBg.blue(), 248))},
        {"menuBorder", rgbaCss(chrome.panelBorder)},
        {"tooltip", rgbaCss(chrome.tooltipBg)},
        {"tooltipText", chrome.tooltipText.name()},
        {"tooltipBorder", rgbaCss(chrome.tooltipBorder)}};
    for (const auto &[name, value] : colors)
        css.replace("@" + name + "@", value);
    qApp->setStyleSheet(css);
    for (auto widget : QApplication::allWidgets()) {
        prepareTranslucentPopup(widget);
        if (auto button = qobject_cast<QPushButton *>(widget)) {
            const auto name = button->property("glyphName").toString();
            if (!name.isEmpty()) {
                QColor color = button->property("captureChrome").toBool()
                                   ? chrome.glyph
                                   : button->property("glyphColor").value<QColor>();
                if (button->objectName() == "scrollStop" && name == QLatin1String("scroll-stop"))
                    color = chrome.danger;
                else if (button->property("primary").toBool() ||
                         (button->objectName() == "explodeButton" && button->isChecked()))
                    color = Qt::white;
                else if (button->property("captureChrome").toBool() &&
                         (button->property("active").toBool() || button->isChecked()))
                    color = chrome.accent;
                button->setIcon(glyph(name, color));
            }
        }
        if (auto label = qobject_cast<QLabel *>(widget)) {
            const auto name = label->property("glyphName").toString();
            if (!name.isEmpty()) {
                const bool isGrip = name == QLatin1String("scroll-grip");
                const QColor tint = isGrip ? chrome.textMuted : palette.color(QPalette::PlaceholderText);
                const int iconDim = isGrip ? 16 : 24;
                label->setPixmap(glyph(name, tint).pixmap(iconDim, iconDim));
            }
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
CaptureChromeTokens captureChromeTokens() {
    CaptureChromeTokens t;
    // Match the workspace dock, stage and status colors in frontend/src/styles/tokens.css.
    t.scrim = QColor(11, 12, 15, 122);
    if (darkTheme) {
        t.panelBg = QColor("#202228");
        t.panelBorder = QColor(255, 255, 255, 33);
        t.panelHighlight = QColor(255, 255, 255, 23);
        t.stage = QColor("#13151a");
        t.text = QColor("#f5f5f7");
        t.textSecondary = QColor("#d1d1d6");
        t.textMuted = QColor("#8e8e93");
        t.glyph = t.textSecondary;
        t.hoverBg = QColor(120, 120, 128, 56);
        t.pressedBg = QColor(120, 120, 128, 77);
        t.activeBg = QColor(10, 132, 255, 46);
        t.divider = QColor(255, 255, 255, 38);
        t.accent = QColor("#0a84ff");
        t.accentHover = QColor("#409cff");
        t.success = QColor("#30d158");
        t.successSoft = QColor(48, 209, 88, 46);
        t.warning = QColor("#ff9f0a");
        t.warningSoft = QColor(255, 159, 10, 46);
        t.danger = QColor("#ff453a");
    } else {
        t.panelBg = Qt::white;
        t.panelBorder = QColor(15, 23, 42, 31);
        t.panelHighlight = QColor(255, 255, 255, 217);
        t.stage = QColor("#f4f5f8");
        t.text = QColor("#1d1d1f");
        t.textSecondary = QColor("#48484a");
        t.textMuted = QColor("#86868b");
        t.glyph = t.textSecondary;
        t.hoverBg = QColor(120, 120, 128, 26);
        t.pressedBg = QColor(120, 120, 128, 41);
        t.activeBg = QColor(0, 122, 255, 28);
        t.divider = QColor(15, 23, 42, 36);
        t.accent = QColor("#007aff");
        t.accentHover = QColor("#0066d6");
        t.success = QColor("#34c759");
        t.successSoft = QColor(52, 199, 89, 36);
        t.warning = QColor("#ff9500");
        t.warningSoft = QColor(255, 149, 0, 36);
        t.danger = QColor("#ff3b30");
    }
    t.tooltipBg = QColor(18, 20, 26, 235);
    t.tooltipText = QColor("#f5f5f7");
    t.tooltipBorder = QColor(255, 255, 255, 36);
    return t;
}
void paintFloatingPanel(QPainter &painter, const QRectF &rect, int radius) {
    const auto tokens = captureChromeTokens();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(tokens.panelBorder, 1));
    painter.setBrush(tokens.panelBg);
    painter.drawRoundedRect(r, radius, radius);
    if (r.width() > radius * 2) {
        painter.setPen(QPen(tokens.panelHighlight, 1));
        painter.drawLine(QPointF(r.left() + radius, r.top() + 1.0),
                         QPointF(r.right() - radius, r.top() + 1.0));
    }
    painter.restore();
}
void paintCaptureHandles(QPainter &painter, const QRectF &selection) {
    painter.save();
    painter.setPen(QPen(accent(), 1.5));
    painter.setBrush(Qt::white);
    const auto center = selection.center();
    for (const auto &point : {selection.topLeft(), QPointF(center.x(), selection.top()),
                              selection.topRight(), QPointF(selection.left(), center.y()),
                              QPointF(selection.right(), center.y()), selection.bottomLeft(),
                              QPointF(center.x(), selection.bottom()), selection.bottomRight()}) {
        painter.drawRoundedRect(QRectF(point - QPointF(3.5, 3.5), QSizeF(7, 7)), 2, 2);
    }
    painter.restore();
}
QColor accent() {
    return QColor(darkTheme ? "#0a84ff" : "#007aff");
}
QRectF magnifierPlacement(QPointF at, QSizeF window, const QVector<QRectF> &tools) {
    constexpr double gap = 24, margin = 8;
    for (const double height : {magnifierPanelHeight, magnifierCompactPanelHeight}) {
        const QSizeF panel(magnifierPanelWidth, height);
        if (panel.width() + 2 * margin > window.width() || height + 2 * margin > window.height())
            continue;
        const auto settle = [&](double x, double y) {
            return QPointF(std::clamp(x, margin, window.width() - panel.width() - margin),
                           std::clamp(y, margin, window.height() - height - margin));
        };
        const auto clear = [&](const QRectF &here) {
            return std::none_of(tools.cbegin(), tools.cend(), [&here](const QRectF &tool) {
                return here.intersects(tool);
            });
        };
        // Flip to the other side of the pointer at the window edge.
        const double x = at.x() + gap + panel.width() <= window.width() - margin
                             ? at.x() + gap : at.x() - gap - panel.width();
        const double y = at.y() + gap + height <= window.height() - margin
                             ? at.y() + gap : at.y() - gap - height;
        const QPointF preferred = settle(x, y);
        if (clear(QRectF(preferred, panel)))
            return {preferred, panel};

        // Combining the edges also lets the panel slip past two adjacent tools.
        QVector<double> columns{preferred.x()};
        QVector<double> rows{preferred.y()};
        for (const QRectF &tool : tools) {
            columns.append(tool.left() - panel.width() - margin);
            columns.append(tool.right() + margin);
            rows.append(tool.top() - height - margin);
            rows.append(tool.bottom() + margin);
        }
        QRectF placed;
        double nearest = std::numeric_limits<double>::max();
        for (const double column : columns) {
            for (const double row : rows) {
                const auto candidate = settle(column, row);
                const QRectF area(candidate, panel);
                const double distance = std::abs(candidate.x() - preferred.x()) + std::abs(candidate.y() - preferred.y());
                if (distance < nearest && clear(area)) {
                    nearest = distance;
                    placed = area;
                }
            }
        }
        if (!placed.isEmpty())
            return placed;
    }
    return {};
}
void applyTheme(ThemeMode mode) {
    currentTheme = mode;
    if (!themeInitialized) {
        themeInitialized = true;
        qApp->setStyle("Fusion");
        applyInterfaceFont();
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, qApp, [](Qt::ColorScheme) {
            if (currentTheme == ThemeMode::System)
                refreshTheme();
        });
    }
    refreshTheme();
}
void applyInterfaceFont() {
#ifdef Q_OS_MAC
    qApp->setFont(QFont(resolveFontFamily({".AppleSystemUIFont", "SF Pro Text", "PingFang SC"}), 12));
#else
    qApp->setFont(QFont(isChineseInterface()
                            ? cjkFontFamily()
                            : latinFontFamily(),
                        9));
#endif
}
namespace {
const QMap<QString, QByteArray> &glyphSources() {
    static const auto sources = [] {
        initializeUiIcons();
        QMap<QString, QByteArray> result;
        const QDir directory(QStringLiteral(":/ui-icons/icons"));
        for (const auto &fileName : directory.entryList({QStringLiteral("*.svg")}, QDir::Files)) {
            QFile file(directory.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "Cannot read shared UI icon:" << file.fileName();
                continue;
            }
            const auto svg = file.readAll();
            if (!QSvgRenderer(svg).isValid()) {
                qWarning() << "Invalid shared UI icon:" << file.fileName();
                continue;
            }
            result.insert(fileName.chopped(4), svg);
        }
        return result;
    }();
    return sources;
}

// Render the vector at the requested device scale rather than scaling one fixed
// raster. Both themes and all platforms use the exact same SVG geometry as Web.
class SvgGlyphEngine final : public QIconEngine {
  public:
    SvgGlyphEngine(QString name, QByteArray svg, QColor normal, QColor checked, QColor disabled)
        : name_(std::move(name)), svg_(std::move(svg)), normal_(normal), checked_(checked), disabled_(disabled) {}
    QIconEngine *clone() const override {
        return new SvgGlyphEngine(name_, svg_, normal_, checked_, disabled_);
    }
    QString key() const override { return QStringLiteral("EditHereSharedSvg"); }
    QString iconName() override { return name_; }
    bool isNull() override { return svg_.isEmpty(); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        const QColor color = mode == QIcon::Disabled ? disabled_ : state == QIcon::On ? checked_ : normal_;
        auto source = svg_;
        source.replace("currentColor", color.name().toUtf8());
        QSvgRenderer renderer(source);
        painter->save();
        painter->setOpacity(painter->opacity() * color.alphaF());
        renderer.render(painter, QRectF(rect));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        if (size.isEmpty() || scale <= 0)
            return {};
        QPixmap result(size * scale);
        result.fill(Qt::transparent);
        result.setDevicePixelRatio(scale);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

  private:
    QString name_;
    QByteArray svg_;
    QColor normal_, checked_, disabled_;
};

// A menu bar image is not a picture but a mask: AppKit keeps the alpha channel
// and paints it in the colour the bar asks for, which Qt selects through
// QIcon::setIsMask(). Rasterising the brand vector here, at the scale the icon is
// requested for, keeps that silhouette sharp on Retina instead of upscaling the
// dock icon's PNG. The inset leaves the clearance the system's own status symbols
// keep: somewhere around 16 points of artwork in the 22 point menu bar slot.
constexpr qreal statusGlyphInset = 0.06;

// The brand file draws its symbol inside a square viewBox with room around it, and
// a status item is laid out by the artwork rather than by the file, so the engine
// crops to what is actually painted. Measuring the drawn box once keeps the two
// from drifting apart when the brand file is redrawn.
QRectF symbolInkBox(const QByteArray &svg) {
    constexpr int probe = 128;
    QImage image(probe, probe, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return {};
    renderer.setAspectRatioMode(Qt::IgnoreAspectRatio);
    {
        QPainter painter(&image);
        renderer.render(&painter, QRectF(0, 0, probe, probe));
    }
    int left = probe, top = probe, right = -1, bottom = -1;
    for (int y = 0; y < probe; ++y) {
        for (int x = 0; x < probe; ++x) {
            if (qAlpha(image.pixel(x, y)) == 0)
                continue;
            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
        }
    }
    const QRectF box = renderer.viewBoxF();
    if (right < left || bottom < top)
        return box;
    return QRectF(box.x() + box.width() * left / probe, box.y() + box.height() * top / probe,
                  box.width() * (right - left + 1) / probe, box.height() * (bottom - top + 1) / probe);
}

class StatusGlyphEngine final : public QIconEngine {
  public:
    StatusGlyphEngine(QByteArray svg, QRectF ink) : svg_(std::move(svg)), ink_(ink) {}
    QIconEngine *clone() const override { return new StatusGlyphEngine(svg_, ink_); }
    QString key() const override { return QStringLiteral("EditHereStatusGlyph"); }
    bool isNull() override { return svg_.isEmpty(); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override {
        QSvgRenderer renderer(svg_);
        if (ink_.isValid())
            renderer.setViewBox(ink_);
        const qreal inset = qRound(rect.width() * statusGlyphInset);
        renderer.render(painter, QRectF(rect).adjusted(inset, inset, -inset, -inset));
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        if (size.isEmpty() || scale <= 0)
            return {};
        QPixmap result(size * scale);
        result.fill(Qt::transparent);
        result.setDevicePixelRatio(scale);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }

  private:
    QByteArray svg_;
    QRectF ink_;
};
} // namespace

QStringList glyphNames() {
    return glyphSources().keys();
}

QIcon glyph(const QString &name, QColor color) {
    const auto &sources = glyphSources();
    const auto source = sources.constFind(name);
    if (source == sources.cend()) {
        qWarning() << "Unknown shared UI icon:" << name;
        return {};
    }
    const bool themed = !color.isValid();
    if (themed)
        color = QColor(darkTheme ? "#d4d5dd" : "#606069");
    return QIcon(new SvgGlyphEngine(name, *source, color, themed ? accent() : color,
                                   qApp->palette().color(QPalette::Disabled, QPalette::ButtonText)));
}
QIcon trayIcon() {
#ifdef Q_OS_MACOS
    static const QByteArray brandSymbol = [] {
        QFile file(QStringLiteral(":/brand/edithere.svg"));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Cannot read the brand symbol:" << file.fileName();
            return QByteArray();
        }
        return file.readAll();
    }();
    if (!brandSymbol.isEmpty()) {
        static const QRectF brandInk = symbolInkBox(brandSymbol);
        QIcon icon(new StatusGlyphEngine(brandSymbol, brandInk));
        icon.setIsMask(true);
        return icon;
    }
#endif
    const QIcon windowIcon = QApplication::windowIcon();
    return windowIcon.isNull() ? glyph(QStringLiteral("capture"), accent()) : windowIcon;
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
void styleControl(QWidget *widget, const QString &declarations) {
    const auto name = widget->objectName();
    const auto selector = name.isEmpty() ? QString::fromLatin1(widget->metaObject()->className())
                                         : QStringLiteral("#") + name;
    widget->setStyleSheet(selector + " { " + declarations + " }");
}
QLabel *mutedLabel(const QString &text, QWidget *parent) {
    auto l = new QLabel(text, parent);
    l->setProperty("muted", true);
    return l;
}
DragBar::DragBar(QWidget *parent, bool canMaximize) : QWidget(parent), canMaximize_(canMaximize) {
    setCursor(Qt::OpenHandCursor);
}
void DragBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && !window()->isFullScreen() && !window()->isMaximized()) {
        // Let the window manager handle monitor/DPI transitions and snapping.
        // Offscreen and platforms without system moves retain the manual path.
        if (auto handle = window()->windowHandle(); handle && handle->startSystemMove()) {
            dragging_ = false;
            e->accept();
            return;
        }
        dragging_ = true;
        offset_ = e->globalPosition().toPoint() - window()->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}
void DragBar::mouseMoveEvent(QMouseEvent *e) {
    if (dragging_ && !window()->isFullScreen() && !window()->isMaximized())
        window()->move(e->globalPosition().toPoint() - offset_);
}
void DragBar::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
}
void DragBar::mouseDoubleClickEvent(QMouseEvent *e) {
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    if (canMaximize_ && e->button() == Qt::LeftButton && !window()->isFullScreen()) {
        emit maximizeRequested();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}
} // namespace h2d
