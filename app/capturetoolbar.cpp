#include "capturetoolbar.h"
#include "ocr.h"
#include "platform.h"
#include "ui.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegion>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>

namespace h2d {
namespace {
constexpr int captureButtonSize = 32;
constexpr int captureBarHeight = 44;

struct ChromeApplyGuard {
    QWidget *w;
    bool entered;
    explicit ChromeApplyGuard(QWidget *widget)
        : w(widget), entered(widget != nullptr && !widget->property("_applyingChrome").toBool()) {
        if (entered)
            w->setProperty("_applyingChrome", true);
    }
    ~ChromeApplyGuard() {
        if (entered && w != nullptr)
            w->setProperty("_applyingChrome", false);
    }
};

QString rgbaCss(const QColor &c) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

void styleCaptureToolButton(QPushButton *button, const QString &glyphName) {
    const auto tokens = captureChromeTokens();
    button->setProperty("glyphName", glyphName);
    button->setProperty("captureChrome", true);
    button->setProperty("tool", true);
    QColor tint = tokens.glyph;
    if (glyphName == QLatin1String("scroll-stop"))
        tint = tokens.danger;
    else if (button->property("primary").toBool())
        tint = Qt::white;
    else if (button->property("active").toBool() || button->isChecked())
        tint = tokens.accent;
    button->setProperty("glyphColor", tint);
    button->setIcon(glyph(glyphName, tint));
    button->setIconSize(QSize(18, 18));
    button->setFixedSize(captureButtonSize, captureButtonSize);
    button->setCursor(Qt::PointingHandCursor);
}

QString captureControlsStyle(const CaptureChromeTokens &t) {
    return QStringLiteral(R"(
QPushButton[captureChrome="true"] { background: transparent; border: 1px solid transparent;
    border-radius: %1px; padding: 0; min-width: %2px; max-width: %2px; min-height: %2px; max-height: %2px; }
QPushButton[captureChrome="true"]:hover { background: %3; }
QPushButton[captureChrome="true"]:pressed { background: %4; }
QPushButton[captureChrome="true"]:checked, QPushButton[captureChrome="true"][active="true"] { background: %5; }
QPushButton[captureChrome="true"]:disabled { background: transparent; color: %6; }
QPushButton[captureChrome="true"][primary="true"] { background: %7; }
QPushButton[captureChrome="true"][primary="true"]:hover { background: %8; }
QPushButton[captureChrome="true"][primary="true"]:pressed { background: %7; }
QPushButton[captureChrome="true"][primary="true"]:disabled { background: %5; }
)")
        .arg(CaptureChromeTokens::cardRadius).arg(captureButtonSize - 2)
        .arg(rgbaCss(t.hoverBg), rgbaCss(t.pressedBg), rgbaCss(t.activeBg),
             t.textMuted.name(), t.accent.name(), t.accentHover.name());
}

QString captureBarStyle(const CaptureChromeTokens &t) {
    return captureControlsStyle(t) + QStringLiteral(R"(
QLabel[captureReadout="true"], QPushButton[captureReadout="true"] { color: %1; font-size: 12px; }
QPushButton[captureReadout="true"], QPushButton[captureChoice="true"] {
    background: transparent; border: 1px solid transparent; border-radius: %2px;
    padding: 0 6px; min-width: 0; min-height: 26px; max-height: 26px; }
QPushButton[captureReadout="true"]:hover, QPushButton[captureChoice="true"] { background: %3; color: %4; }
QPushButton[captureChoice="true"] { padding: 0 22px 0 8px; font-size: 12px; }
QPushButton[captureChoice="true"]:hover, QPushButton[captureReadout="true"]:pressed { background: %5; }
QPushButton[captureReadout="true"]:disabled, QPushButton[captureChoice="true"]:disabled { color: %6; }
QPushButton[captureChoice="true"]::menu-indicator { subcontrol-origin: border;
    subcontrol-position: center right; right: 8px; width: 9px; height: 9px; }
QLabel#captureBusyStatus { color: %1; font-size: 12px; padding: 0 4px; }
QFrame#toolbarSeparator { background: %7; border: none; }
)")
        .arg(t.textSecondary.name()).arg(CaptureChromeTokens::controlRadius)
        .arg(rgbaCss(t.hoverBg), t.text.name(), rgbaCss(t.pressedBg), t.textMuted.name(), rgbaCss(t.divider));
}

QFrame *captureSeparator(QWidget *parent) {
    auto *separator = new QFrame(parent);
    separator->setObjectName(QStringLiteral("toolbarSeparator"));
    separator->setFixedSize(1, 16);
    return separator;
}

QPoint captureBarPosition(const QRect &selection, const QRect &bounds, QSize size) {
    constexpr int gap = 6;
    const int x = std::clamp(selection.right() + 1 - size.width(), bounds.left(),
                             std::max(bounds.left(), bounds.right() + 1 - size.width()));
    const int below = selection.bottom() + 1 + gap;
    const int above = selection.top() - size.height() - gap;
    const int y = below + size.height() <= bounds.bottom() + 1 ? below : above;
    return {x, std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - size.height()))};
}

