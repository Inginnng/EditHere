#include "guide.h"
#include "ui.h"
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <algorithm>

namespace h2d {
namespace {
class GuideCard final : public QWidget {
  public:
    explicit GuideCard(QWidget *parent) : QWidget(parent) { setObjectName("guideCard"); }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlpha(35);
        painter.setPen(QPen(border, 1));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 14, 14);
    }
};
}

GuideOverlay::GuideOverlay(QWidget *parent) : QWidget(parent) {
    setObjectName("guideOverlay");
    setAccessibleName("HelpDesign 使用引导");
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    card_ = new GuideCard(this);
    auto layout = new QVBoxLayout(card_);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto top = new QHBoxLayout;
    top->addWidget(mutedLabel("快速上手", card_));
    top->addStretch();
    progress_ = mutedLabel({}, card_);
    progress_->setObjectName("guideProgress");
    top->addWidget(progress_);
    layout->addLayout(top);
    title_ = new QLabel(card_);
    title_->setObjectName("guideTitle");
    title_->setWordWrap(true);
    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3);
    titleFont.setWeight(QFont::DemiBold);
    title_->setFont(titleFont);
    layout->addWidget(title_);
    body_ = new QLabel(card_);
    body_->setObjectName("guideBody");
    body_->setWordWrap(true);
    body_->setTextFormat(Qt::PlainText);
    body_->setMinimumHeight(62);
    body_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(body_);
    auto footer = new QHBoxLayout;
    footer->setSpacing(7);
    skip_ = textButton("跳过", false, card_);
    skip_->setObjectName("guideSkip");
    skip_->setAccessibleName("跳过使用引导");
    back_ = textButton("上一步", false, card_);
    back_->setObjectName("guideBack");
    next_ = textButton("下一步", true, card_);
    next_->setObjectName("guideNext");
    footer->addWidget(skip_);
    footer->addStretch();
    footer->addWidget(back_);
    footer->addWidget(next_);
    layout->addLayout(footer);
    connect(skip_, &QPushButton::clicked, this, &GuideOverlay::dismiss);
    connect(back_, &QPushButton::clicked, this, [this] { setStep(step_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this] {
        if (step_ + 1 == stepCount()) dismiss();
        else setStep(step_ + 1);
    });
    fade_ = new QGraphicsOpacityEffect(card_);
    card_->setGraphicsEffect(fade_);
    animation_ = new QVariantAnimation(this);
    animation_->setDuration(140);
    animation_->setStartValue(.65);
    animation_->setEndValue(1.0);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { fade_->setOpacity(value.toReal()); });
    hide();
}

GuideOverlay::~GuideOverlay() {
    if (qApp) qApp->removeEventFilter(this);
}

void GuideOverlay::start() {
    QToolTip::hideText();
    if (!isVisible()) {
        previousFocus_ = QApplication::focusWidget();
        if (previousFocus_ && !belongsToEditor(previousFocus_)) previousFocus_.clear();
        qApp->installEventFilter(this);
    }
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setStep(0);
    next_->setFocus(Qt::OtherFocusReason);
}

void GuideOverlay::dismiss() {
    if (isHidden()) return;
    animation_->stop();
    qApp->removeEventFilter(this);
    hide();
    if (previousFocus_ && previousFocus_->isVisible() && previousFocus_->isEnabled())
        previousFocus_->setFocus(Qt::OtherFocusReason);
    else if (auto surface = visibleControl("imageWell"))
        surface->setFocus(Qt::OtherFocusReason);
    previousFocus_.clear();
    emit dismissed();
}

QWidget *GuideOverlay::visibleControl(const QString &name) const {
    auto control = parentWidget()->findChild<QWidget *>(name);
    return control && control->isVisibleTo(parentWidget()) ? control : nullptr;
}

void GuideOverlay::setStep(int step) {
    step_ = std::clamp(step, 0, stepCount() - 1);
    const QStringList titles{
        "先放进一张界面图", "找到你想改的那一块", "写下风格与意图",
        "用大爆炸调整布局", "把反馈交给 AI", "随时回来看看"};
    title_->setText(titles[step_]);
    progress_->setText(QString("%1 / %2").arg(step_ + 1).arg(stepCount()));
    back_->setEnabled(step_ > 0);
    next_->setText(step_ + 1 == stepCount() ? "完成" : "下一步");
    next_->setAccessibleName(next_->text());
    placeCard();
    animation_->stop();
    animation_->start();
    next_->setFocus(Qt::OtherFocusReason);
}

