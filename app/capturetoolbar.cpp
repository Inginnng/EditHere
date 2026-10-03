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
#include <QPalette>
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
#include <algorithm>
#include <cmath>

namespace h2d {
namespace {
// The two panels and the bar are all drawn by hand rather than by the style of the
// day, so that a light theme cannot turn the controls into dark shapes on a dark
// face. The colours are written down once here.
const QColor kFace(28, 30, 36, 240);
const QColor kEdge(255, 255, 255, 40);
const QColor kInk(0xe8, 0xe9, 0xee);
const QColor kGlyph(0xd4, 0xd5, 0xdd);

class RoundedCropPopup final : public QWidget {
  public:
    explicit RoundedCropPopup(QWidget *parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_AlwaysShowToolTips);
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(isDarkTheme() ? "#41434d" : "#dddde5"), 1));
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    }
};

void setScrollGlyph(QPushButton *button, const QString &name) {
    button->setProperty("glyphName", name);
    button->setProperty("tool", true);
    button->setIcon(glyph(name, button->objectName() == "scrollStop"
        ? QColor(isDarkTheme() ? "#ff8991" : "#bd252f") : QColor()));
    button->setIconSize(QSize(20, 20));
    button->setCursor(Qt::PointingHandCursor);
}

QString panelStyleSheet() {
    return QStringLiteral(R"(
QWidget { color: #e8e9ee; }
QPushButton { background: transparent; border: none; }
QPushButton[tab="true"] { padding: 5px 12px; border-radius: 7px; }
QPushButton[tab="true"]:hover { background: rgba(255, 255, 255, 0.12); }
QPushButton[tab="true"][chosen="true"] { background: rgba(255, 255, 255, 0.20); }
QSlider::groove:horizontal { height: 4px; background: rgba(255, 255, 255, 0.18); border-radius: 2px; }
QSlider::sub-page:horizontal { background: #d4d5dd; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; height: 12px; margin: -5px 0; border-radius: 6px;
    background: #f2f2f5; }
QCheckBox { color: #c9cbd4; }
)");
}
} // namespace

// ---------------------------------------------------------------- StylePanel

StylePanel::StylePanel(Mode mode, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint), mode_(mode) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(panelStyleSheet());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    caption_ = new QLabel(this);
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
    retranslate();
    syncFromStyle();
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
    setStyleSheet(panelStyleSheet());
    update();
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
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kEdge, 1));
    painter.setBrush(kFace);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}

// ------------------------------------------------------------- CaptureToolbar