class ToolbarGrip final : public QWidget {
  public:
    explicit ToolbarGrip(bool childBarMode, std::function<void(QPoint)> onMoved = {}, QWidget *parent = nullptr)
        : QWidget(parent), childBarMode_(childBarMode), onMoved_(std::move(onMoved)) {
        setObjectName(childBarMode ? QStringLiteral("captureDragBar") : QStringLiteral("scrollDragBar"));
        setCursor(Qt::OpenHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(18, captureButtonSize);
        icon_ = new QLabel(this);
        icon_->setProperty("glyphName", QStringLiteral("scroll-grip"));
        icon_->setAlignment(Qt::AlignCenter);
        icon_->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *box = new QHBoxLayout(this);
        box->setContentsMargins(0, 0, 0, 0);
        box->addWidget(icon_);
        refreshTheme();
    }
    void refreshTheme() {
        icon_->setPixmap(glyph(QStringLiteral("scroll-grip"), captureChromeTokens().textMuted).pixmap(16, 16));
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (childBarMode_) {
            if (QWidget *bar = parentWidget()) {
                dragging_ = true;
                offset_ = event->globalPosition().toPoint() - bar->pos();
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
        } else {
            if (auto *handle = window()->windowHandle(); handle && handle->startSystemMove()) {
                dragging_ = false;
                event->accept();
                return;
            }
            dragging_ = true;
            offset_ = event->globalPosition().toPoint() - window()->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!dragging_) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        if (childBarMode_) {
            if (onMoved_) {
                onMoved_(event->globalPosition().toPoint() - offset_);
                event->accept();
                return;
            }
        } else {
            window()->move(event->globalPosition().toPoint() - offset_);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (dragging_) {
            dragging_ = false;
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

  private:
    bool childBarMode_ = true;
    bool dragging_ = false;
    QPoint offset_;
    QLabel *icon_ = nullptr;
    std::function<void(QPoint)> onMoved_;
};

class RoundedCropPopup final : public QWidget {
  public:
    explicit RoundedCropPopup(QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_AlwaysShowToolTips);
        applyTheme();
    }
    void applyTheme() {
        ChromeApplyGuard guard(this);
        if (!guard.entered)
            return;
        const auto t = captureChromeTokens();
        setStyleSheet(captureControlsStyle(t) + QStringLiteral(
            "QWidget#scrollCropMenu QCheckBox { color: %1; font-size: 13px; spacing: 6px; }"
            "QWidget#scrollCropMenu QCheckBox::indicator { width: 16px; height: 16px; }"
            "QWidget#scrollCropMenu QFrame#toolbarSeparator { background: %2; border: none; }")
            .arg(t.text.name(), rgbaCss(t.divider)));
        update();
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        paintFloatingPanel(painter, rect());
    }
    void changeEvent(QEvent *event) override {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
            applyTheme();
        QWidget::changeEvent(event);
    }
};

QString panelStyleSheet() {
    const auto t = captureChromeTokens();
    return QStringLiteral(R"(
QWidget#captureStylePanel { color: %1; }
QWidget#captureStylePanel QLabel { color: %1; }
QWidget#captureStylePanel QPushButton { background: transparent; border: 1px solid transparent; color: %2; }
QWidget#captureStylePanel QPushButton[tab="true"] { padding: 5px 12px; border-radius: 8px; }
QWidget#captureStylePanel QPushButton[tab="true"]:hover { background: %3; color: %1; }
QWidget#captureStylePanel QPushButton[tab="true"][chosen="true"] { background: %4; color: %5; font-weight: 600; }
QWidget#captureStylePanel QSlider::groove:horizontal { height: 4px; background: %6; border-radius: 2px; }
QWidget#captureStylePanel QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
QWidget#captureStylePanel QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;
    background: #ffffff; border: 1px solid %7; }
QWidget#captureStylePanel QCheckBox { color: %2; spacing: 7px; }
)")
        .arg(t.text.name(), t.textSecondary.name(), rgbaCss(t.hoverBg),
             rgbaCss(t.activeBg), t.accent.name(), rgbaCss(t.divider), rgbaCss(t.panelBorder));
}
} // namespace

// ---------------------------------------------------------------- StylePanel

StylePanel::StylePanel(Mode mode, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), mode_(mode) {
    setObjectName(QStringLiteral("captureStylePanel"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    caption_ = new QLabel(this);
    QFont captionFont = caption_->font();
    captionFont.setWeight(QFont::DemiBold);
    caption_->setFont(captionFont);
    layout->addWidget(caption_);
    if (mode_ == Mode::Shadow) {
        auto *tabs = new QHBoxLayout;
        tabs->setContentsMargins(0, 0, 0, 0);
        tabs->setSpacing(6);
        shadowTab_ = new QPushButton(this);
        borderTab_ = new QPushButton(this);
        for (auto *button : {shadowTab_, borderTab_}) {
            button->setProperty("tab", true);
            button->setCursor(Qt::PointingHandCursor);
            button->setCheckable(true);
            tabs->addWidget(button);
        }
        layout->addLayout(tabs);
        connect(shadowTab_, &QPushButton::clicked, this, [this] { setTab(0); });
        connect(borderTab_, &QPushButton::clicked, this, [this] { setTab(1); });
    }
    strength_ = new QSlider(Qt::Horizontal, this);
    strength_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(strength_);
    strengthValue_ = new QLabel(this);
    strengthValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(strengthValue_);
    remember_ = new QCheckBox(this);
    layout->addWidget(remember_);
    connect(strength_, &QSlider::valueChanged, this, [this](int) {
        if (syncing_)
            return;
        emitStyle();
    });
    connect(remember_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !syncing_)
            emit rememberRequested(style_);
    });
    setTab(0);
    setFixedWidth(232);
    applyChromeTheme();
    retranslate();
    syncFromStyle();
}

void StylePanel::applyChromeTheme() {
    ChromeApplyGuard guard(this);
    if (!guard.entered)
        return;
    setStyleSheet(panelStyleSheet());
    update();
}

void StylePanel::retranslate() {
    if (mode_ == Mode::Corners) {
        caption_->setText(tr("圆角"));
        strength_->setToolTip(tr("圆角半径"));
    } else {
        caption_->setText(tr("阴影 / 边框"));
        shadowTab_->setText(tr("阴影"));
        borderTab_->setText(tr("边框"));
        shadowTab_->setToolTip(tr("图片外圈的柔和投影"));
        borderTab_->setToolTip(tr("图片内沿的一条描边"));
    }
    remember_->setText(tr("每次截图都设置"));
    remember_->setToolTip(tr("以后每次截图都从这个样式开始"));
    setAccessibleName(caption_->text());
}

void StylePanel::refresh() {
    applyChromeTheme();
}

void StylePanel::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyChromeTheme();
    QWidget::changeEvent(event);
}

void StylePanel::setTab(int tab) {
    tab_ = mode_ == Mode::Corners ? 0 : std::clamp(tab, 0, 1);
    if (shadowTab_ != nullptr) {
        shadowTab_->setChecked(tab_ == 0);
        borderTab_->setChecked(tab_ == 1);
        shadowTab_->setProperty("chosen", tab_ == 0);
        borderTab_->setProperty("chosen", tab_ == 1);
        for (auto *button : {shadowTab_, borderTab_}) {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
    syncing_ = true;
    if (mode_ == Mode::Corners) {
        strength_->setRange(0, 32);
        strength_->setValue(style_.cornerRadius);
    } else if (tab_ == 0) {
        strength_->setRange(0, 100);
        strength_->setValue(style_.shadow ? style_.shadowStrength : 0);
    } else {
        strength_->setRange(0, 8);
        strength_->setValue(style_.border ? style_.borderWidth : 0);
    }
    syncing_ = false;
    strengthValue_->setText(mode_ == Mode::Corners ? tr("%1 px").arg(strength_->value())
                                                   : QStringLiteral("%1").arg(strength_->value()));
}

void StylePanel::setStyle(const CaptureStyle &style) {
    style_ = style;
    syncFromStyle();
}

void StylePanel::syncFromStyle() {
    // The shadow tab is the one the panel opens on, unless the picture only carries a
    // border, in which case opening on an empty shadow would look like a bug.
    if (mode_ == Mode::Shadow)
        setTab(!style_.shadow && style_.border ? 1 : 0);
    else
        setTab(0);
    syncing_ = true;
    remember_->setChecked(false);
    syncing_ = false;
    setFixedWidth(232);
    adjustSize();
}

void StylePanel::emitStyle() {
    const int value = strength_->value();
    if (mode_ == Mode::Corners) {
        style_.cornerRadius = value;
    } else if (tab_ == 0) {
        style_.shadow = value > 0;
        if (value > 0)
            style_.shadowStrength = value;
    } else {
        style_.border = value > 0;
        if (value > 0)
            style_.borderWidth = value;
    }
    strengthValue_->setText(mode_ == Mode::Corners ? tr("%1 px").arg(value)
                                                   : QStringLiteral("%1").arg(value));
    emit styleChanged(style_);
}

void StylePanel::popupBeside(const QWidget *anchor, const QRect &bounds) {
    adjustSize();
    const QRect area = anchor != nullptr
                           ? QRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size())
                           : QRect(QCursor::pos(), QSize(1, 1));
    const int gap = 8;
    int x = area.left() - gap - width();
    if (x < bounds.left())
        x = area.right() + gap;
    x = std::clamp(x, bounds.left() + gap,
                   std::max(bounds.left() + gap, bounds.right() - width() - gap));
    int y = area.bottom() - height();
    y = std::clamp(y, bounds.top() + gap,
                   std::max(bounds.top() + gap, bounds.bottom() - height() - gap));
    move(x, y);
    show();
    raise();
}

void StylePanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintFloatingPanel(painter, rect());
}

// ------------------------------------------------------------- CaptureToolbar

CaptureToolbar::CaptureToolbar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("captureToolbar"));
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setFixedHeight(captureBarHeight);
    dockLayout_ = new QVBoxLayout(this);
    dockLayout_->setContentsMargins(8, 5, 8, 5);
    dockLayout_->setSpacing(4);
    actions_ = new QHBoxLayout;
    actions_->setContentsMargins(0, 0, 0, 0);
    actions_->setSpacing(4);
    dockLayout_->addLayout(actions_);
    dragBar_ = new ToolbarGrip(true, [this](QPoint position) {
        userMoved_ = true;
        moveWithinBounds(position);
        emit moved();
    }, this);
    actions_->addWidget(dragBar_);
    readout_ = new QWidget(this);
    auto *readoutLayout = new QHBoxLayout(readout_);
    readoutLayout->setContentsMargins(0, 0, 0, 0);
    readoutLayout->setSpacing(4);
    size_ = new QPushButton(readout_);
    size_->setObjectName(QStringLiteral("captureSize"));
    size_->setProperty("captureReadout", true);
    size_->setFixedHeight(28);
    size_->setCursor(Qt::PointingHandCursor);
    readoutLayout->addWidget(size_);
    ratio_ = new QPushButton(readout_);
    ratio_->setObjectName(QStringLiteral("captureRatio"));
    ratio_->setProperty("captureChoice", true);
    ratio_->setFixedHeight(28);
    ratio_->setCursor(Qt::PointingHandCursor);
    ratio_->setMenu(new QMenu(ratio_));
    readoutLayout->addWidget(ratio_);
    actions_->addWidget(readout_);
    actions_->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("captureBusyStatus"));
    actions_->addWidget(status_);
    status_->hide();
    brush_ = addButton(QStringLiteral("brush"), tr("画笔（B）"));
    mosaic_ = addButton(QStringLiteral("mosaic"), tr("马赛克（M）"));
    for (auto *tool : {brush_, mosaic_})
        tool->setCheckable(true);
    markupSettings_ = addButton(QStringLiteral("layout-settings"), tr("绘制设置"));
    undoMarkup_ = addButton(QStringLiteral("undo"), tr("撤销绘制（Ctrl+Z）"));
    actions_->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    (void)addButton(QStringLiteral("ocr"), tr("文字识别"));
    (void)addButton(QStringLiteral("scroll"), tr("长截图"));
    actions_->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    (void)addButton(QStringLiteral("edit"), tr("批注"));
    (void)addButton(QStringLiteral("pin"), tr("贴图"));
    (void)addButton(QStringLiteral("image-save"), tr("保存图片"));
    (void)addButton(QStringLiteral("image-copy"), tr("复制图像"));
    more_ = new QPushButton(this);
    more_->setObjectName(QStringLiteral("captureMore"));
    styleCaptureToolButton(more_, QStringLiteral("more"));
    actions_->addWidget(more_);
    actions_->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    close_ = new QPushButton(this);
    close_->setObjectName(QStringLiteral("captureClose"));
    styleCaptureToolButton(close_, QStringLiteral("close"));
    connect(close_, &QPushButton::clicked, this, &CaptureToolbar::dismiss);
    actions_->addWidget(close_);
    connect(size_, &QPushButton::clicked, this, &CaptureToolbar::chooseSize);
    connect(more_, &QPushButton::clicked, this, &CaptureToolbar::showMenu);
    applyChromeTheme();
    retranslate();
    updateActionAvailability();
}

void CaptureToolbar::applyChromeTheme() {
    ChromeApplyGuard guard(this);
    if (!guard.entered)
        return;
    setStyleSheet(captureBarStyle(captureChromeTokens()));
    if (auto *grip = static_cast<ToolbarGrip *>(dragBar_))
        grip->refreshTheme();
    updateActionIcons();
    adjustSize();
    update();
}

void CaptureToolbar::updateActionIcons() {
    for (auto *button : buttons_)
        styleCaptureToolButton(button, button->property("glyphName").toString());
    if (more_)
        styleCaptureToolButton(more_, QStringLiteral("more"));
    if (close_)
        styleCaptureToolButton(close_, QStringLiteral("close"));
}

void CaptureToolbar::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyChromeTheme();
    QWidget::changeEvent(event);
}

QPushButton *CaptureToolbar::addButton(const QString &glyphName, const QString &label) {
    auto *button = new QPushButton(this);
    const bool isPrimary = glyphName == QLatin1String("edit");
    if (isPrimary)
        button->setProperty("primary", true);
    styleCaptureToolButton(button, glyphName);
    button->setToolTip(label);
    button->setAccessibleName(label);
    connect(button, &QPushButton::clicked, this, [this, glyphName] {
        if (glyphName == QLatin1String("image-copy"))
            copy();
        else if (glyphName == QLatin1String("pin"))
            pin();
        else if (glyphName == QLatin1String("image-save"))
            save();
        else if (glyphName == QLatin1String("ocr"))
            recognize();
        else if (glyphName == QLatin1String("edit"))
            annotate();
        else if (glyphName == QLatin1String("scroll"))
            scroll();
        else if (glyphName == QLatin1String("brush"))
            setMarkupTool(brush_->isChecked() ? CaptureMarkupTool::Brush : CaptureMarkupTool::None);
        else if (glyphName == QLatin1String("mosaic"))
            setMarkupTool(mosaic_->isChecked() ? CaptureMarkupTool::Mosaic : CaptureMarkupTool::None);
        else if (glyphName == QLatin1String("layout-settings"))
            showMarkupOptions();
        else if (glyphName == QLatin1String("undo"))
            emit undoMarkupRequested();
    });
    actions_->addWidget(button);
    buttons_.append(button);
    return button;
}

void CaptureToolbar::retranslate() {
    // The buttons are found by their glyph so a language change does not have to
    // remember the order they were created in.
    for (auto *button : buttons_) {
        const auto name = button->property("glyphName").toString();
        QString label;
        if (name == QLatin1String("image-copy"))
            label = tr("复制图像");
        else if (name == QLatin1String("pin"))
            label = tr("贴图");
        else if (name == QLatin1String("image-save"))
            label = tr("保存图片");
        else if (name == QLatin1String("ocr"))
            label = tr("文字识别");
        else if (name == QLatin1String("edit"))
            label = tr("批注");
        else if (name == QLatin1String("scroll"))
            label = tr("长截图");
        else if (name == QLatin1String("brush"))
            label = tr("画笔（B）");
        else if (name == QLatin1String("mosaic"))
            label = tr("马赛克（M）");
        else if (name == QLatin1String("layout-settings"))
            label = tr("绘制设置");
        else if (name == QLatin1String("undo"))
            label = tr("撤销绘制（Ctrl+Z）");
        if (!label.isEmpty()) {
            button->setToolTip(label);
            button->setAccessibleName(label);
        }
    }
    if (dragBar_)
        dragBar_->setToolTip(tr("拖动工具栏"));
    more_->setToolTip(tr("更多选项"));
    more_->setAccessibleName(tr("更多选项"));
    if (close_) {
        close_->setToolTip(tr("取消截图（Esc）"));
        close_->setAccessibleName(close_->toolTip());
    }
    size_->setToolTip(tr("点击输入精确尺寸"));
    ratio_->setToolTip(tr("点击选择固定比例"));
    ratio_->setAccessibleName(tr("固定比例"));
    ratio_->setText(captureRatioLabel(ratioChoice_, customWidth_, customHeight_));
    ratio_->setFixedWidth(ratio_->fontMetrics().horizontalAdvance(ratio_->text()) + 34);
    ratio_->menu()->clear();
    populateRatioMenu(ratio_->menu());
    if (!message_.isEmpty())
        status_->setText(message_);
    updateActionAvailability();
    adjustSize();
}

