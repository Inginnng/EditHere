#include "inlinenoteedit.h"

#include <QFocusEvent>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextLayout>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace h2d {

// Fade the viewport's pixels, rather than painting an assumed card background.
// This also works for selected cards, dark mode and fractional display scaling.
class NoteFadeEffect final : public QGraphicsEffect {
  public:
    explicit NoteFadeEffect(QObject *parent) : QGraphicsEffect(parent) {}
    void setFade(qreal start, qreal end) {
        if (qFuzzyCompare(start_, start) && qFuzzyCompare(end_, end))
            return;
        start_ = start;
        end_ = std::max(start + 1, end);
        update();
    }

  protected:
    void draw(QPainter *painter) override {
        QPoint offset;
        QPixmap source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
        if (source.isNull())
            return;
        QPainter mask(&source);
        mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QLinearGradient gradient(0, start_ - offset.y(), 0, end_ - offset.y());
        gradient.setColorAt(0, QColor(0, 0, 0, 255));
        gradient.setColorAt(1, QColor(0, 0, 0, 0));
        mask.fillRect(QRectF(QPointF(), QSizeF(source.size()) / source.devicePixelRatio()), gradient);
        mask.end();
        painter->drawPixmap(offset, source);
    }

  private:
    qreal start_ = 0;
    qreal end_ = 1;
};

InlineNoteEdit::InlineNoteEdit(QWidget *parent) : QPlainTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setCenterOnScroll(false);
    fade_ = new NoteFadeEffect(viewport());
    fade_->setEnabled(false);
    viewport()->setGraphicsEffect(fade_);
    connect(this, &QPlainTextEdit::textChanged, this, &InlineNoteEdit::fitContent);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value && !expanded_ && !hasFocus())
            verticalScrollBar()->setValue(0);
    });
}

InlineNoteEdit::InlineNoteEdit(const QString &text, QWidget *parent) : InlineNoteEdit(parent) {
    setPlainText(text);
}

void InlineNoteEdit::fitContent() {
    if (fitting_)
        return;
    fitting_ = true;
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // QPlainTextDocumentLayout reports its document height in lines, not pixels.
    // Measure the actual laid-out blocks so wrapping matches the editor exactly.
    const qreal margin = document()->documentMargin();
    qreal blocksHeight = 0;
    qreal secondLineTop = margin;
    qreal secondLineBottom = margin;
    int lines = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const QRectF blockRect = blockBoundingRect(block); // Ensures its line layout.
        const QTextLayout *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            if (++lines == 2) {
                secondLineTop = margin + blocksHeight + line.y();
                secondLineBottom = secondLineTop + line.height();
            }
        }
        blocksHeight += blockRect.height();
    }
    const bool previousNeedsCollapse = needsCollapse_;
    needsCollapse_ = lines > 2;
    const bool collapsed = needsCollapse_ && !expanded_;
    // The last block's rectangle already contains the bottom document margin.
    const qreal contentHeight = collapsed ? secondLineBottom + margin : blocksHeight + margin;
    const int chromeHeight = height() - viewport()->height();
    const int fittedHeight = std::max(40, int(std::ceil(contentHeight)) + chromeHeight + 1);
    if (height() != fittedHeight || minimumHeight() != fittedHeight || maximumHeight() != fittedHeight)
        setFixedHeight(fittedHeight);
    fade_->setFade(secondLineTop, secondLineBottom);
    fade_->setEnabled(collapsed);
    if (collapsed)
        verticalScrollBar()->setValue(0);
    else if (verticalScrollBar()->maximum() == 0)
        verticalScrollBar()->setValue(0);
    fitting_ = false;
    if (previousNeedsCollapse != needsCollapse_ && presentationChanged)
        presentationChanged();
}

void InlineNoteEdit::setExpanded(bool expanded) {
    if (expanded_ == expanded) {
        fitContent();
        return;
    }
    expanded_ = expanded;
    fitContent();
    if (presentationChanged)
        presentationChanged();
}

void InlineNoteEdit::focusInEvent(QFocusEvent *event) {
    setExpanded(true);
    QPlainTextEdit::focusInEvent(event);
    if (started)
        started();
}

void InlineNoteEdit::focusOutEvent(QFocusEvent *event) {
    QPlainTextEdit::focusOutEvent(event);
    if (suppressFocusFinish_) {
        suppressFocusFinish_ = false;
        return;
    }
    if (finished)
        finished();
}

void InlineNoteEdit::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    fitContent();
}

void InlineNoteEdit::showEvent(QShowEvent *event) {
    QPlainTextEdit::showEvent(event);
    fitContent();
}

void InlineNoteEdit::changeEvent(QEvent *event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ApplicationFontChange)
        QTimer::singleShot(0, this, &InlineNoteEdit::fitContent);
}

void InlineNoteEdit::keyPressEvent(QKeyEvent *event) {
    const bool save = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
                      (event->modifiers().testFlag(Qt::ControlModifier) ||
                       event->modifiers().testFlag(Qt::MetaModifier));
    if (event->key() != Qt::Key_Escape && !save) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    event->accept();
    suppressFocusFinish_ = true;
    QPointer<InlineNoteEdit> guard(this);
    const auto callback = save ? finished : cancelled;
    if (callback)
        callback();
    if (guard) {
        clearFocus();
        suppressFocusFinish_ = false;
    }
}

void InlineNoteEdit::wheelEvent(QWheelEvent *event) {
    // Leave wheel handling to the outer notes scroll area, including over text.
    event->ignore();
}

} // namespace h2d