CaptureToolbar::CaptureToolbar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("captureToolbar"));
    // The face of the bar is drawn by hand, so the controls on it are told to stay out
    // of the way and the light glyphs stay readable whatever the application theme is.
    // 批注 is why the program exists, so it gets the one coloured shape on an otherwise
    // monochrome bar: a blue rounded rectangle around it, which is how a capture tool
    // marks the thing it is for.
    const QColor mark = accent();
    setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"
                                 "QPushButton[tool=\"true\"]:hover { background: rgba(255, 255, 255, 0.14);"
                                 " border-radius: 8px; }"
                                 "QPushButton[primary=\"true\"] { background: rgba(%1, %2, %3, 0.30);"
                                 " border: 1px solid rgba(%1, %2, %3, 0.95); border-radius: 8px; }"
                                 "QPushButton[primary=\"true\"]:hover { background: rgba(%1, %2, %3, 0.46); }"
                                 "QPushButton:disabled { color: rgba(232, 233, 238, 0.4); }"
                                 "QLabel { color: #e8e9ee; }")
                      .arg(mark.red())
                      .arg(mark.green())
                      .arg(mark.blue()));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    size_ = new QPushButton(this);
    size_->setProperty("flat", true);
    size_->setCursor(Qt::PointingHandCursor);
    size_->setStyleSheet(QStringLiteral("QPushButton { color: #c9cbd4; padding: 0 6px; }"));
    layout->addWidget(size_);
    ratio_ = new QPushButton(this);
    ratio_->setProperty("flat", true);
    ratio_->setCursor(Qt::PointingHandCursor);
    ratio_->setStyleSheet(QStringLiteral("QPushButton { color: #c9cbd4; padding: 0 6px; }"));
    layout->addWidget(ratio_);
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 0.16);"));
    divider->setFixedHeight(20);
    layout->addWidget(divider);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("captureBusyStatus"));
    layout->addWidget(status_);
    status_->hide();
    // The order is the order they are reached in: what the capture is for, then what
    // can be read out of it, then the two that end the session.
    (void)addButton(QStringLiteral("edit"), tr("批注"));
    (void)addButton(QStringLiteral("ocr"), tr("文字识别"));
    (void)addButton(QStringLiteral("scroll"), tr("长截图"));
    (void)addButton(QStringLiteral("pin"), tr("贴图"));
    (void)addButton(QStringLiteral("image-save"), tr("保存图片"));
    (void)addButton(QStringLiteral("copy"), tr("复制图像"));
    more_ = new QPushButton(this);
    more_->setIcon(glyph(QStringLiteral("more"), kGlyph));
    more_->setIconSize({20, 20});
    more_->setProperty("tool", true);
    more_->setFixedSize(34, 34);
    more_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(more_);
    auto *close = new QPushButton(this);
    close->setIcon(glyph(QStringLiteral("close"), kGlyph));
    close->setIconSize({20, 20});
    close->setProperty("tool", true);
    close->setFixedSize(34, 34);
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, &CaptureToolbar::dismiss);
    layout->addWidget(close);
    connect(size_, &QPushButton::clicked, this, &CaptureToolbar::chooseSize);
    connect(ratio_, &QPushButton::clicked, this, &CaptureToolbar::showMenu);
    connect(more_, &QPushButton::clicked, this, &CaptureToolbar::showMenu);
    retranslate();
}

QPushButton *CaptureToolbar::addButton(const QString &glyphName, const QString &label) {
    // The bar keeps its own dark face whatever the application theme is, so the glyphs
    // are built in a light colour instead of following the theme.
    auto *button = new QPushButton(this);
    button->setProperty("glyphName", glyphName);
    button->setIcon(glyph(glyphName, kGlyph));
    button->setIconSize({20, 20});
    button->setToolTip(label);
    button->setAccessibleName(label);
    button->setProperty("tool", true);
    // The one action that is the point of the whole program is wrapped in a coloured
    // rectangle rather than left as one glyph among seven.
    if (glyphName == QLatin1String("edit"))
        button->setProperty("primary", true);
    button->setFixedSize(34, 34);
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, [this, glyphName] {
        if (glyphName == QLatin1String("copy"))
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
    });
    layout()->addWidget(button);
    buttons_.append(button);
    return button;
}

void CaptureToolbar::retranslate() {
    // The buttons are found by their glyph so a language change does not have to
    // remember the order they were created in.
    for (auto *button : buttons_) {
        const auto name = button->property("glyphName").toString();
        QString label;
        if (name == QLatin1String("copy"))
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
        if (!label.isEmpty()) {
            button->setToolTip(label);
            button->setAccessibleName(label);
        }
    }
    more_->setToolTip(tr("更多选项"));
    more_->setAccessibleName(tr("更多选项"));
    size_->setToolTip(tr("点击输入精确尺寸"));
    ratio_->setToolTip(tr("点击选择固定比例"));
    ratio_->setText(captureRatioLabel(ratioChoice_, customWidth_, customHeight_));
    if (!message_.isEmpty())
        status_->setText(message_);
    adjustSize();
}

void CaptureToolbar::setSelectionSize(QSize size) {
    // The size is kept as a property so the dialog can open on it, and remembered so
    // that adding the corner afterwards does not have to be told twice.
    size_->setProperty("pixels", size);
    size_->setText(tr("%1 × %2 px").arg(size.width()).arg(size.height()));
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
    buildMenu();
    more_->menu()->exec(more_->mapToGlobal(QPoint(0, more_->height())));
}