void CaptureToolbar::setSelectionSize(QSize size) {
    // The size is kept as a property so the dialog can open on it, and remembered so
    // that adding the corner afterwards does not have to be told twice.
    size_->setProperty("pixels", size);
    size_->setText(tr("%1 × %2 px").arg(size.width()).arg(size.height()));
    size_->setFixedWidth(size_->fontMetrics().horizontalAdvance(size_->text()) + 14);
    adjustSize();
}

void CaptureToolbar::setSelectionOrigin(QPoint origin) {
    const auto pixels = size_->property("pixels").toSize();
    // The corner and the size read as one line, the way a capture tool shows where the
    // region is and how big it is.
    size_->setText(tr("%1, %2 · %3 × %4 px")
                       .arg(origin.x())
                       .arg(origin.y())
                       .arg(pixels.width())
                       .arg(pixels.height()));
    size_->setFixedWidth(size_->fontMetrics().horizontalAdvance(size_->text()) + 14);
    adjustSize();
}

void CaptureToolbar::setOcrLanguage(OcrLanguageMode language) {
    ocrLanguage_ = language;
}

void CaptureToolbar::setHistory(const QStringList &labels) {
    historyLabels_ = labels;
}

void CaptureToolbar::setSelections(const QVector<QRect> &selections) {
    selections_ = selections;
}

void CaptureToolbar::setHistoryIndex(int index) {
    historyIndex_ = index;
}

void CaptureToolbar::showMenu() {
    if (busy_)
        return;
    QPointer<QMenu> menu = new QMenu(more_);
    buildMenu(menu);
    menu->exec(more_->mapToGlobal(QPoint(0, more_->height())));
    if (menu)
        menu->deleteLater();
}

void CaptureToolbar::setBusy(bool busy, const QString &message) {
    busy_ = busy;
    message_ = message;
    status_->setText(message);
    status_->setVisible(!message.isEmpty());
    updateActionAvailability();
    more_->setEnabled(!busy);
    size_->setEnabled(!busy);
    ratio_->setEnabled(!busy);
    adjustSize();
    raise();
}

void CaptureToolbar::setScrollAvailable(bool available) {
    scrollAvailable_ = available;
    updateActionAvailability();
}

void CaptureToolbar::setMarkupTool(CaptureMarkupTool tool) {
    if (markupTool_ == tool)
        return;
    markupTool_ = tool;
    brush_->setChecked(tool == CaptureMarkupTool::Brush);
    mosaic_->setChecked(tool == CaptureMarkupTool::Mosaic);
    updateActionIcons();
    updateActionAvailability();
    emit markupToolChanged();
}

void CaptureToolbar::setHasMarkup(bool hasMarkup) {
    if (hasMarkup_ == hasMarkup)
        return;
    hasMarkup_ = hasMarkup;
    updateActionAvailability();
}

void CaptureToolbar::updateActionAvailability() {
    for (auto *button : buttons_) {
        const auto name = button->property("glyphName").toString();
        if (name == QLatin1String("scroll")) {
            button->setEnabled(!busy_ && scrollAvailable_ && !hasMarkup_);
            button->setToolTip(hasMarkup_ ? tr("撤销绘制后可开始长截图") : tr("长截图"));
        } else if (button == undoMarkup_) {
            button->setEnabled(!busy_ && hasMarkup_);
        } else if (button == markupSettings_) {
            button->setEnabled(!busy_ && markupTool_ != CaptureMarkupTool::None);
        } else {
            button->setEnabled(!busy_);
        }
    }
}

void CaptureToolbar::moveWithinBounds(QPoint position) {
    const int x = std::clamp(position.x(), toolBounds_.left(),
                            std::max(toolBounds_.left(), toolBounds_.right() + 1 - width()));
    const int y = std::clamp(position.y(), toolBounds_.top(),
                            std::max(toolBounds_.top(), toolBounds_.bottom() + 1 - height()));
    move(x, y);
    raise();
}

void CaptureToolbar::placeNear(const QRect &selection, const QRect &bounds) {
    toolBounds_ = bounds.adjusted(8, 0, -8, -8);
    readout_->setFixedWidth(size_->width() + ratio_->width() + readout_->layout()->spacing());
    actions_->invalidate();
    const auto margins = dockLayout_->contentsMargins();
    const int restoredReadout = readoutAbove_ ? readout_->width() + actions_->spacing() : 0;
    const int inlineWidth = actions_->sizeHint().width() + restoredReadout + margins.left() + margins.right();
    const bool needsSecondRow = inlineWidth > toolBounds_.width();
    if (readoutAbove_ != needsSecondRow) {
        readoutAbove_ = needsSecondRow;
        if (readoutAbove_) {
            actions_->removeWidget(readout_);
            dockLayout_->insertWidget(0, readout_, 0, Qt::AlignRight);
        } else {
            dockLayout_->removeWidget(readout_);
            actions_->insertWidget(1, readout_);
        }
        setFixedHeight(readoutAbove_ ? captureBarHeight + 32 : captureBarHeight);
    }
    dockLayout_->invalidate();
    const QSize naturalSize = dockLayout_->sizeHint().expandedTo(dockLayout_->minimumSize());
    resize(naturalSize.width(), height());
    dockLayout_->activate();
    moveWithinBounds(userMoved_ ? pos() : captureBarPosition(selection, toolBounds_, size()));
}

void CaptureToolbar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintFloatingPanel(painter, rect());
}

void CaptureToolbar::copy() {
    if (!busy_)
        emit copyRequested();
}

void CaptureToolbar::pin() {
    if (!busy_)
        emit pinRequested();
}

void CaptureToolbar::save() {
    if (!busy_)
        emit saveRequested();
}

void CaptureToolbar::recognize() {
    if (!busy_)
        emit ocrRequested();
}

void CaptureToolbar::annotate() {
    if (!busy_)
        emit annotateRequested();
}

void CaptureToolbar::dismiss() {
    if (!busy_)
        emit dismissed();
}

void CaptureToolbar::scroll() {
    if (!busy_ && scrollAvailable_ && !hasMarkup_)
        emit scrollRequested();
}

ScrollCaptureRegion::ScrollCaptureRegion(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setObjectName(QStringLiteral("scrollCaptureRegion"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    auto *handle = new QPushButton(this);
    handle->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                           Qt::WindowDoesNotAcceptFocus);
    handle->setObjectName(QStringLiteral("scrollRegionHandle"));
    handle->setAttribute(Qt::WA_ShowWithoutActivating);
    handle->setAttribute(Qt::WA_AlwaysShowToolTips);
    handle->setAttribute(Qt::WA_TranslucentBackground);
    handle->setFocusPolicy(Qt::NoFocus);
    styleCaptureToolButton(handle, QStringLiteral("scroll-move-vertical"));
    handle->setCursor(Qt::SizeVerCursor);
    handle->installEventFilter(this);
    handle_ = handle;
    applyChromeTheme();
}

ScrollCaptureRegion::~ScrollCaptureRegion() = default;

void ScrollCaptureRegion::applyChromeTheme() {
    ChromeApplyGuard guard(this);
    if (!guard.entered)
        return;
    const auto t = captureChromeTokens();
    if (handle_) {
        const QColor handleBg = anchored_ ? t.stage : t.panelBg;
        handle_->setStyleSheet(captureControlsStyle(t) + QStringLiteral(
            "QPushButton#scrollRegionHandle { background: %1; border-color: %2; }"
            "QPushButton#scrollRegionHandle:hover { background: %3; }"
            "QPushButton#scrollRegionHandle:pressed { background: %4; }"
            "QPushButton#scrollRegionHandle:disabled { background: %1; }")
            .arg(rgbaCss(handleBg), rgbaCss(t.panelBorder), rgbaCss(t.hoverBg), rgbaCss(t.pressedBg)));
    }
    update();
}

void ScrollCaptureRegion::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyChromeTheme();
    QWidget::changeEvent(event);
}

void ScrollCaptureRegion::setSelection(const QRect &selection, const QRect &bounds) {
    bounds_ = bounds;
    selection_ = selection.intersected(bounds);
    setGeometry(selection_.adjusted(-3, -3, 3, 3));
    updateMask();
    placeHandle();
    update();
}

void ScrollCaptureRegion::updateMask() {
    // A native hole, not merely transparent paint: WindowFromPoint and browser
    // wheel routing must find the page below the center of the capture region.
    // While sampling, keep even the resize hit area outside the captured pixels.
    // One logical pixel of clearance covers fractional-DPI coordinate rounding.
    const QRect hole = running_ ? selection_.translated(-geometry().topLeft()).adjusted(-1, -1, 1, 1)
                                : rect().adjusted(8, 8, -8, -8);
    setMask(QRegion(rect()).subtracted(QRegion(hole)));
}

void ScrollCaptureRegion::setState(bool running, Qt::Orientation axis) {
    running_ = running;
    axis_ = axis;
    styleCaptureToolButton(static_cast<QPushButton *>(handle_),
        !running ? QStringLiteral("move") : axis == Qt::Vertical
            ? QStringLiteral("scroll-move-vertical") : QStringLiteral("scroll-move-horizontal"));
    if (windowFlags().testFlag(Qt::WindowTransparentForInput) != running) {
        const bool visible = isVisible();
        setWindowFlag(Qt::WindowTransparentForInput, running);
        updateMask();
        if (visible) {
            show();
            configureNativeWindow(this, true);
        }
    } else {
        updateMask();
    }
    handle_->setCursor(running ? (axis == Qt::Vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor)
                               : Qt::SizeAllCursor);
    handle_->setToolTip(running ? tr("拖动沿方向微调选区") : tr("拖动移动选区 · 拖边缘调整大小"));
    handle_->setAccessibleName(handle_->toolTip());
    update();
}

void ScrollCaptureRegion::placeHandle() {
    if (selection_.isEmpty())
        return;
    if (anchored_) {
        handle_->move(handleAnchor_);
        return;
    }
    int x = selection_.center().x() - handle_->width() / 2;
    int y = selection_.top() - handle_->height() - 5;
    if (y < bounds_.top())
        y = selection_.bottom() + 5;
    if (y + handle_->height() > bounds_.bottom() + 1)
        y = selection_.top() + 5;
    x = std::clamp(x, bounds_.left(), std::max(bounds_.left(), bounds_.right() + 1 - handle_->width()));
    handle_->move(x, y);
}

