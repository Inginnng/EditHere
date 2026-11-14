#include "ui.h"
#include "fonts.h"
#include "i18n.h"
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
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QToolButton>
#include <QWindow>

// Reference the resource from the static library, including in test executables.
static void initializeUiIcons() {
    Q_INIT_RESOURCE(ui_icons);
    Q_INIT_RESOURCE(brand_symbol);
}

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
QWidget#scrollCaptureProgress QPushButton,QWidget#scrollCropMenu QPushButton,QPushButton#scrollRegionHandle { background:transparent; border:1px solid transparent; border-radius:8px; padding:0; min-width:0; min-height:0; }
QWidget#scrollCaptureProgress QPushButton:hover,QWidget#scrollCropMenu QPushButton:hover,QPushButton#scrollRegionHandle:hover { background:@hover@; }
QWidget#scrollCaptureProgress QPushButton:pressed,QWidget#scrollCropMenu QPushButton:pressed,QPushButton#scrollRegionHandle:pressed { background:@pressed@; }
QWidget#scrollCaptureProgress QPushButton:checked { background:@selected@; color:@accent@; }
QWidget#scrollCaptureProgress QPushButton:disabled,QWidget#scrollCropMenu QPushButton:disabled,QPushButton#scrollRegionHandle:disabled { background:transparent; color:@disabled@; }
QWidget#scrollCaptureProgress QPushButton:focus,QWidget#scrollCropMenu QPushButton:focus { border-color:@focus@; }
QWidget#scrollCaptureProgress QPushButton#scrollDirection { background:@button@; font-size:14px; padding:0 12px; }
QWidget#scrollCaptureProgress QPushButton#scrollDirection:hover { background:@hover@; }
QWidget#scrollCaptureProgress QPushButton#scrollDirection::menu-indicator { subcontrol-origin:padding; subcontrol-position:center right; right:8px; width:9px; height:9px; }
QPushButton#scrollRegionHandle { background:@window@; }
QWidget#scrollCropMenu QCheckBox { font-size:14px; spacing:6px; }
QWidget#scrollCropMenu QCheckBox::indicator { width:18px; height:18px; }
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
QMenu { background:@window@; color:@text@; padding:6px; border:1px solid @border@; }
QMenu::item { padding:8px 24px; border-radius:5px; }
QMenu::item:selected { background:@accent@; color:white; }
QMenu::item:disabled { color:@disabled@; }
QMenu::separator { height:1px; margin:5px 8px; background:@border@; }
QToolTip { background:@tooltip@; color:@tooltipText@; padding:5px 8px; border:0; }
)");
#ifdef Q_OS_MAC
    // macOS draws the window frame itself, and the traffic lights live in its title
    // bar. A rounded, bordered shell inside that frame would draw a second edge just
    // inside the real one, so a later rule of the same specificity removes it.
    css += QStringLiteral("QWidget#editorShell { border:0; border-radius:0; }\n");
#endif
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
            if (!name.isEmpty()) {
                QColor color = button->property("glyphColor").value<QColor>();
                if (button->objectName() == "scrollStop")
                    color = tone("#bd252f", "#ff8991");
                else if (button->property("primary").toBool() ||
                         (button->objectName() == "explodeButton" && button->isChecked()))
                    color = Qt::white;
                button->setIcon(glyph(name, color));
            }
        }
        if (auto label = qobject_cast<QLabel *>(widget)) {
            const auto name = label->property("glyphName").toString();
            if (!name.isEmpty())
                label->setPixmap(glyph(name, palette.color(QPalette::PlaceholderText)).pixmap(24, 24));
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
QRectF magnifierPlacement(QPointF at, QSizeF window, const QVector<QRectF> &tools) {
    const QSizeF panel(magnifierPanelWidth, magnifierPanelHeight);
    constexpr double gap = 24, margin = 8;
    // Anywhere the panel is put is confined to the window; that is what keeps a flip
    // from turning into a panel half off the screen.
    const auto settle = [&](double x, double y) {
        return QPointF(std::clamp(x, margin, std::max(margin, window.width() - panel.width() - margin)),
                       std::clamp(y, margin, std::max(margin, window.height() - panel.height() - margin)));
    };
    const auto clear = [&](QPointF topLeft) {
        const QRectF here(topLeft, panel);
        for (const QRectF &tool : tools)
            if (here.intersects(tool))
                return false;
        return true;
    };
    // Beside the pointer, and on the other side of it when there is no room there.
    double x = at.x() + gap;
    if (x + panel.width() > window.width() - margin)
        x = at.x() - gap - panel.width();
    double y = at.y() + gap;
    if (y + panel.height() > window.height() - margin)
        y = at.y() - gap - panel.height();
    QPointF placed = settle(x, y);
    if (clear(placed))
        return {placed, panel};
    // It has landed on a tool. Step out along whichever side is nearest, which keeps
    // the panel as close to the pointer as it can be; a way that is still on a tool or
    // would leave the window is not a way out at all.
    double nearest = std::numeric_limits<double>::max();
    for (const QRectF &tool : tools) {
        if (!QRectF(placed, panel).intersects(tool))
            continue;
        const QVector<QPointF> ways{settle(placed.x(), tool.top() - panel.height() - margin),
                                    settle(placed.x(), tool.bottom() + margin),
                                    settle(tool.left() - panel.width() - margin, placed.y()),
                                    settle(tool.right() + margin, placed.y())};
        for (const QPointF &way : ways) {
            if (!clear(way))
                continue;
            const double distance = std::abs(way.x() - placed.x()) + std::abs(way.y() - placed.y());
            if (distance < nearest) {
                nearest = distance;
                placed = way;
            }
        }
    }
    return {placed, panel};
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
        if (window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}
} // namespace h2d