void GuideOverlay::placeCard() {
    QStringList controls;
    QString body;
    switch (step_) {
    case 0:
        controls = {"importDocument", "captureImage"};
        body = "点导入打开图片或项目，也可以直接截图。图片还能拖进窗口，或从剪贴板粘贴。";
        break;
    case 1:
        controls = {"mode_smart"};
        body = "选中智能选块，把鼠标放在目标上。滚轮可切换所有包含鼠标的框：向上更大，向下更小；单击添加批注。";
        break;
    case 2:
        controls = {visibleControl("addGlobalNote") ? "addGlobalNote" : "collapseNotes"};
        body = "圈出位置后，写清想要的风格和改变，例如「更轻盈、安静，减少装饰」。整页方向可用全局批注表达。";
        break;
    case 3:
        controls = {"explodeButton"};
        body = "打开大爆炸后，选块并拖动来移动布局。拖边缘可自由拉伸，拖角点会等比缩放。调整结果会随反馈保存。";
        break;
    case 4: {
        const bool direct = visibleControl("exportJson");
        controls = {direct ? "exportJson" : "moreActions"};
        body = direct ? "点「查看 JSON」，检查批注与布局变化，再复制 JSON 给 AI。JSON 默认包含原图，让 AI 能结合画面理解你的意图。"
                      : "点更多操作，选择「查看 JSON」，检查批注与布局变化，再复制 JSON 给 AI。JSON 默认包含原图。";
        break;
    }
    default:
        controls = {"showGuide"};
        body = "右上角的问号随时能从头打开引导。现在开始试试；按 Esc 或点跳过，也能直接回到编辑。";
        break;
    }
    body_->setText(body);
    highlights_.clear();
    highlightedControls_.clear();
    QRect anchor;
    for (const auto &name : controls) {
        if (auto control = visibleControl(name)) {
            QRect spot(mapFromGlobal(control->mapToGlobal(QPoint())), control->size());
            spot = spot.adjusted(-5, -5, 5, 5).intersected(rect().adjusted(3, 3, -3, -3));
            if (!spot.isEmpty()) {
                highlights_.append(spot);
                highlightedControls_.append(name);
                anchor = anchor.isNull() ? spot : anchor.united(spot);
            }
        }
    }
    const QRect available = rect().adjusted(14, 14, -14, -14);
    const int cardWidth = std::min(364, available.width());
    card_->setFixedWidth(cardWidth);
    card_->layout()->invalidate();
    const int wantedHeight = card_->layout()->totalHeightForWidth(cardWidth);
    const int cardHeight = std::min(available.height(), std::max(card_->sizeHint().height(), wantedHeight));
    const int x = std::clamp(anchor.isNull() ? available.center().x() - cardWidth / 2
                                           : anchor.center().x() - cardWidth / 2,
                             available.left(), available.right() - cardWidth + 1);
    int y = anchor.isNull() ? available.center().y() - cardHeight / 2 : anchor.bottom() + 14;
    if (y + cardHeight > available.bottom() + 1 && !anchor.isNull()) y = anchor.top() - cardHeight - 14;
    y = std::clamp(y, available.top(), available.bottom() - cardHeight + 1);
    card_->setGeometry(x, y, cardWidth, cardHeight);
    update();
}

void GuideOverlay::queuePlacement() {
    if (placementQueued_) return;
    placementQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        placementQueued_ = false;
        if (!isVisible()) return;
        setGeometry(parentWidget()->rect());
        placeCard();
        raise();
    });
}

bool GuideOverlay::belongsToEditor(QObject *object) const {
    // Dialogs and tray menus may be owned by the editor while remaining separate windows.
    // For non-widget receivers such as QShortcut, use their nearest owning widget.
    for (auto ancestor = object; ancestor; ancestor = ancestor->parent())
        if (auto widget = qobject_cast<QWidget *>(ancestor))
            return widget->window() == parentWidget()->window();
    return false;
}

bool GuideOverlay::eventFilter(QObject *object, QEvent *event) {
    if (!isVisible() || !belongsToEditor(object)) return false;
    auto widget = qobject_cast<QWidget *>(object);
    const bool insideGuide = object == this || (widget && isAncestorOf(widget));
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::Shortcut) return true;
    if (event->type() == QEvent::KeyPress) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            QList<QPushButton *> order{skip_};
            if (back_->isEnabled()) order.append(back_);
            order.append(next_);
            int current = order.indexOf(qobject_cast<QPushButton *>(QApplication::focusWidget()));
            const int direction = key->key() == Qt::Key_Backtab || key->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
            order[(current + direction + order.size()) % order.size()]->setFocus(Qt::TabFocusReason);
            return true;
        }
        if (key->key() == Qt::Key_Left) { setStep(step_ - 1); return true; }
        if (key->key() == Qt::Key_Right) { setStep(step_ + 1); return true; }
        if (!insideGuide) return true;
    }
    if (insideGuide) return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::ContextMenu:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        return true;
    case QEvent::FocusIn:
        next_->setFocus(Qt::OtherFocusReason);
        return true;
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::LayoutRequest:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
        queuePlacement();
        break;
    default: break;
    }
    return false;
}

void GuideOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath dim;
    dim.setFillRule(Qt::WindingFill);
    dim.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 14, 14);
    QPainterPath holes;
    holes.setFillRule(Qt::WindingFill);
    for (const auto &spot : highlights_) holes.addRoundedRect(QRectF(spot), 10, 10);
    painter.fillPath(dim.subtracted(holes), QColor(8, 12, 22, isDarkTheme() ? 164 : 126));
    painter.setPen(QPen(accent(), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(holes);
}

void GuideOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    placeCard();
}
} // namespace h2d