void ScrollCaptureRegion::setHandlePosition(QPoint global) {
    const bool newlyAnchored = !anchored_;
    handleAnchor_ = global;
    anchored_ = true;
    handle_->move(global);
    if (newlyAnchored)
        applyChromeTheme();
    if (handle_->isVisible()) configureNativeWindow(handle_, true);
}

void ScrollCaptureRegion::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(accent(), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    if (!running_)
        paintCaptureHandles(painter, QRectF(rect()).adjusted(4, 4, -4, -4));
}

void ScrollCaptureRegion::showEvent(QShowEvent *) {
    handle_->show();
    configureNativeWindow(handle_, true);
}

void ScrollCaptureRegion::hideEvent(QHideEvent *) { handle_->hide(); }

bool ScrollCaptureRegion::eventFilter(QObject *object, QEvent *event) {
    if (object == handle_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                dragEdges_ = {};
                dragOrigin_ = mouse->globalPosition().toPoint();
                dragSelection_ = selection_;
                emit dragStarted();
                handle_->grabMouse();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && dragging_) {
            dragTo(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && dragging_) {
            dragTo(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            dragging_ = false;
            handle_->releaseMouse();
            emit dragFinished();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void ScrollCaptureRegion::mousePressEvent(QMouseEvent *event) {
    if (running_ || event->button() != Qt::LeftButton)
        return;
    const QPoint at = event->position().toPoint();
    dragEdges_ = {};
    if (at.x() < 8) dragEdges_ |= Qt::LeftEdge;
    if (at.x() >= width() - 8) dragEdges_ |= Qt::RightEdge;
    if (at.y() < 8) dragEdges_ |= Qt::TopEdge;
    if (at.y() >= height() - 8) dragEdges_ |= Qt::BottomEdge;
    dragging_ = true;
    dragOrigin_ = event->globalPosition().toPoint();
    dragSelection_ = selection_;
    emit dragStarted();
    event->accept();
}

void ScrollCaptureRegion::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_)
        dragTo(event->globalPosition().toPoint());
}

void ScrollCaptureRegion::mouseReleaseEvent(QMouseEvent *event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragTo(event->globalPosition().toPoint());
    dragging_ = false;
    emit dragFinished();
}

void ScrollCaptureRegion::dragTo(QPoint global) {
    QPoint delta = global - dragOrigin_;
    QRect next = dragSelection_;
    if (dragEdges_ == Qt::Edges{}) {
        if (running_)
            axis_ == Qt::Vertical ? delta.setX(0) : delta.setY(0);
        next.translate(delta);
        next.moveLeft(std::clamp(next.left(), bounds_.left(), bounds_.right() + 1 - next.width()));
        next.moveTop(std::clamp(next.top(), bounds_.top(), bounds_.bottom() + 1 - next.height()));
    } else {
        // Valid native-pixel selections can be smaller in logical pixels on a
        // scaled display. Never make a minimum greater than the starting size.
        const int minimumWidth = std::min(64, dragSelection_.width());
        const int minimumHeight = std::min(120, dragSelection_.height());
        if (dragEdges_.testFlag(Qt::LeftEdge))
            next.setLeft(std::clamp(next.left() + delta.x(), bounds_.left(), next.right() - minimumWidth + 1));
        if (dragEdges_.testFlag(Qt::RightEdge))
            next.setRight(std::clamp(next.right() + delta.x(), next.left() + minimumWidth - 1, bounds_.right()));
        if (dragEdges_.testFlag(Qt::TopEdge))
            next.setTop(std::clamp(next.top() + delta.y(), bounds_.top(), next.bottom() - minimumHeight + 1));
        if (dragEdges_.testFlag(Qt::BottomEdge))
            next.setBottom(std::clamp(next.bottom() + delta.y(), next.top() + minimumHeight - 1, bounds_.bottom()));
    }
    if (next != selection_) {
        setSelection(next, bounds_);
        emit regionChanged(selection_);
    }
}

ScrollCaptureShade::ScrollCaptureShade(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
    setObjectName(QStringLiteral("scrollCaptureShade"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void ScrollCaptureShade::setSelection(const QRect &selection, const QRect &screen) {
    setGeometry(screen);
    selection_ = selection.translated(-screen.topLeft()).intersected(rect());
    const QRegion outside = QRegion(rect()).subtracted(QRegion(selection_.adjusted(-1, -1, 1, 1)));
    // An empty QWidget mask removes clipping. Keep a nonempty mask outside the
    // widget for a full-screen selection, so later show() calls stay harmless.
    setMask(outside.isEmpty() ? QRegion(QRect(-2, -2, 1, 1)) : outside);
    update();
}

void ScrollCaptureShade::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), captureChromeTokens().scrim);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(selection_, Qt::transparent);
}

ScrollCapturePreview::ScrollCapturePreview(QWidget *parent) : QLabel(parent) {
    setObjectName(QStringLiteral("scrollPreview"));
    setAlignment(Qt::AlignCenter);
    setMouseTracking(true);
}

QRectF ScrollCapturePreview::imageRect() const {
    if (fullSize_.isEmpty()) return {};
    if (!visibleRegion_.isEmpty()) {
        const double scale = axis_ == Qt::Vertical ? double(width()) / fullSize_.width()
                                                  : double(height()) / fullSize_.height();
        return QRectF(-visibleRegion_.x() * scale, -visibleRegion_.y() * scale,
                      fullSize_.width() * scale, fullSize_.height() * scale);
    }
    const QSizeF fit = QSizeF(fullSize_).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - fit.width()) / 2, (height() - fit.height()) / 2), fit);
}

QRectF ScrollCapturePreview::viewportRect() const {
    const auto area = imageRect();
    if (area.isEmpty()) return {};
    return QRectF(area.x() + viewport_.x() * area.width() / fullSize_.width(),
                  area.y() + viewport_.y() * area.height() / fullSize_.height(),
                  viewport_.width() * area.width() / fullSize_.width(),
                  viewport_.height() * area.height() / fullSize_.height())
        .intersected(area).intersected(QRectF(rect()));
}

void ScrollCapturePreview::setCapture(const QImage &image, QSize fullSize, const QRect &viewport,
                                     bool matched, Qt::Orientation axis, const QRect &imageSource) {
    const bool sameAxis = axis == axis_;
    const int oldLength = axis == Qt::Vertical ? fullSize_.height() : fullSize_.width();
    const int oldPosition = axis == Qt::Vertical ? viewport_.y() : viewport_.x();
    if (fullSize != fullSize_ || axis != axis_) {
        if (!autoCrop_ || !sameAxis || fullSize.isEmpty()) cropBegin_ = cropEnd_ = 0;
        cropEdge_ = -1;
        unsetCursor();
    }
    fullSize_ = fullSize;
    imageSource_ = imageSource.isEmpty() ? QRect(QPoint(), fullSize) : imageSource;
    viewport_ = viewport.isEmpty() ? QRect(QPoint(), fullSize) : viewport;
    matched_ = matched;
    axis_ = axis;
    const int newLength = axis == Qt::Vertical ? fullSize.height() : fullSize.width();
    const int newPosition = axis == Qt::Vertical ? viewport_.y() : viewport_.x();
    if (sameAxis && matched && oldLength > 0 && newLength > oldLength)
        knownGrowthDirection_ = newPosition == 0 ? -1 : 1;
    if (autoCrop_ && matched && !fullSize.isEmpty() && sameAxis && oldLength > 0) {
        const int length = axis == Qt::Vertical ? fullSize.height() : fullSize.width();
        const int position = axis == Qt::Vertical ? viewport_.y() : viewport_.x();
        const int viewLength = axis == Qt::Vertical ? viewport_.height() : viewport_.width();
        int movement = position - oldPosition;
        // Prepending shifts the origin, so a viewport at zero has still moved up.
        if (length > oldLength && position == 0) movement = -(length - oldLength);
        if (!growthDirection_ && movement) growthDirection_ = movement > 0 ? 1 : -1;
        if (growthDirection_ > 0) cropEnd_ = std::max(0, length - position - viewLength);
        if (growthDirection_ < 0) cropBegin_ = position;
        cropBegin_ = std::clamp(cropBegin_, 0, std::max(0, length - 1));
        cropEnd_ = std::clamp(cropEnd_, 0, std::max(0, length - cropBegin_ - 1));
        if (length - cropBegin_ - cropEnd_ <= viewLength) growthDirection_ = 0;
    }
    if (!sameAxis || fullSize.isEmpty()) growthDirection_ = knownGrowthDirection_ = 0;
    setProperty("captureViewport", viewport_);
    setProperty("captureMatched", matched_);
    setPixmap(QPixmap::fromImage(image));
    update();
}

void ScrollCapturePreview::setVisibleRegion(const QRect &region) {
    visibleRegion_ = region;
    setProperty("captureSourceRect", region);
    update();
}

QRect ScrollCapturePreview::croppedRect() const {
    return axis_ == Qt::Vertical
        ? QRect(0, cropBegin_, fullSize_.width(), fullSize_.height() - cropBegin_ - cropEnd_)
        : QRect(cropBegin_, 0, fullSize_.width() - cropBegin_ - cropEnd_, fullSize_.height());
}

void ScrollCapturePreview::clearCrop() {
    cropBegin_ = cropEnd_ = 0;
    cropEdge_ = -1;
    growthDirection_ = 0;
    unsetCursor();
    update();
}

void ScrollCapturePreview::setAutoCrop(bool enabled) {
    autoCrop_ = enabled;
    clearCrop();
    if (enabled) growthDirection_ = knownGrowthDirection_;
    emit cropChanged();
}

void ScrollCapturePreview::beginCrop(bool end) {
    cropEdge_ = end ? 1 : 0;
    setCursor(axis_ == Qt::Vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor);
    update();
}

void ScrollCapturePreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    const auto t = captureChromeTokens();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), t.controlRadius, t.controlRadius);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipPath(clip);
    painter.fillRect(rect(), t.stage);
    const auto area = imageRect();
    if (area.isEmpty() || pixmap().isNull()) return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double xScale = area.width() / fullSize_.width();
    const double yScale = area.height() / fullSize_.height();
    const QRectF target(area.x() + imageSource_.x() * xScale,
                        area.y() + imageSource_.y() * yScale,
                        imageSource_.width() * xScale, imageSource_.height() * yScale);
    const QRectF visible = target.intersected(QRectF(rect()));
    if (!visible.isEmpty()) {
        const QRectF source((visible.x() - target.x()) * pixmap().width() / target.width(),
                            (visible.y() - target.y()) * pixmap().height() / target.height(),
                            visible.width() * pixmap().width() / target.width(),
                            visible.height() * pixmap().height() / target.height());
        painter.drawPixmap(visible, pixmap(), source);
    }
    const int length = axis_ == Qt::Vertical ? fullSize_.height() : fullSize_.width();
    const double scale = (axis_ == Qt::Vertical ? area.height() : area.width()) / length;
    QRectF begin = area, end = area;
    if (axis_ == Qt::Vertical) {
        begin.setHeight(cropBegin_ * scale);
        end.setTop(area.bottom() - cropEnd_ * scale);
    } else {
        begin.setWidth(cropBegin_ * scale);
        end.setLeft(area.right() - cropEnd_ * scale);
    }
    painter.fillRect(begin, QColor(0, 0, 0, 155));
    painter.fillRect(end, QColor(0, 0, 0, 155));
    painter.setBrush(matched_ ? t.successSoft : t.warningSoft);
    painter.setPen(QPen(matched_ ? t.success : t.warning, 2));
    const QRectF locator = viewportRect().adjusted(1, 1, -1, -1);
    if (!locator.isEmpty())
        painter.drawRoundedRect(locator, 3, 3);
    if (cropBegin_ || cropEnd_ || cropEdge_ >= 0) {
        painter.setPen(QPen(t.accent, 1.5, Qt::DashLine));
        if (axis_ == Qt::Vertical) {
            painter.drawLine(begin.bottomLeft(), begin.bottomRight());
            painter.drawLine(end.topLeft(), end.topRight());
        } else {
            painter.drawLine(begin.topRight(), begin.bottomRight());
            painter.drawLine(end.topLeft(), end.bottomLeft());
        }
    }
}