void CaptureToolbar::setBusy(bool busy, const QString &message) {
    busy_ = busy;
    message_ = message;
    status_->setText(message);
    status_->setVisible(!message.isEmpty());
    for (auto *button : buttons_)
        button->setEnabled(!busy && (button->property("glyphName").toString() != QLatin1String("scroll") ||
                                    scrollAvailable_));
    more_->setEnabled(!busy);
    size_->setEnabled(!busy);
    ratio_->setEnabled(!busy);
    adjustSize();
    raise();
}

void CaptureToolbar::setScrollAvailable(bool available) {
    scrollAvailable_ = available;
    for (auto *button : buttons_)
        if (button->property("glyphName").toString() == QLatin1String("scroll"))
            button->setEnabled(available && !busy_);
}

void CaptureToolbar::placeNear(const QRect &selection, const QRect &bounds) {
    adjustSize();
    const int gap = 10;
    // Above the selection, because what is under it is what the user just decided to
    // keep; below only when the screen has no room left up there.
    int y = selection.top() - gap - height();
    if (y < bounds.top())
        y = selection.bottom() + gap;
    int x = selection.left();
    // Near either edge of the screen the bar is pulled back inside rather than left
    // half off it.
    x = std::clamp(x, bounds.left() + gap, std::max(bounds.left() + gap, bounds.right() - width() - gap));
    y = std::clamp(y, bounds.top() + gap, std::max(bounds.top() + gap, bounds.bottom() - height() - gap));
    move(x, y);
    raise();
}

void CaptureToolbar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kEdge, 1));
    painter.setBrush(kFace);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
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
    if (!busy_ && scrollAvailable_)
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
    handle->setFocusPolicy(Qt::NoFocus);
    handle->setFixedSize(25, 30);
    setScrollGlyph(handle, QStringLiteral("scroll-move-vertical"));
    handle->setCursor(Qt::SizeVerCursor);
    handle->installEventFilter(this);
    handle_ = handle;
}

ScrollCaptureRegion::~ScrollCaptureRegion() = default;

void ScrollCaptureRegion::setSelection(const QRect &selection, const QRect &bounds) {
    bounds_ = bounds;
    selection_ = selection.intersected(bounds);
    setGeometry(selection_.adjusted(-3, -3, 3, 3));
    // A native hole, not merely transparent paint: WindowFromPoint and browser
    // wheel routing must find the page below the center of the capture region.
    setMask(QRegion(rect()).subtracted(QRegion(rect().adjusted(8, 8, -8, -8))));
    placeHandle();
    update();
}

void ScrollCaptureRegion::setState(bool running, Qt::Orientation axis) {
    running_ = running;
    axis_ = axis;
    setScrollGlyph(static_cast<QPushButton *>(handle_),
        axis == Qt::Vertical ? QStringLiteral("scroll-move-vertical") : QStringLiteral("scroll-move-horizontal"));
    if (windowFlags().testFlag(Qt::WindowTransparentForInput) != running) {
        const bool visible = isVisible();
        setWindowFlag(Qt::WindowTransparentForInput, running);
        if (visible) {
            show();
            configureNativeWindow(this, true);
        }
    }
    handle_->setCursor(running ? (axis == Qt::Vertical ? Qt::SizeVerCursor : Qt::SizeHorCursor)
                               : Qt::SizeAllCursor);
    handle_->setToolTip(running ? tr("拖动以沿截图方向移动选区") : tr("拖动移动选区；拖动蓝框边缘调整大小"));
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
    handleAnchor_ = global;
    anchored_ = true;
    handle_->move(global);
    if (handle_->isVisible()) configureNativeWindow(handle_, true);
}

