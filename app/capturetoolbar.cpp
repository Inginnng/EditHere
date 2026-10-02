#include "capturetoolbar.h"
#include "ocr.h"
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
#include <QPainter>
#include <QPushButton>
#include <QScreen>
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

ScrollCaptureProgress::ScrollCaptureProgress(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setObjectName(QStringLiteral("scrollCaptureProgress"));
    setWindowTitle(tr("EditHere · 长截图"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *dragBar = new DragBar(this);
    dragBar->setObjectName(QStringLiteral("scrollDragBar"));
    dragBar->setToolTip(tr("拖动窗口，避开滚动区域。"));
    auto *heading = new QHBoxLayout(dragBar);
    heading->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("长截图"), dragBar);
    auto *dragHint = mutedLabel(tr("拖动窗口"), dragBar);
    for (auto *label : {title, dragHint})
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    heading->addWidget(title);
    heading->addStretch();
    heading->addWidget(dragHint);
    layout->addWidget(dragBar);
    preview_ = new QLabel(this);
    preview_->setObjectName(QStringLiteral("scrollPreview"));
    preview_->setFixedSize(232, 190);
    preview_->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview_);
    status_ = new QLabel(tr("正在准备长截图…"), this);
    status_->setObjectName(QStringLiteral("scrollStatus"));
    status_->setWordWrap(true);
    status_->setMinimumHeight(44);
    layout->addWidget(status_);
    automatic_ = new QCheckBox(tr("自动滚动"), this);
    automatic_->setObjectName(QStringLiteral("scrollAutomatic"));
    automatic_->setToolTip(tr("勾选后由程序向下滚动；默认由你手动滚动页面。"));
    layout->addWidget(automatic_);
    auto *buttons = new QHBoxLayout;
    stop_ = new QPushButton(tr("停止"), this);
    stop_->setObjectName(QStringLiteral("scrollStop"));
    finish_ = textButton(tr("完成长截图"), true, this);
    finish_->setObjectName(QStringLiteral("scrollFinish"));
    finish_->setEnabled(false);
    buttons->addWidget(stop_);
    buttons->addWidget(finish_);
    layout->addLayout(buttons);
    auto *cancel = new QPushButton(tr("返回选区 (Esc)"), this);
    cancel->setObjectName(QStringLiteral("scrollCancel"));
    layout->addWidget(cancel);
    connect(stop_, &QPushButton::clicked, this, &ScrollCaptureProgress::stopRequested);
    connect(finish_, &QPushButton::clicked, this, &ScrollCaptureProgress::finishRequested);
    connect(cancel, &QPushButton::clicked, this, &ScrollCaptureProgress::cancelRequested);
    connect(automatic_, &QCheckBox::toggled, this, &ScrollCaptureProgress::automaticChanged);
    setFixedWidth(256);
    adjustSize();
}

void ScrollCaptureProgress::placeBeside(const QRect &selection, const QRect &screen) {
    adjustSize();
    const int gap = 12;
    const QVector<QPoint> positions = {
        {selection.right() + gap, selection.top()},
        {selection.left() - width() - gap, selection.top()},
        {selection.left(), selection.top() - height() - gap},
        {selection.left(), selection.bottom() + gap}};
    for (const auto &position : positions)
        if (screen.contains(QRect(position, size()))) {
            move(position);
            return;
        }
    move(std::clamp(screen.right() - width() - gap, screen.left(),
                    std::max(screen.left(), screen.right() - width())),
         std::clamp(screen.top() + gap, screen.top(),
                    std::max(screen.top(), screen.bottom() - height())));
}

void ScrollCaptureProgress::setProgress(const QImage &image, int addedFrames) {
    hasProgress_ = addedFrames > 0;
    finish_->setEnabled(hasProgress_);
    if (!image.isNull())
        preview_->setPixmap(QPixmap::fromImage(image.scaled(preview_->size(), Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation)));
    status_->setText((automatic_->isChecked()
                         ? tr("正在自动滚动 · %1 × %2 px\n已追加 %3 帧，随时可停止")
                         : tr("请在选区内向下滚动页面\n%1 × %2 px · 已追加 %3 帧"))
                         .arg(image.width()).arg(image.height()).arg(addedFrames));
}

void ScrollCaptureProgress::setAutomatic(bool automatic) {
    const QSignalBlocker blocked(automatic_);
    automatic_->setChecked(automatic);
}

void ScrollCaptureProgress::setStopped(const QString &message) {
    status_->setText(message);
    setAutomatic(false);
    automatic_->setEnabled(false);
    stop_->hide();
    finish_->show();
    finish_->setEnabled(hasProgress_);
}

void ScrollCaptureProgress::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit cancelRequested();
        event->accept();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
               finish_->isVisible() && hasProgress_) {
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