void ScrollCapturePreview::cropAt(QPointF point) {
    const auto area = imageRect();
    if (area.isEmpty() || cropEdge_ < 0) return;
    const int length = axis_ == Qt::Vertical ? fullSize_.height() : fullSize_.width();
    const double fraction = axis_ == Qt::Vertical ? (point.y() - area.y()) / area.height()
                                                : (point.x() - area.x()) / area.width();
    const int at = qRound(std::clamp(fraction, 0.0, 1.0) * length);
    if (cropEdge_ == 0)
        cropBegin_ = std::clamp(at, 0, std::max(0, length - cropEnd_ - 1));
    else
        cropEnd_ = std::clamp(length - at, 0, std::max(0, length - cropBegin_ - 1));
    update();
    emit cropChanged();
}

void ScrollCapturePreview::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && cropEdge_ >= 0) {
        cropping_ = true;
        cropAt(event->position());
        event->accept();
    }
}
void ScrollCapturePreview::mouseMoveEvent(QMouseEvent *event) {
    if (cropping_) cropAt(event->position());
}
void ScrollCapturePreview::mouseReleaseEvent(QMouseEvent *event) {
    if (cropping_) {
        cropAt(event->position());
        cropping_ = false;
    }
}

ScrollCaptureProgress::ScrollCaptureProgress(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setObjectName(QStringLiteral("scrollCaptureProgress"));
    setWindowTitle(tr("EditHere · 长截图"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AlwaysShowToolTips);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(captureBarHeight);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(4);
    dragBar_ = new ToolbarGrip(false, {}, this);
    dragBar_->setToolTip(tr("拖动工具栏"));
    dragBar_->installEventFilter(this);
    layout->addWidget(dragBar_);
    size_ = new QLabel(QStringLiteral("0 × 0 px"), this);
    size_->setObjectName(QStringLiteral("scrollSize"));
    size_->setAlignment(Qt::AlignCenter);
    size_->setMinimumWidth(72);
    size_->setProperty("captureReadout", true);
    size_->setToolTip(tr("当前长图尺寸（像素）"));
    size_->installEventFilter(this);
    layout->addWidget(size_);
    layout->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    direction_ = new QPushButton(this);
    direction_->setObjectName(QStringLiteral("scrollDirection"));
    direction_->setProperty("captureChoice", true);
    direction_->setToolTip(tr("切换滚动方向"));
    direction_->setAccessibleName(tr("切换滚动方向"));
    direction_->setCursor(Qt::PointingHandCursor);
    direction_->setFixedHeight(28);
    direction_->installEventFilter(this);
    auto *axisMenu = new QMenu(direction_);
    direction_->setMenu(axisMenu);
    for (const auto axis : {Qt::Vertical, Qt::Horizontal}) {
        auto *action = axisMenu->addAction(axis == Qt::Vertical ? tr("垂直") : tr("水平"));
        action->setObjectName(axis == Qt::Vertical ? "scrollVerticalAction" : "scrollHorizontalAction");
        action->setCheckable(true);
        action->setData(static_cast<int>(axis));
        connect(action, &QAction::triggered, this, [this, axis] {
            if (direction_->property("captureAxis").toInt() != static_cast<int>(axis))
                emit directionRequested();
        });
    }
    layout->addWidget(direction_);
    // Reserve a slot for the selection handle's separate native window.
    moveSlot_ = new QWidget(this);
    moveSlot_->setFixedSize(captureButtonSize, captureButtonSize);
    layout->addWidget(moveSlot_);
    automatic_ = new QCheckBox(this);
    automatic_->setObjectName(QStringLiteral("scrollAutomatic"));
    automatic_->setToolTip(tr("自动滚动"));
    automatic_->hide();
    automaticButton_ = new QPushButton(this);
    automaticButton_->setObjectName(QStringLiteral("scrollAutomaticButton"));
    styleCaptureToolButton(automaticButton_, QStringLiteral("scroll-mouse"));
    automaticButton_->setCheckable(true);
    automaticButton_->setToolTip(automatic_->toolTip());
    automaticButton_->setAccessibleName(tr("自动滚动（默认手动滚动）"));
    automaticButton_->installEventFilter(this);
    layout->addWidget(automaticButton_);
    connect(automaticButton_, &QPushButton::clicked, automatic_, &QCheckBox::setChecked);
    connect(automatic_, &QCheckBox::toggled, automaticButton_, &QPushButton::setChecked);
    connect(automaticButton_, &QPushButton::toggled, this, [this](bool) {
        styleCaptureToolButton(automaticButton_, QStringLiteral("scroll-mouse"));
    });
    cropButton_ = new QPushButton(this);
    cropButton_->setObjectName(QStringLiteral("scrollCrop"));
    styleCaptureToolButton(cropButton_, QStringLiteral("crop"));
    cropButton_->setToolTip(tr("裁剪长截图"));
    cropButton_->setAccessibleName(tr("裁剪长截图"));
    cropButton_->installEventFilter(this);
    layout->addWidget(cropButton_);
    stop_ = new QPushButton(this);
    stop_->setObjectName(QStringLiteral("scrollStop"));
    styleCaptureToolButton(stop_, QStringLiteral("scroll-stop"));
    stop_->setToolTip(tr("停止长截图"));
    stop_->setAccessibleName(tr("停止长截图"));
    stop_->installEventFilter(this);
    layout->addWidget(stop_);
    layout->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    finish_ = new QPushButton(this);
    finish_->setObjectName(QStringLiteral("scrollFinish"));
    finish_->setProperty("primary", true);
    styleCaptureToolButton(finish_, QStringLiteral("edit"));
    finish_->setToolTip(tr("批注"));
    finish_->setAccessibleName(tr("批注"));
    finish_->setEnabled(false);
    finish_->installEventFilter(this);
    layout->addWidget(finish_);
    const QStringList labels = {QStringLiteral("pin"), QStringLiteral("image-save"), QStringLiteral("image-copy")};
    const QStringList tips = {tr("贴图"), tr("保存图片"), tr("复制图像")};
    const QStringList accessibleNames = {tr("贴图"), tr("保存图片"), tr("复制图像")};
    const QStringList names = {QStringLiteral("scrollPin"), QStringLiteral("scrollSave"),
                               QStringLiteral("scrollCopy")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(this);
        button->setObjectName(names[i]);
        styleCaptureToolButton(button, labels[i]);
        button->setToolTip(tips[i]);
        button->setAccessibleName(accessibleNames[i]);
        button->setEnabled(false);
        button->installEventFilter(this);
        layout->addWidget(button);
        outputButtons_.append(button);
    }
    layout->addWidget(captureSeparator(this), 0, Qt::AlignVCenter);
    cancel_ = new QPushButton(this);
    cancel_->setObjectName(QStringLiteral("scrollCancel"));
    styleCaptureToolButton(cancel_, QStringLiteral("close"));
    cancel_->setToolTip(tr("取消截图（Esc）"));
    cancel_->setAccessibleName(tr("取消截图"));
    cancel_->installEventFilter(this);
    layout->addWidget(cancel_);
    adjustSize();
    setFixedWidth(std::max(536, sizeHint().width()));

    // The thumbnail is its own top-level surface. It stays beside the capture
    // frame while running and sits strictly below this bar so tooltips never go behind it.
    previewArea_ = new QScrollArea(this);
    previewArea_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                 Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    previewArea_->setObjectName(QStringLiteral("scrollPreviewArea"));
    previewArea_->setAttribute(Qt::WA_ShowWithoutActivating);
    previewArea_->setAttribute(Qt::WA_TranslucentBackground);
    previewArea_->viewport()->setAutoFillBackground(false);
    previewArea_->setFixedSize(150, 260);
    previewArea_->setFrameShape(QFrame::NoFrame);
    previewArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview_ = new ScrollCapturePreview;
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    previewArea_->setWidget(preview_);
    previewArea_->setWidgetResizable(true);

    cropMenu_ = new RoundedCropPopup(this);
    cropMenu_->setObjectName(QStringLiteral("scrollCropMenu"));
    cropMenu_->setMinimumSize(194, 48);
    auto *cropLayout = new QHBoxLayout(cropMenu_);
    cropLayout->setContentsMargins(8, 5, 8, 5);
    cropLayout->setSpacing(4);
    cropBegin_ = new QPushButton(cropMenu_);
    cropBegin_->setObjectName(QStringLiteral("scrollCropBegin"));
    cropEnd_ = new QPushButton(cropMenu_);
    cropEnd_->setObjectName(QStringLiteral("scrollCropEnd"));
    for (auto *button : {cropBegin_, cropEnd_})
        button->installEventFilter(this);
    cropLayout->addWidget(cropBegin_);
    cropLayout->addWidget(cropEnd_);
    cropLayout->addWidget(captureSeparator(cropMenu_), 0, Qt::AlignVCenter);
    autoCrop_ = new QCheckBox(tr("自动裁剪"), cropMenu_);
    autoCrop_->setObjectName(QStringLiteral("scrollAutoCrop"));
    autoCrop_->setToolTip(tr("反向滚动时自动裁剪超出区域的内容"));
    autoCrop_->setAccessibleName(tr("自动裁剪"));
    autoCrop_->installEventFilter(this);
    cropLayout->addWidget(autoCrop_);
    connect(autoCrop_, &QCheckBox::toggled, this, &ScrollCaptureProgress::autoCropChanged);
    connect(cropButton_, &QPushButton::clicked, this, [this] {
        cropMenu_->adjustSize();
        QPoint position = cropButton_->mapToGlobal(QPoint(cropButton_->width() / 2 -
            cropMenu_->width() / 2, height() - cropButton_->y() + 4));
        if (!captureScreen_.isEmpty()) {
            position.setX(std::clamp(position.x(), captureScreen_.left(),
                std::max(captureScreen_.left(), captureScreen_.right() + 1 - cropMenu_->width())));
            if (position.y() + cropMenu_->height() > captureScreen_.bottom() + 1)
                position.setY(std::max(captureScreen_.top(), y() - cropMenu_->height() - 4));
        }
        cropMenu_->move(position);
        cropMenu_->show();
        configureNativeWindow(cropMenu_, true);
        if (previewArea_->isVisible())
            orderWindowBelow(previewArea_, cropMenu_);
    });
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("scrollStatus"));
    status_->hide();
    setAxis(Qt::Vertical);
    applyChromeTheme();
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (stopped_) emit resumeRequested();
        else emit stopRequested();
    });
    connect(finish_, &QPushButton::clicked, this, &ScrollCaptureProgress::finishRequested);
    connect(cancel_, &QPushButton::clicked, this, &ScrollCaptureProgress::cancelRequested);
    connect(automatic_, &QCheckBox::toggled, this, &ScrollCaptureProgress::automaticChanged);
    connect(cropBegin_, &QPushButton::clicked, this, [this] {
        cropMenu_->hide();
        emit cropRequested(false);
    });
    connect(cropEnd_, &QPushButton::clicked, this, [this] {
        cropMenu_->hide();
        emit cropRequested(true);
    });
    connect(preview_, &ScrollCapturePreview::cropChanged, this, [this] {
        const auto cropped = croppedRect();
        setSelectionSize(cropped.size());
    });
    connect(outputButtons_[0], &QPushButton::clicked, this, &ScrollCaptureProgress::pinRequested);
    connect(outputButtons_[1], &QPushButton::clicked, this, &ScrollCaptureProgress::saveRequested);
    connect(outputButtons_[2], &QPushButton::clicked, this, &ScrollCaptureProgress::copyRequested);
}