void ScrollCaptureRegion::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setPen(QPen(QColor(running_ ? "#ffffff" : "#3489ff"), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    if (!running_) {
        painter.setBrush(Qt::white);
        for (const QPointF point : {QPointF(3, 3), QPointF(width() / 2.0, 3), QPointF(width() - 3, 3),
                QPointF(3, height() / 2.0), QPointF(width() - 3, height() / 2.0),
                QPointF(3, height() - 3), QPointF(width() / 2.0, height() - 3), QPointF(width() - 3, height() - 3)})
            painter.drawRect(QRectF(point - QPointF(2, 2), QSizeF(4, 4)));
    }
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
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && dragging_) {
            dragTo(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && dragging_) {
            dragging_ = false;
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
    event->accept();
}

void ScrollCaptureRegion::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_)
        dragTo(event->globalPosition().toPoint());
}

void ScrollCaptureRegion::mouseReleaseEvent(QMouseEvent *) { dragging_ = false; }

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
    update();
}

void ScrollCaptureShade::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 105));
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
    painter.fillRect(rect(), palette().color(QPalette::Base));
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
    painter.setBrush(QColor(matched_ ? 131 : 245, matched_ ? 229 : 155, matched_ ? 79 : 35, 35));
    painter.setPen(QPen(QColor(matched_ ? "#83e54f" : "#f59b23"), 3));
    painter.drawRect(viewportRect().adjusted(1.5, 1.5, -1.5, -1.5));
    if (cropBegin_ || cropEnd_ || cropEdge_ >= 0) {
        painter.setPen(QPen(QColor("#47abff"), 2, Qt::DashLine));
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
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(3);
    auto *dragBar = new DragBar(this);
    dragBar->setObjectName(QStringLiteral("scrollDragBar"));
    dragBar->setToolTip(tr("拖动工具栏"));
    auto *dots = new QLabel(dragBar);
    dots->setProperty("glyphName", QStringLiteral("scroll-grip"));
    dots->setPixmap(glyph("scroll-grip", palette().color(QPalette::PlaceholderText)).pixmap(24, 24));
    dots->setFixedSize(18, 24);
    dots->setAlignment(Qt::AlignCenter);
    dots->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *dragLayout = new QHBoxLayout(dragBar);
    dragLayout->setContentsMargins(0, 0, 0, 0);
    dragLayout->addWidget(dots);
    dragBar->setFixedSize(18, 30);
    layout->addWidget(dragBar);
    size_ = new QLabel(QStringLiteral("0 × 0"), this);
    size_->setObjectName(QStringLiteral("scrollSize"));
    size_->setAlignment(Qt::AlignCenter);
    size_->setMinimumWidth(76);
    size_->setProperty("muted", true);
    QFont sizeFont = size_->font();
    sizeFont.setPixelSize(14);
    size_->setFont(sizeFont);
    size_->setToolTip(tr("已拼接长图的宽度和高度，单位为像素"));
    layout->addWidget(size_);
    // The move handle is a separate native window owned by the region; this gap
    // receives it so dragging the live selection never steals page input.
    moveSlot_ = new QWidget(this);
    moveSlot_->setFixedSize(25, 30);
    layout->addWidget(moveSlot_);
    auto *separator = new QFrame(this);
    separator->setFixedSize(1, 18);
    separator->setObjectName(QStringLiteral("toolbarSeparator"));
    layout->addWidget(separator, 0, Qt::AlignVCenter);
    direction_ = new QPushButton(this);
    direction_->setObjectName(QStringLiteral("scrollDirection"));
    direction_->setToolTip(tr("切换垂直或水平截图；切换方向会重新开始拼接"));
    direction_->setAccessibleName(tr("切换截图方向"));
    direction_->setCursor(Qt::PointingHandCursor);
    direction_->setFixedSize(96, 26);
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
    automatic_ = new QCheckBox(this);
    automatic_->setObjectName(QStringLiteral("scrollAutomatic"));
    automatic_->setToolTip(tr("自动向下滚动并拼接；再次点击切换为手动滚动"));
    automatic_->hide();
    automaticButton_ = new QPushButton(this);
    automaticButton_->setObjectName(QStringLiteral("scrollAutomaticButton"));
    setScrollGlyph(automaticButton_, QStringLiteral("scroll-mouse"));
    automaticButton_->setCheckable(true);
    automaticButton_->setToolTip(automatic_->toolTip());
    automaticButton_->setAccessibleName(tr("自动滚动（默认手动滚动）"));
    automaticButton_->setFixedSize(28, 30);
    layout->addWidget(automaticButton_);
    connect(automaticButton_, &QPushButton::clicked, automatic_, &QCheckBox::setChecked);
    connect(automatic_, &QCheckBox::toggled, automaticButton_, &QPushButton::setChecked);
    cropButton_ = new QPushButton(this);
    cropButton_->setObjectName(QStringLiteral("scrollCrop"));
    setScrollGlyph(cropButton_, QStringLiteral("crop"));
    cropButton_->setToolTip(tr("裁剪当前可见区域之前或之后的内容，裁剪后可继续截图"));
    cropButton_->setAccessibleName(tr("裁剪长截图"));
    cropButton_->setFixedSize(30, 30);
    layout->addWidget(cropButton_);
    stop_ = new QPushButton(this);
    stop_->setObjectName(QStringLiteral("scrollStop"));
    setScrollGlyph(stop_, QStringLiteral("scroll-stop"));
    stop_->setToolTip(tr("停止截图并清空本次结果；可调整选区后重新开始"));
    stop_->setAccessibleName(tr("停止截图"));
    stop_->setFixedSize(30, 30);
    layout->addWidget(stop_);
    finish_ = new QPushButton(this);
    finish_->setObjectName(QStringLiteral("scrollFinish"));
    setScrollGlyph(finish_, QStringLiteral("edit"));
    finish_->setToolTip(tr("完成长截图并进入编辑器（Enter）"));
    finish_->setAccessibleName(tr("完成并进入编辑器"));
    finish_->setFixedSize(30, 30);
    finish_->setEnabled(false);
    layout->addWidget(finish_);
    const QStringList labels = {QStringLiteral("pin"), QStringLiteral("image-save"),
                                QStringLiteral("scroll-quick-save"), QStringLiteral("copy")};
    const QStringList tips = {tr("将完整长图贴在桌面上"), tr("选择位置和格式，保存完整长图"),
                             tr("将完整长图快速保存到设置的目录"), tr("复制完整长图到剪贴板并关闭长截图")};
    const QStringList accessibleNames = {tr("贴图"), tr("保存"), tr("快速保存"), tr("复制并关闭")};
    const QStringList names = {QStringLiteral("scrollPin"), QStringLiteral("scrollSave"),
                               QStringLiteral("scrollQuickSave"), QStringLiteral("scrollCopy")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(this);
        button->setObjectName(names[i]);
        setScrollGlyph(button, labels[i]);
        button->setToolTip(tips[i]);
        button->setAccessibleName(accessibleNames[i]);
        button->setFixedSize(29, 30);
        button->setEnabled(false);
        layout->addWidget(button);
        outputButtons_.append(button);
    }
    auto *cancel = new QPushButton(this);
    cancel->setObjectName(QStringLiteral("scrollCancel"));
    setScrollGlyph(cancel, QStringLiteral("close"));
    cancel->setToolTip(tr("关闭长截图并放弃本次结果（Esc）"));
    cancel->setAccessibleName(tr("关闭长截图"));
    cancel->setFixedSize(29, 30);
    // The close action comes before the last copy action in the reference bar.
    layout->removeWidget(outputButtons_.last());
    layout->addWidget(cancel);
    layout->addWidget(outputButtons_.last());
    setFixedHeight(42);
    adjustSize();
    setFixedWidth(std::max(536, sizeHint().width()));

    // The thumbnail is its own top-level surface. It stays to the right of the
    // capture frame while running and disappears when capture is stopped.
    previewArea_ = new QScrollArea(this);
    previewArea_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                 Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    previewArea_->setObjectName(QStringLiteral("scrollPreviewArea"));
    previewArea_->setAttribute(Qt::WA_ShowWithoutActivating);
    previewArea_->setFixedSize(150, 260);
    previewArea_->setStyleSheet(QStringLiteral(
        "QScrollArea#scrollPreviewArea { border: 2px solid palette(highlight); background: palette(base); }"));
    previewArea_->setFrameShape(QFrame::NoFrame);
    previewArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    preview_ = new ScrollCapturePreview;
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    previewArea_->setWidget(preview_);
    previewArea_->setWidgetResizable(true);

    cropMenu_ = new RoundedCropPopup(this);
    cropMenu_->setObjectName(QStringLiteral("scrollCropMenu"));
    cropMenu_->setMinimumSize(194, 52);
    auto *cropLayout = new QHBoxLayout(cropMenu_);
    cropLayout->setContentsMargins(7, 4, 7, 4);
    cropLayout->setSpacing(4);
    cropBegin_ = new QPushButton(cropMenu_);
    cropBegin_->setObjectName(QStringLiteral("scrollCropBegin"));
    cropEnd_ = new QPushButton(cropMenu_);
    cropEnd_->setObjectName(QStringLiteral("scrollCropEnd"));
    for (auto *button : {cropBegin_, cropEnd_}) button->setFixedSize(28, 28);
    cropLayout->addWidget(cropBegin_);
    cropLayout->addWidget(cropEnd_);
    auto *cropDivider = new QFrame(cropMenu_);
    cropDivider->setFixedSize(1, 18);
    cropDivider->setObjectName(QStringLiteral("toolbarSeparator"));
    cropLayout->addWidget(cropDivider, 0, Qt::AlignVCenter);
    autoCrop_ = new QCheckBox(tr("自动裁剪"), cropMenu_);
    autoCrop_->setObjectName(QStringLiteral("scrollAutoCrop"));
    autoCrop_->setToolTip(tr("反向滚动时，自动裁去当前可见区域之外的长图内容"));
    autoCrop_->setAccessibleName(tr("自动裁剪"));
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
    });
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("scrollStatus"));
    status_->hide();
    setAxis(Qt::Vertical);
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (stopped_) emit resumeRequested();
        else emit stopRequested();
    });
    connect(finish_, &QPushButton::clicked, this, &ScrollCaptureProgress::finishRequested);
    connect(cancel, &QPushButton::clicked, this, &ScrollCaptureProgress::cancelRequested);
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
    connect(outputButtons_[2], &QPushButton::clicked, this, &ScrollCaptureProgress::quickSaveRequested);
    connect(outputButtons_[3], &QPushButton::clicked, this, &ScrollCaptureProgress::copyRequested);
}

