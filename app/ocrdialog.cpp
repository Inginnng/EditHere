#include "ocrdialog.h"
#include "diagnostics.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace h2d {
namespace {
// The picture with the box of the line the user picked drawn over it. Drawing is the
// whole of its job, so it carries no signals and needs no meta object.
class OcrPreview final : public QWidget {
  public:
    explicit OcrPreview(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(240, 160);
    }
    void setPicture(const QImage &picture) {
        picture_ = picture;
        update();
    }
    void setHighlight(const QRectF &box) {
        box_ = box;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(18, 19, 24));
        if (picture_.isNull())
            return;
        // The picture is fitted rather than cropped, so every box stays on screen
        // however the window is resized.
        const QSize scaled = picture_.size().scaled(size() - QSize(8, 8), Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(area, picture_);
        if (box_.isNull())
            return;
        // The box was normalised against the picture, so it is scaled by the area the
        // picture ended up in rather than by the picture's own size.
        const QRectF target(area.left() + box_.x() * area.width(), area.top() + box_.y() * area.height(),
                            box_.width() * area.width(), box_.height() * area.height());
        painter.setPen(QPen(QColor(255, 138, 76), 2));
        painter.setBrush(QColor(255, 138, 76, 70));
        painter.drawRect(target);
    }

  private:
    QImage picture_;
    QRectF box_;
};
} // namespace

OcrDialog::OcrDialog(QImage image, OcrLanguageMode language, QWidget *parent)
    : QDialog(parent), image_(std::move(image)) {
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumSize(760, 480);
    auto *layout = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("识别语言"), this));
    language_ = new QComboBox(this);
    for (const auto mode : {OcrLanguageMode::System, OcrLanguageMode::SimplifiedChinese,
                            OcrLanguageMode::English}) {
        language_->addItem(ocrLanguageLabel(mode), int(mode));
    }
    language_->setCurrentIndex(language_->findData(int(language)));
    top->addWidget(language_);
    status_ = new QLabel(this);
    status_->setProperty("muted", true);
    top->addWidget(status_, 1);
    layout->addLayout(top);
    auto *body = new QHBoxLayout;
    auto *preview = new OcrPreview(this);
    previewWidget_ = preview;
    preview->setObjectName("ocrPreview");
    preview->setPicture(image_);
    body->addWidget(preview, 3);
    lines_ = new QListWidget(this);
    lines_->setSelectionMode(QAbstractItemView::SingleSelection);
    lines_->setWordWrap(true);
    body->addWidget(lines_, 2);
    layout->addLayout(body, 1);
    auto *buttons = new QHBoxLayout;
    copyAll_ = textButton(tr("复制全部"), true, this);
    copyLine_ = textButton(tr("复制选中行"), false, this);
    retry_ = textButton(tr("重新识别"), false, this);
    close_ = textButton(tr("关闭"), false, this);
    buttons->addStretch(1);
    for (auto *button : {copyLine_, copyAll_, retry_, close_})
        buttons->addWidget(button);
    layout->addLayout(buttons);
    connect(close_, &QPushButton::clicked, this, &QDialog::close);
    connect(copyAll_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(text());
    });
    connect(copyLine_, &QPushButton::clicked, this, [this] {
        if (auto *item = lines_->currentItem())
            QApplication::clipboard()->setText(item->text());
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        emit retryRequested(static_cast<OcrLanguageMode>(language_->currentData().toInt()));
    });
    // Picking a line in the list is what highlights it on the picture.
    connect(lines_, &QListWidget::currentRowChanged, this, [this, preview](int row) {
        preview->setHighlight(row >= 0 && row < boxes_.size() ? boxes_.at(row) : QRectF());
    });
    retranslate();
    setBusy(true);
}

void OcrDialog::retranslate() {
    setWindowTitle(tr("文字识别"));
    copyAll_->setText(tr("复制全部"));
    copyLine_->setText(tr("复制选中行"));
    retry_->setText(tr("重新识别"));
    close_->setText(tr("关闭"));
    refreshPreview();
}

void OcrDialog::refreshPreview() {
    lines_->clear();
    boxes_.clear();
    for (const auto &line : result_) {
        lines_->addItem(line.text);
        boxes_.append(line.box);
    }
    if (!result_.isEmpty())
        lines_->setCurrentRow(0);
}

QString OcrDialog::text() const {
    QStringList parts;
    for (const auto &line : result_)
        parts.append(line.text);
    return parts.join(QLatin1Char('\n'));
}

void OcrDialog::setImage(const QImage &image, OcrLanguageMode language) {
    image_ = image;
    result_.clear();
    refreshPreview();
    auto *preview = static_cast<OcrPreview *>(previewWidget_);
    preview->setPicture(image_);
    preview->setHighlight({});
    language_->setCurrentIndex(language_->findData(int(language)));
    setBusy(true);
}

void OcrDialog::setBusy(bool busy) {
    busy_ = busy;
    for (auto *button : {copyAll_, copyLine_, retry_})
        button->setEnabled(!busy);
    language_->setEnabled(!busy);
    if (busy)
        status_->setText(tr("正在识别…"));
}

void OcrDialog::setResult(const OcrResult &result) {
    setBusy(false);
    result_ = result.lines;
    refreshPreview();
    if (!result.ok) {
        diagnostics::write(diagnostics::Level::Error, "ocr.recognize", result.message,
                           {{"width", image_.width()}, {"height", image_.height()}, {"language", result.engineLanguage}});
        // A failure is shown here rather than in a message box, so the picture the
        // user was working on stays on screen next to the reason.
        status_->setText(result.message);
        copyAll_->setEnabled(false);
        copyLine_->setEnabled(false);
        return;
    }
    if (result.lines.isEmpty()) {
        status_->setText(tr("没有识别到文字。"));
        return;
    }
    status_->setText(tr("识别到 %1 行 · %2").arg(result.lines.size()).arg(result.engineLanguage));
}
} // namespace h2d