void ScrollCaptureProgress::applyChromeTheme() {
    ChromeApplyGuard guard(this);
    if (!guard.entered)
        return;
    const auto t = captureChromeTokens();
    setStyleSheet(captureBarStyle(t));
    if (previewArea_) {
        previewArea_->setStyleSheet(QStringLiteral(
            "QScrollArea#scrollPreviewArea { border: 1px solid %1; background: %2;"
            " border-radius: %3px; padding: 1px; }")
            .arg(rgbaCss(t.panelBorder), t.stage.name()).arg(CaptureChromeTokens::cardRadius));
        preview_->update();
    }
    if (auto *grip = static_cast<ToolbarGrip *>(dragBar_))
        grip->refreshTheme();
    if (cropMenu_)
        static_cast<RoundedCropPopup *>(cropMenu_)->applyTheme();
    for (QPushButton *button : {automaticButton_, cropButton_, stop_, finish_, cancel_, cropBegin_, cropEnd_}) {
        if (button && !button->property("glyphName").toString().isEmpty())
            styleCaptureToolButton(button, button->property("glyphName").toString());
    }
    for (auto *button : outputButtons_) {
        if (button && !button->property("glyphName").toString().isEmpty())
            styleCaptureToolButton(button, button->property("glyphName").toString());
    }
    update();
}

void ScrollCaptureProgress::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyChromeTheme();
    QWidget::changeEvent(event);
}

void ScrollCaptureProgress::syncPreviewOverlay(bool ensureVisible) {
    if (!previewArea_)
        return;
    if (ensureVisible && !previewArea_->isVisible()) {
        previewArea_->show();
        configureNativeWindow(previewArea_, true);
    }
    if (previewArea_->isVisible())
        orderWindowBelow(previewArea_, cropMenu_ && cropMenu_->isVisible() ? cropMenu_ : this);
}

bool ScrollCaptureProgress::eventFilter(QObject *watched, QEvent *event) {
    if ((event->type() == QEvent::Enter || event->type() == QEvent::ToolTip) &&
        previewArea_ && previewArea_->isVisible()) {
        orderWindowBelow(previewArea_, cropMenu_ && cropMenu_->isVisible() ? cropMenu_ : this);
    }
    return QWidget::eventFilter(watched, event);
}

void ScrollCaptureProgress::placeBeside(const QRect &selection, const QRect &screen) {
    if (!captureSelection_.isEmpty() && !previewLastSize_.isEmpty())
        previewAnchor_ += selection.topLeft() - captureSelection_.topLeft();
    captureSelection_ = selection;
    captureScreen_ = screen;
    move(captureBarPosition(selection, screen, size()));
    previewMaximumWidth_ = 196;
    if (!previewLastSize_.isEmpty())
        previewSourceRect(previewLastSize_, previewLastViewport_, previewAxis_);
}

QRect ScrollCaptureProgress::previewSourceRect(QSize nativeSize, const QRect &viewport,
                                               Qt::Orientation axis, std::optional<int> nativeOrigin) {
    if (captureSelection_.isEmpty() || captureScreen_.isEmpty() || nativeSize.isEmpty()) return {};
    const bool vertical = axis == Qt::Vertical;
    const int cross = vertical ? nativeSize.width() : nativeSize.height();
    const double scale = double(previewMaximumWidth_) / cross;
    const int length = vertical ? nativeSize.height() : nativeSize.width();
    const int position = vertical ? viewport.y() : viewport.x();
    if (previewLastSize_.isEmpty() || axis != previewAxis_) {
        previewAnchor_ = QPointF(captureSelection_.right() + 14, captureSelection_.top());
        previewLastOrigin_ = nativeOrigin;
    } else if (nativeOrigin && previewLastOrigin_) {
        // Fixed bars can be discovered on the first movement. The stitch's
        // native origin distinguishes prepend from append without inferring it
        // from the viewport, and includes actual prefix removal when cropping.
        const double movement = (*nativeOrigin - *previewLastOrigin_) * scale;
        if (vertical) previewAnchor_.ry() += movement;
        else previewAnchor_.rx() += movement;
    } else {
        const int oldLength = vertical ? previewLastSize_.height() : previewLastSize_.width();
        const int oldPosition = vertical ? previewLastViewport_.y() : previewLastViewport_.x();
        const int growth = length - oldLength;
        if ((growth > 0 && position <= oldPosition) || (growth < 0 && position < oldPosition)) {
            if (vertical) previewAnchor_.ry() -= growth * scale;
            else previewAnchor_.rx() -= growth * scale;
        }
    }
    if (nativeOrigin) previewLastOrigin_ = nativeOrigin;
    previewLastSize_ = nativeSize;
    previewLastViewport_ = viewport;
    previewAxis_ = axis;
    const int right = captureSelection_.right() + 14;
    const int rightSpace = std::max(0, captureScreen_.right() + 1 - right);
    const int leftSpace = std::max(0, captureSelection_.left() - 14 - captureScreen_.left());
    const int availableWidth = std::max(6, std::max(rightSpace, leftSpace) - 4);
    const int availableHeight = std::max(6, captureScreen_.height() - 16);
    const int viewWidth = vertical ? std::min(previewMaximumWidth_, std::max(1, captureScreen_.width() - 4))
                                  : std::min(availableWidth, std::max(1, qRound(nativeSize.width() * scale)));
    const int viewHeight = vertical ? std::min(availableHeight, std::max(1, qRound(length * scale)))
                                   : std::min(previewMaximumWidth_, availableHeight);
    previewArea_->setFixedSize(viewWidth + 4, viewHeight + 4);
    const int x = rightSpace >= previewArea_->width() ? right : leftSpace >= previewArea_->width() ?
        captureSelection_.left() - 14 - previewArea_->width() :
        std::clamp(right, captureScreen_.left(), std::max(captureScreen_.left(),
            captureScreen_.right() + 1 - previewArea_->width()));
    const int y = std::clamp(qRound(previewAnchor_.y()), captureScreen_.top() + 6,
        std::max(captureScreen_.top() + 6, captureScreen_.bottom() - previewArea_->height() - 5));
    previewArea_->move(x, y);
    preview_->resize(previewArea_->viewport()->size());
    // QScrollArea can still have its old viewport geometry until its resize
    // event is delivered. Derive the request from the new display dimensions.
    const double actualScale = vertical ? double(viewWidth) / nativeSize.width()
                                        : double(viewHeight) / nativeSize.height();
    const int visibleLength = std::min(length, std::max(1, int(std::ceil(
        (vertical ? viewHeight : viewWidth) / actualScale))));
    const double origin = vertical ? previewAnchor_.y() : previewAnchor_.x();
    const int windowStart = vertical ? y + 2 : x + 2;
    int first = std::clamp(qRound((windowStart - origin) / actualScale), 0, length - visibleLength);
    // Once the image reaches a screen edge, pan the native-pixel window so the
    // current capture stays visible; its width and scale never shrink.
    const int viewLength = vertical ? viewport.height() : viewport.width();
    if (!viewport.isEmpty()) {
        if (position < first) first = position;
        if (position + viewLength > first + visibleLength)
            first = position + viewLength - visibleLength;
        first = std::clamp(first, 0, length - visibleLength);
    }
    const QRect source = vertical ? QRect(0, first, nativeSize.width(), visibleLength)
                                  : QRect(first, 0, visibleLength, nativeSize.height());
    preview_->setVisibleRegion(source);
    previewArea_->setProperty("captureSourceRect", source);
    return source;
}

QPoint ScrollCaptureProgress::moveHandlePosition() const {
    return moveSlot_->mapToGlobal(QPoint());
}

int ScrollCaptureProgress::previewWidth() const {
    return qRound(std::max(1, previewArea_->width() - 4) * devicePixelRatioF());
}

void ScrollCaptureProgress::setProgress(const QImage &image, int addedFrames, QSize size, const QRect &viewport,
                                      bool matched, Qt::Orientation axis, const QRect &imageSource) {
    hasProgress_ = addedFrames > 0;
    direction_->setEnabled(true);
    const QSize dimensions = size.isEmpty() ? image.size() : size;
    canEdit_ = std::max(dimensions.width(), dimensions.height()) <= 32767 &&
               qint64(dimensions.width()) * dimensions.height() <= 32000000;
    finish_->setEnabled(hasProgress_ && canEdit_);
    cropBegin_->setEnabled(hasProgress_);
    cropEnd_->setEnabled(hasProgress_);
    for (auto *button : outputButtons_) button->setEnabled(hasProgress_);
    outputButtons_[0]->setEnabled(hasProgress_ && canEdit_);
    outputButtons_[2]->setEnabled(hasProgress_ && canEdit_);
    const QString largeNotice = tr("超大长图请使用 PNG 保存；当前尺寸超过复制、贴图和编辑的图像上限。");
    finish_->setToolTip(canEdit_ ? tr("批注") : largeNotice);
    outputButtons_[0]->setToolTip(canEdit_ ? tr("贴图") : largeNotice);
    outputButtons_[2]->setToolTip(canEdit_ ? tr("复制图像") : largeNotice);
    if (size.isEmpty())
        size = image.size();
    if (!image.isNull()) {
        previewSourceRect(size, viewport, axis);
        preview_->resize(previewArea_->viewport()->size());
        preview_->setCapture(image, size, viewport, matched, axis, imageSource);
        if (!stopped_ && isVisible())
            syncPreviewOverlay(true);
    } else {
        preview_->setCapture({}, {}, {}, matched, axis);
        preview_->setVisibleRegion({});
        previewLastSize_ = {};
        previewLastOrigin_.reset();
        previewArea_->hide();
    }
    if (!preview_->croppedRect().isEmpty()) size = preview_->croppedRect().size();
    if (!size.isEmpty()) setSelectionSize(size);
    status_->setText((automatic_->isChecked()
                         ? tr("正在自动滚动 · %1 × %2 px\n已追加 %3 帧，随时可停止")
                         : (axis == Qt::Vertical
                            ? tr("请在选区内上下滚动页面\n%1 × %2 px · 已追加 %3 帧")
                            : tr("请在选区内左右滚动或拖动水平滚动条\n%1 × %2 px · 已追加 %3 帧")))
                         .arg(size.width()).arg(size.height()).arg(addedFrames));
    setToolTip(status_->text());
}