void ScrollCaptureProgress::placeBeside(const QRect &selection, const QRect &screen) {
    if (!captureSelection_.isEmpty() && !previewLastSize_.isEmpty())
        previewAnchor_ += selection.topLeft() - captureSelection_.topLeft();
    captureSelection_ = selection;
    captureScreen_ = screen;
    const int gap = 6;
    const int x = std::clamp(selection.right() + 1 - width(), screen.left(),
                             std::max(screen.left(), screen.right() + 1 - width()));
    const int below = selection.bottom() + gap;
    const int y = below + height() <= screen.bottom() + 1
        ? below : std::max(screen.top(), selection.top() - height() - gap);
    move(x, y);
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
    outputButtons_[3]->setEnabled(hasProgress_ && canEdit_);
    const QString largeNotice = tr("超大长图请使用 PNG 保存；当前尺寸超过复制、贴图和编辑的图像上限。");
    finish_->setToolTip(canEdit_ ? tr("完成长截图并进入编辑器（Enter）") : largeNotice);
    outputButtons_[0]->setToolTip(canEdit_ ? tr("将完整长图贴在桌面上") : largeNotice);
    outputButtons_[3]->setToolTip(canEdit_ ? tr("复制完整长图到剪贴板并关闭长截图") : largeNotice);
    if (size.isEmpty())
        size = image.size();
    if (!image.isNull()) {
        previewSourceRect(size, viewport, axis);
        preview_->resize(previewArea_->viewport()->size());
        preview_->setCapture(image, size, viewport, matched, axis, imageSource);
        if (!stopped_ && isVisible()) {
            previewArea_->show();
            configureNativeWindow(previewArea_, true);
        }
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

void ScrollCaptureProgress::setStopped(const QString &message) {
    status_->setText(message);
    setAutomatic(false);
    stopped_ = true;
    automatic_->setEnabled(false);
    automaticButton_->setEnabled(false);
    automaticButton_->setToolTip(tr("重新开始截图后可启用自动滚动"));
    stop_->setText({});
    setScrollGlyph(stop_, QStringLiteral("scroll-play"));
    stop_->setToolTip(tr("按当前选区开始新的长截图"));
    stop_->setAccessibleName(tr("开始截图"));
    setToolTip(message);
    previewArea_->hide();
    finish_->show();
    finish_->setEnabled(hasProgress_ && canEdit_);
}

void ScrollCaptureProgress::setRunning(Qt::Orientation axis) {
    stopped_ = false;
    stop_->setText({});
    setScrollGlyph(stop_, QStringLiteral("scroll-stop"));
    stop_->setToolTip(tr("停止截图并清空本次结果；可调整选区后重新开始"));
    stop_->setAccessibleName(tr("停止截图"));
    automatic_->setEnabled(axis == Qt::Vertical);
    setAxis(axis);
    if (isVisible() && !preview_->pixmap().isNull()) {
        previewArea_->show();
        configureNativeWindow(previewArea_, true);
    }
}

void ScrollCaptureProgress::setAxis(Qt::Orientation axis) {
    direction_->setProperty("captureAxis", static_cast<int>(axis));
    direction_->setText(axis == Qt::Vertical ? tr("垂直") : tr("水平"));
    for (auto *action : direction_->menu()->actions())
        action->setChecked(action->data().toInt() == static_cast<int>(axis));
    cropBegin_->setText({});
    cropEnd_->setText({});
    setScrollGlyph(cropBegin_, axis == Qt::Vertical ? QStringLiteral("scroll-trim-top") : QStringLiteral("scroll-trim-left"));
    setScrollGlyph(cropEnd_, axis == Qt::Vertical ? QStringLiteral("scroll-trim-bottom") : QStringLiteral("scroll-trim-right"));
    cropBegin_->setToolTip(axis == Qt::Vertical ? tr("裁去当前可见区域上方的内容，保留当前区域及其下方")
                                              : tr("裁去当前可见区域左侧的内容，保留当前区域及其右侧"));
    cropEnd_->setToolTip(axis == Qt::Vertical ? tr("裁去当前可见区域下方的内容，保留当前区域及其上方")
                                            : tr("裁去当前可见区域右侧的内容，保留当前区域及其左侧"));
    cropBegin_->setAccessibleName(axis == Qt::Vertical ? tr("上裁剪") : tr("左裁剪"));
    cropEnd_->setAccessibleName(axis == Qt::Vertical ? tr("下裁剪") : tr("右裁剪"));
    automatic_->setEnabled(!stopped_ && axis == Qt::Vertical);
    automaticButton_->setEnabled(!stopped_ && axis == Qt::Vertical);
    automaticButton_->setToolTip(stopped_ ? tr("重新开始截图后可启用自动滚动") :
        axis == Qt::Vertical ? tr("自动向下滚动并拼接；再次点击切换为手动滚动") :
                              tr("水平截图请手动滚动或拖动水平滚动条"));
    if (axis == Qt::Horizontal) setAutomatic(false);
}

void ScrollCaptureProgress::setSelectionSize(QSize size) {
    size_->setText(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
    size_->setFixedWidth(std::max(76, size_->fontMetrics().horizontalAdvance(size_->text()) + 10));
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
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(isDarkTheme() ? "#41434d" : "#dddde5"), 1));
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
}

void ScrollCaptureProgress::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (moveSlot_) emit positionChanged(moveHandlePosition());
}