void ScrollCaptureProgress::setNotice(const QString &message) {
    status_->setText(message);
    setToolTip(message);
}

void ScrollCaptureProgress::setAutomatic(bool automatic) {
    const QSignalBlocker blocked(automatic_);
    automatic_->setChecked(automatic);
    automaticButton_->setChecked(automatic);
}
void ScrollCaptureProgress::setAutomaticSupport(bool available, const QString &reason) {
    automaticSupported_ = available;
    automaticUnavailableReason_ = reason;
    if (!available) setAutomatic(false);
    const auto current = direction_->property("captureAxis");
    setAxis(current.isValid() ? static_cast<Qt::Orientation>(current.toInt()) : Qt::Vertical);
}

void ScrollCaptureProgress::setStopped(const QString &message) {
    status_->setText(message);
    setAutomatic(false);
    stopped_ = true;
    automatic_->setEnabled(false);
    automaticButton_->setEnabled(false);
    automaticButton_->setToolTip(tr("重新开始长截图后可启用自动滚动"));
    stop_->setText({});
    styleCaptureToolButton(stop_, QStringLiteral("scroll-play"));
    stop_->setToolTip(tr("重新开始长截图"));
    stop_->setAccessibleName(tr("开始长截图"));
    setToolTip(message);
    previewArea_->hide();
    finish_->show();
    finish_->setEnabled(hasProgress_ && canEdit_);
}

void ScrollCaptureProgress::setRunning(Qt::Orientation axis) {
    stopped_ = false;
    stop_->setText({});
    styleCaptureToolButton(stop_, QStringLiteral("scroll-stop"));
    stop_->setToolTip(tr("停止长截图"));
    stop_->setAccessibleName(tr("停止长截图"));
    automatic_->setEnabled(axis == Qt::Vertical);
    setAxis(axis);
    if (isVisible() && !preview_->pixmap().isNull())
        syncPreviewOverlay(true);
}

void ScrollCaptureProgress::setAxis(Qt::Orientation axis) {
    direction_->setProperty("captureAxis", static_cast<int>(axis));
    direction_->setText(axis == Qt::Vertical ? tr("垂直") : tr("水平"));
    const auto metrics = direction_->fontMetrics();
    direction_->setFixedWidth(std::max(76, 40 + std::max(metrics.horizontalAdvance(tr("垂直")),
                                                        metrics.horizontalAdvance(tr("水平")))));
    for (auto *action : direction_->menu()->actions())
        action->setChecked(action->data().toInt() == static_cast<int>(axis));
    cropBegin_->setText({});
    cropEnd_->setText({});
    styleCaptureToolButton(cropBegin_, axis == Qt::Vertical ? QStringLiteral("scroll-trim-top") : QStringLiteral("scroll-trim-left"));
    styleCaptureToolButton(cropEnd_, axis == Qt::Vertical ? QStringLiteral("scroll-trim-bottom") : QStringLiteral("scroll-trim-right"));
    cropBegin_->setToolTip(axis == Qt::Vertical ? tr("裁去上方内容，保留当前区域及下方")
                                              : tr("裁去左侧内容，保留当前区域及右侧"));
    cropEnd_->setToolTip(axis == Qt::Vertical ? tr("裁去下方内容，保留当前区域及上方")
                                            : tr("裁去右侧内容，保留当前区域及左侧"));
    cropBegin_->setAccessibleName(axis == Qt::Vertical ? tr("上裁剪") : tr("左裁剪"));
    cropEnd_->setAccessibleName(axis == Qt::Vertical ? tr("下裁剪") : tr("右裁剪"));
    automatic_->setEnabled(!stopped_ && automaticSupported_ && axis == Qt::Vertical);
    automaticButton_->setEnabled(!stopped_ && automaticSupported_ && axis == Qt::Vertical);
    automaticButton_->setToolTip(stopped_ ? tr("重新开始长截图后可启用自动滚动") :
        !automaticSupported_ ? automaticUnavailableReason_ :
        axis == Qt::Vertical ? tr("自动向下滚动并拼接") :
                              tr("水平截图请手动滚动或拖动滚动条"));
    automatic_->setToolTip(automaticButton_->toolTip());
    if (axis == Qt::Horizontal) setAutomatic(false);
}

void ScrollCaptureProgress::setSelectionSize(QSize size) {
    size_->setText(QStringLiteral("%1 × %2 px").arg(size.width()).arg(size.height()));
    size_->setFixedWidth(std::max(72, size_->fontMetrics().horizontalAdvance(size_->text()) + 10));
    setFixedWidth(std::max(536, layout()->sizeHint().width()));
    layout()->activate();
    if (!captureScreen_.isEmpty())
        move(std::clamp(x(), captureScreen_.left(), std::max(captureScreen_.left(),
            captureScreen_.right() + 1 - width())), y());
    emit positionChanged(moveHandlePosition());
}

void ScrollCaptureProgress::setAutoCrop(bool enabled) {
    autoCrop_->setChecked(enabled);
}

bool ScrollCaptureProgress::autoCrop() const { return autoCrop_->isChecked(); }

void ScrollCaptureProgress::beginCrop(bool end) {
    preview_->beginCrop(end);
    status_->setText(tr("在预览中点击并拖动，调整蓝色裁剪线。\n点击另一端裁剪按钮可切换边界。"));
}

QRect ScrollCaptureProgress::croppedRect() const { return preview_->croppedRect(); }
void ScrollCaptureProgress::clearCrop() { preview_->clearCrop(); }

void ScrollCaptureProgress::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintFloatingPanel(painter, rect());
}

void ScrollCaptureProgress::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (moveSlot_) emit positionChanged(moveHandlePosition());
}

void ScrollCaptureProgress::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!stopped_ && !preview_->pixmap().isNull())
        syncPreviewOverlay(true);
}

void ScrollCaptureProgress::hideEvent(QHideEvent *event) {
    previewArea_->hide();
    cropMenu_->hide();
    QWidget::hideEvent(event);
}

void ScrollCaptureProgress::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit cancelRequested();
        event->accept();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
               finish_->isEnabled() && finish_->isVisible() && hasProgress_) {
        emit finishRequested();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ScrollCaptureProgress::closeEvent(QCloseEvent *event) {
    event->ignore();
    emit cancelRequested();
}

QSize CaptureToolbar::askForSize(QWidget *parent, QSize current, bool *accepted) {
    QDialog dialog(parent);
    dialog.setWindowTitle(tr("输入选区尺寸"));
    auto *form = new QFormLayout(&dialog);
    auto *width = new QSpinBox(&dialog);
    width->setRange(1, 100000);
    width->setValue(current.width());
    width->setSuffix(tr(" 像素"));
    auto *height = new QSpinBox(&dialog);
    height->setRange(1, 100000);
    height->setValue(current.height());
    height->setSuffix(tr(" 像素"));
    form->addRow(tr("宽"), width);
    form->addRow(tr("高"), height);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr)
        *accepted = ok;
    return {width->value(), height->value()};
}

void CaptureToolbar::chooseSize() {
    bool accepted = false;
    const QSize current = size_->property("pixels").toSize();
    const QSize wanted = askForSize(this, current.isEmpty() ? QSize(100, 100) : current, &accepted);
    if (!accepted)
        return;
    // The window reshapes the selection and tells the bar what came of it, so a
    // locked ratio can never be contradicted by the two numbers that were typed.
    emit sizeRequested(wanted);
}

void CaptureToolbar::setRatio(CaptureRatio ratio, int customWidth, int customHeight) {
    ratioChoice_ = ratio;
    customWidth_ = std::max(1, customWidth);
    customHeight_ = std::max(1, customHeight);
    ratio_->setText(captureRatioLabel(ratioChoice_, customWidth_, customHeight_));
    ratio_->setFixedWidth(ratio_->fontMetrics().horizontalAdvance(ratio_->text()) + 34);
    for (auto *action : ratio_->menu()->actions())
        action->setChecked(action->data().toInt() == int(ratioChoice_));
    adjustSize();
    emit ratioChanged();
}

void CaptureToolbar::populateRatioMenu(QMenu *menu) {
    const QVector<CaptureRatio> choices{CaptureRatio::Free,    CaptureRatio::Square,
                                        CaptureRatio::FourThree, CaptureRatio::ThreeTwo,
                                        CaptureRatio::SixteenNine, CaptureRatio::NineSixteen};
    for (const auto choice : choices) {
        auto *action = menu->addAction(captureRatioLabel(choice, customWidth_, customHeight_));
        action->setCheckable(true);
        action->setData(int(choice));
        action->setChecked(ratioChoice_ == choice);
        connect(action, &QAction::triggered, this, [this, choice] { setRatio(choice); });
    }
    auto *custom = menu->addAction(tr("自定义…"));
    custom->setCheckable(true);
    custom->setData(int(CaptureRatio::Custom));
    custom->setChecked(ratioChoice_ == CaptureRatio::Custom);
    connect(custom, &QAction::triggered, this, [this, custom] {
        custom->setChecked(ratioChoice_ == CaptureRatio::Custom);
        bool accepted = false;
        const QString current = QStringLiteral("%1:%2").arg(customWidth_).arg(customHeight_);
        const QString text = QInputDialog::getText(this, tr("自定义比例"), tr("宽:高，例如 21:9"),
                                                  QLineEdit::Normal, current, &accepted);
        if (!accepted)
            return;
        const auto parts = text.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (parts.size() != 2)
            return;
        bool left = false, right = false;
        const int width = parts.at(0).trimmed().toInt(&left);
        const int height = parts.at(1).trimmed().toInt(&right);
        if (left && right && width > 0 && height > 0)
            setRatio(CaptureRatio::Custom, width, height);
    });
}

void CaptureToolbar::showMarkupOptions() {
    if (busy_ || markupTool_ == CaptureMarkupTool::None)
        return;
    QPointer<QMenu> menu = new QMenu(markupSettings_);
    const auto choice = [this](QMenu *parent, const QString &label, bool selected, auto apply) {
        auto *action = parent->addAction(label, this, std::move(apply));
        action->setCheckable(true);
        action->setChecked(selected);
    };
    if (markupTool_ == CaptureMarkupTool::Brush) {
        choice(menu, tr("画笔"), !markupOptions_.highlighter,
               [this] { markupOptions_.highlighter = false; });
        choice(menu, tr("荧光笔"), markupOptions_.highlighter,
               [this] { markupOptions_.highlighter = true; });
        auto *colorMenu = menu->addMenu(tr("颜色"));
        const QList<QPair<QString, QColor>> colors{
            {tr("红色"), QColor("#ff453a")}, {tr("黄色"), QColor("#ffd60a")},
            {tr("绿色"), QColor("#30d158")}, {tr("蓝色"), QColor("#0a84ff")},
            {tr("白色"), Qt::white}, {tr("黑色"), Qt::black}};
        for (const auto &[label, color] : colors)
            choice(colorMenu, label, markupOptions_.brushColor == color,
                   [this, color] { markupOptions_.brushColor = color; });
        auto *widthMenu = menu->addMenu(tr("线宽"));
        for (const int width : {2, 4, 8})
            choice(widthMenu, QStringLiteral("%1 px").arg(width), width == markupOptions_.brushWidth,
                   [this, width] { markupOptions_.brushWidth = width; });
    } else {
        choice(menu, tr("像素化"), !markupOptions_.blur, [this] { markupOptions_.blur = false; });
        choice(menu, tr("模糊"), markupOptions_.blur, [this] { markupOptions_.blur = true; });
        auto *strengthMenu = menu->addMenu(tr("强度"));
        for (const int strength : {6, 12, 24})
            choice(strengthMenu, QStringLiteral("%1 px").arg(strength), strength == markupOptions_.mosaicStrength,
                   [this, strength] { markupOptions_.mosaicStrength = strength; });
    }
    menu->exec(markupSettings_->mapToGlobal(QPoint(0, markupSettings_->height())));
    if (menu)
        menu->deleteLater();
}

void CaptureToolbar::buildMenu(QMenu *menu) {
    menu->addAction(tr("批注"), this, &CaptureToolbar::annotate);
    menu->addSeparator();
    populateRatioMenu(menu->addMenu(tr("固定比例")));
    menu->addSeparator();
    // Earlier work, offered where the rest of the extras are; an empty list says so
    // rather than showing a submenu that does nothing.
    auto *history = menu->addMenu(tr("截图历史"));
    history->setEnabled(!historyLabels_.isEmpty());
    for (int index = 0; index < historyLabels_.size(); ++index) {
        auto *action = history->addAction(historyLabels_.at(index));
        action->setCheckable(true);
        action->setChecked(index == historyIndex_);
        connect(action, &QAction::triggered, this, [this, index] { emit historyRequested(index); });
    }
    if (!historyLabels_.isEmpty()) {
        history->addSeparator();
        auto *previous = history->addAction(tr("上一张  <"));
        previous->setEnabled(historyIndex_ < historyLabels_.size() - 1);
        connect(previous, &QAction::triggered, this, [this] { emit historyStepRequested(-1); });
        auto *next = history->addAction(tr("下一张  >"));
        next->setEnabled(historyIndex_ > 0);
        connect(next, &QAction::triggered, this, [this] { emit historyStepRequested(1); });
    }
    auto *recent = menu->addMenu(tr("最近的选区"));
    recent->setEnabled(!selections_.isEmpty());
    for (const auto &area : selections_) {
        auto *action = recent->addAction(tr("%1 × %2").arg(area.width()).arg(area.height()));
        connect(action, &QAction::triggered, this, [this, area] { emit selectionRequested(area); });
    }
    menu->addSeparator();
    auto *languages = menu->addMenu(tr("文字识别语言"));
    for (const auto mode : {OcrLanguageMode::System, OcrLanguageMode::SimplifiedChinese,
                            OcrLanguageMode::English}) {
        auto *action = languages->addAction(ocrLanguageLabel(mode));
        action->setCheckable(true);
        action->setChecked(ocrLanguage_ == mode);
        connect(action, &QAction::triggered, this, [this, mode] {
            ocrLanguage_ = mode;
            emit ocrRequested();
        });
    }
    menu->addSeparator();
    // The shortcuts are the only way to reach some of this at speed, so they are
    // written down where they can be found.
    auto *keys = menu->addMenu(tr("快捷键"));
    for (const auto &line : QVector<QPair<QString, QString>>{
             {QStringLiteral("Ctrl+C / Enter / 双击"), tr("复制图像")},
             {QStringLiteral("Ctrl+S"), tr("保存图片")},
             {QStringLiteral("Ctrl+T / Ctrl+2 / P"), tr("贴图")},
             {QStringLiteral("Shift+C / T"), tr("文字识别")},
             {QStringLiteral("C"), tr("取色")},
             {QStringLiteral("B"), tr("画笔")},
             {QStringLiteral("M"), tr("马赛克")},
             {QStringLiteral("Ctrl+Z"), tr("撤销绘制")},
             {QStringLiteral("R"), tr("恢复上次选区")},
             {QStringLiteral("H"), tr("更多选项")},
             {QStringLiteral("< / >"), tr("历史截图上一张 / 下一张")},
             {QStringLiteral("Shift + 方向键"), tr("从对角收缩 1 px")},
             {QStringLiteral("Ctrl + 方向键"), tr("向外扩展 1 px")},
             {QStringLiteral("方向键"), tr("移动选区 1 px")},
             {QStringLiteral("Esc"), tr("取消")}}) {
        auto *action = keys->addAction(QStringLiteral("%1  %2").arg(line.first, line.second));
        action->setEnabled(false);
    }
}

// ------------------------------------------------------------ CaptureSidebar

CaptureSidebar::CaptureSidebar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("captureSidebar"));
    setAttribute(Qt::WA_AlwaysShowToolTips);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    const auto add = [this, layout](const QString &glyphName) {
        auto *button = new QPushButton(this);
        styleCaptureToolButton(button, glyphName);
        layout->addWidget(button);
        return button;
    };
    corner_ = add(QStringLiteral("corner"));
    shadow_ = add(QStringLiteral("shadow"));
    reset_ = add(QStringLiteral("reset"));
    connect(corner_, &QPushButton::clicked, this,
            [this] { openPanel(StylePanel::Mode::Corners, corner_); });
    connect(shadow_, &QPushButton::clicked, this,
            [this] { openPanel(StylePanel::Mode::Shadow, shadow_); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        style_ = CaptureStyle{};
        refreshButtons();
        emit styleChanged(style_);
    });
    applyChromeTheme();
    retranslate();
    refreshButtons();
}

void CaptureSidebar::applyChromeTheme() {
    ChromeApplyGuard guard(this);
    if (!guard.entered)
        return;
    const auto t = captureChromeTokens();
    setStyleSheet(captureControlsStyle(t));
    for (auto *button : {corner_, shadow_, reset_}) {
        if (button && !button->property("glyphName").toString().isEmpty())
            styleCaptureToolButton(button, button->property("glyphName").toString());
    }
    if (panel_ != nullptr)
        panel_->refresh();
    adjustSize();
    update();
}

void CaptureSidebar::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        applyChromeTheme();
    QWidget::changeEvent(event);
}

void CaptureSidebar::retranslate() {
    corner_->setToolTip(tr("圆角"));
    shadow_->setToolTip(tr("阴影 / 边框"));
    reset_->setToolTip(tr("重置圆角、边框与阴影"));
    for (auto *button : {corner_, shadow_, reset_})
        button->setAccessibleName(button->toolTip());
    if (panel_ != nullptr) {
        panel_->retranslate();
        panel_->refresh();
    }
}

void CaptureSidebar::refreshButtons() {
    const auto mark = [](QPushButton *button, bool active) {
        button->setProperty("active", active);
        styleCaptureToolButton(button, button->property("glyphName").toString());
        button->style()->unpolish(button);
        button->style()->polish(button);
    };
    mark(corner_, style_.cornerRadius > 0);
    mark(shadow_, style_.shadow || style_.border);
    corner_->setToolTip(style_.cornerRadius > 0 ? tr("圆角 %1 px").arg(style_.cornerRadius) : tr("圆角"));
    QString edge;
    if (style_.shadow)
        edge = tr("阴影 %1").arg(style_.shadowStrength);
    if (style_.border)
        edge = edge.isEmpty() ? tr("边框 %1 px").arg(style_.borderWidth)
                              : tr("%1 · 边框 %2 px").arg(edge).arg(style_.borderWidth);
    shadow_->setToolTip(edge.isEmpty() ? tr("阴影 / 边框") : edge);
    shadow_->setAccessibleName(shadow_->toolTip());
    adjustSize();
}

void CaptureSidebar::setStyle(const CaptureStyle &style) {
    style_ = style;
    refreshButtons();
    if (panel_ != nullptr)
        panel_->setStyle(style_);
}

void CaptureSidebar::setBusy(bool busy) {
    busy_ = busy;
    for (auto *button : {corner_, shadow_, reset_})
        button->setEnabled(!busy);
}

void CaptureSidebar::openPanel(StylePanel::Mode mode, QPushButton *anchor) {
    if (busy_)
        return;
    // A panel built for the other tool cannot be reused: it has a different set of
    // controls, so it is dropped rather than reconfigured into something else.
    if (panel_ != nullptr && panel_->property("mode").toInt() != int(mode)) {
        panel_->deleteLater();
        panel_ = nullptr;
    }
    if (panel_ == nullptr) {
        panel_ = new StylePanel(mode, window());
        panel_->setProperty("mode", int(mode));
        connect(panel_, &StylePanel::styleChanged, this, [this](const CaptureStyle &style) {
            style_ = style;
            refreshButtons();
            emit styleChanged(style_);
        });
        connect(panel_, &StylePanel::rememberRequested, this,
                [this](const CaptureStyle &style) { emit rememberRequested(style); });
    }
    panel_->setStyle(style_);
    // The panel is a window of its own, so the place it opens at has to be worked out
    // in screen coordinates.
    const QRect bounds = window() != nullptr
                             ? QRect(window()->mapToGlobal(QPoint(0, 0)), window()->size())
                             : QRect(0, 0, 1000, 1000);
    panel_->popupBeside(anchor, bounds);
}

void CaptureSidebar::placeBeside(const QRect &selection, const QRect &bounds) {
    adjustSize();
    const int gap = 10;
    // To the right of the selection, where it does not cover what was just chosen;
    // to the left when the screen ends first.
    int x = selection.right() + gap;
    if (x + width() > bounds.right())
        x = selection.left() - gap - width();
    x = std::clamp(x, bounds.left() + gap, std::max(bounds.left() + gap, bounds.right() - width() - gap));
    int y = selection.top();
    y = std::clamp(y, bounds.top() + gap, std::max(bounds.top() + gap, bounds.bottom() - height() - gap));
    move(x, y);
    raise();
}

void CaptureSidebar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintFloatingPanel(painter, rect());
}
} // namespace h2d