void ScrollCaptureProgress::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!stopped_ && !preview_->pixmap().isNull()) {
        previewArea_->show();
        configureNativeWindow(previewArea_, true);
    }
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
    adjustSize();
    emit ratioChanged();
}

void CaptureToolbar::buildMenu() {
    // The menu is rebuilt on every use so the ticks match the state the selection is
    // actually in; the previous one is dropped at that point rather than collected.
    if (auto *previous = more_->menu()) {
        more_->setMenu(nullptr);
        delete previous;
    }
    auto *menu = new QMenu(this);
    more_->setMenu(menu);
    // 批注 leads the menu as well: the bar can be hidden behind the pointer, and the
    // menu is how the keyboard reaches it.
    auto *annotate = menu->addAction(tr("批注"), this, &CaptureToolbar::annotate);
    annotate->setIcon(glyph(QStringLiteral("edit"), accent()));
    menu->addSeparator();
    auto *ratios = menu->addMenu(tr("固定比例"));
    const QVector<CaptureRatio> choices{CaptureRatio::Free,    CaptureRatio::Square,
                                        CaptureRatio::FourThree, CaptureRatio::ThreeTwo,
                                        CaptureRatio::SixteenNine, CaptureRatio::NineSixteen};
    for (const auto choice : choices) {
        auto *action = ratios->addAction(captureRatioLabel(choice, customWidth_, customHeight_));
        action->setCheckable(true);
        action->setChecked(ratioChoice_ == choice);
        connect(action, &QAction::triggered, this, [this, choice] { setRatio(choice); });
    }
    auto *custom = ratios->addAction(tr("自定义…"));
    custom->setCheckable(true);
    custom->setChecked(ratioChoice_ == CaptureRatio::Custom);
    connect(custom, &QAction::triggered, this, [this] {
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
    setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"
                                 "QPushButton[tool=\"true\"]:hover { background: rgba(255, 255, 255, 0.14);"
                                 " border-radius: 8px; }"
                                 "QPushButton[active=\"true\"] { background: rgba(255, 255, 255, 0.22);"
                                 " border-radius: 8px; }"
                                 "QPushButton:disabled { color: rgba(232, 233, 238, 0.4); }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);
    const auto add = [this, layout](const QString &glyphName) {
        auto *button = new QPushButton(this);
        button->setProperty("glyphName", glyphName);
        button->setIcon(glyph(glyphName, kGlyph));
        button->setIconSize({20, 20});
        button->setProperty("tool", true);
        button->setFixedSize(32, 32);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
        return button;
    };
    // Only what the bar above the region does not already offer lives here. Pinning is
    // one of the verbs up there, and offering it twice made the same action look like
    // two different things.
    corner_ = add(QStringLiteral("corner"));
    shadow_ = add(QStringLiteral("shadow"));
    reset_ = add(QStringLiteral("undo"));
    connect(corner_, &QPushButton::clicked, this,
            [this] { openPanel(StylePanel::Mode::Corners, corner_); });
    connect(shadow_, &QPushButton::clicked, this,
            [this] { openPanel(StylePanel::Mode::Shadow, shadow_); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        style_ = CaptureStyle{};
        refreshButtons();
        emit styleChanged(style_);
    });
    retranslate();
    refreshButtons();
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
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kEdge, 1));
    painter.setBrush(kFace);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
}
} // namespace h2d
