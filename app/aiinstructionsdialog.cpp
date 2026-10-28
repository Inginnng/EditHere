#include "aiinstructionsdialog.h"
#include "aicredentials.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace h2d {
AiInstructionsDialog::AiInstructionsDialog(const AppSettings &settings, AiFeedbackInput input,
                                         QWidget *parent, AiCredentials *credentials)
    : QDialog(parent), input_(std::move(input)),
      credentials_(credentials ? credentials : new AiCredentials(this)), client_(new AiFeedbackClient(this)) {
    setObjectName("aiInstructionsDialog");
    setWindowTitle(tr("AI解析"));
    resize(800, 700); setMinimumSize(540, 540);
    connection_ = settings.aiProviders.value(settings.aiProviderId, defaultAiConnection(settings.aiProviderId));
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22); layout->setSpacing(16);
    auto heading = new QHBoxLayout;
    auto titles = new QVBoxLayout;
    titles->setSpacing(4);
    auto title = new QLabel(tr("AI解析"), this);
    auto font = title->font(); font.setPointSize(font.pointSize() + 7); font.setBold(true); title->setFont(font);
    titles->addWidget(title);
    titles->addWidget(mutedLabel(tr("把画面上的批注，整理成清楚的修改意见。"), this));
    heading->addLayout(titles, 1);
    configure_ = textButton(tr("API 设置"), false, this); configure_->setObjectName("aiInstructionsSettings");
    configure_->setIcon(glyph("settings"));
    heading->addWidget(configure_);
    layout->addLayout(heading);
    QString providerName = settings.aiProviderId;
    for (const auto &preset : aiProviderPresets()) if (preset.id == settings.aiProviderId) providerName = preset.name;
    auto inputCard = new QFrame(this); inputCard->setObjectName("aiAnalysisInputCard"); inputCard->setProperty("aiPanel", true);
    auto inputLayout = new QVBoxLayout(inputCard);
    inputLayout->setContentsMargins(16, 14, 16, 14); inputLayout->setSpacing(12);
    auto providerRow = new QHBoxLayout;
    auto icon = new QLabel(inputCard); icon->setPixmap(glyph("notes", accent()).pixmap(24, 24));
    providerRow->addWidget(icon); providerRow->addSpacing(4);
    auto summary = new QLabel(inputCard);
    summary->setText(summary->fontMetrics().elidedText(tr("%1 · %2").arg(providerName, connection_.model), Qt::ElideRight, 420));
    summary->setObjectName("aiInstructionsSummary"); summary->setTextFormat(Qt::PlainText);
    auto providerFont = summary->font(); providerFont.setBold(true); summary->setFont(providerFont);
    summary->setToolTip(connection_.baseUrl + '\n' + connection_.model);
    providerRow->addWidget(summary, 1);
    auto count = mutedLabel(tr("%1 张图片").arg(input_.images.size()), inputCard);
    providerRow->addWidget(count); inputLayout->addLayout(providerRow);
    auto previews = new QHBoxLayout; previews->setSpacing(8);
    for (qsizetype i = 0; i < qMin<qsizetype>(3, input_.images.size()); ++i) {
        auto preview = new QLabel(inputCard); preview->setObjectName("aiInputPreview");
        preview->setFixedSize(104, 64); preview->setAlignment(Qt::AlignCenter);
        preview->setPixmap(QPixmap::fromImage(input_.images[i]).scaled(100, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (i < input_.imageLabels.size()) preview->setToolTip(input_.imageLabels[i]);
        previews->addWidget(preview);
    }
    if (input_.images.size() > 3) previews->addWidget(mutedLabel(tr("另 %1 张").arg(input_.images.size() - 3), inputCard));
    previews->addStretch(); inputLayout->addLayout(previews);
    auto disclosure = mutedLabel(tr("图片发往所选 API；复制给 Agent 的只有文字。"), inputCard);
    disclosure->setWordWrap(true); inputLayout->addWidget(disclosure);
    layout->addWidget(inputCard);
    auto resultCard = new QFrame(this); resultCard->setObjectName("aiAnalysisResultCard"); resultCard->setProperty("aiPanel", true);
    auto resultLayout = new QVBoxLayout(resultCard); resultLayout->setContentsMargins(16, 14, 16, 14);
    auto resultHeading = new QHBoxLayout;
    auto resultTitle = new QLabel(tr("修改意见"), resultCard);
    resultTitle->setProperty("sectionTitle", true);
    resultHeading->addWidget(resultTitle); resultHeading->addStretch();
    resultHeading->addWidget(mutedLabel(tr("解析后可直接编辑"), resultCard));
    resultLayout->addLayout(resultHeading);
    content_ = new QStackedWidget(resultCard);
    auto empty = new QWidget(content_); empty->setObjectName("aiInstructionsEmpty");
    auto emptyLayout = new QVBoxLayout(empty); emptyLayout->setSpacing(10); emptyLayout->addStretch();
    auto emptyIcon = new QLabel(empty); emptyIcon->setPixmap(glyph("notes", accent()).pixmap(32, 32));
    emptyIcon->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyIcon);
    emptyTitle_ = new QLabel(empty); emptyTitle_->setAlignment(Qt::AlignCenter);
    auto emptyFont = emptyTitle_->font(); emptyFont.setBold(true); emptyFont.setPointSize(emptyFont.pointSize() + 2);
    emptyTitle_->setFont(emptyFont); emptyLayout->addWidget(emptyTitle_);
    emptyHint_ = mutedLabel({}, empty); emptyHint_->setAlignment(Qt::AlignCenter); emptyHint_->setWordWrap(true);
    emptyLayout->addWidget(emptyHint_); emptyLayout->addStretch();
    content_->addWidget(empty);
    output_ = new QPlainTextEdit(this); output_->setObjectName("aiInstructionsText");
    output_->setAccessibleName(tr("修改意见"));
    output_->setPlaceholderText(tr("生成后可在这里核对、编辑，再复制给 Agent。"));
    content_->addWidget(output_); resultLayout->addWidget(content_, 1);
    layout->addWidget(resultCard, 1);
    progress_ = new QProgressBar(this); progress_->setRange(0, 0); progress_->setTextVisible(false);
    progress_->hide(); layout->addWidget(progress_);
    status_ = new QLabel(tr("正在读取 API Key…"), this); status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("aiInstructionsStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    usage_ = mutedLabel({}, this); usage_->setObjectName("aiInstructionsUsage"); layout->addWidget(usage_);
    auto row = new QHBoxLayout;
    generate_ = textButton(tr("开始解析"), true, this); generate_->setObjectName("aiInstructionsGenerate");
    cancel_ = textButton(tr("停止"), false, this); cancel_->setObjectName("aiInstructionsCancel");
    copy_ = textButton(tr("复制文字"), false, this); copy_->setObjectName("aiInstructionsCopy");
    save_ = textButton(tr("另存为…"), false, this); save_->setObjectName("aiInstructionsSave");
    row->addWidget(save_); row->addStretch();
    row->addWidget(cancel_); row->addWidget(generate_); row->addWidget(copy_);
    layout->addLayout(row);
    connect(configure_, &QPushButton::clicked, this, [this] { done(Configure); });
    connect(generate_, &QPushButton::clicked, this, &AiInstructionsDialog::generate);
    connect(cancel_, &QPushButton::clicked, client_, &AiFeedbackClient::cancel);
    connect(output_, &QPlainTextEdit::textChanged, this, &AiInstructionsDialog::updateOutputActions);
    connect(copy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(output_->toPlainText()); status_->setText(tr("修改意见已复制，未附带图片。"));
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("保存修改意见"), "modification-instructions.txt",
                                                     tr("文本文件 (*.txt);;Markdown (*.md)"));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const auto bytes = output_->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            status_->setText(tr("文字保存失败：%1").arg(file.errorString()));
        else status_->setText(tr("修改意见已保存。"));
    });
    connect(client_, &AiFeedbackClient::completed, this, [this](const QString &text) {
        setBusy(false); output_->setPlainText(text);
        status_->setText(tr("解析完成，请核对后复制给 Agent。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this](const QString &message) {
        setBusy(false); status_->setText(message);
    });
    connect(client_, &AiFeedbackClient::usageAvailable, this, [this](qint64 input, qint64 output) {
        usage_->setText(tr("输入 %1 · 输出 %2 tokens").arg(input).arg(output));
    });
    connect(credentials_, &AiCredentials::loaded, this, [this](const QString &id, const QString &key) {
        if (id != connection_.providerId) return;
        key_ = key; credentialsLoaded_ = true; setBusy(false);
        status_->setText(key.isEmpty() ? tr("请先在 API 设置中填写并保存此提供商的 Key。")
                                      : tr("准备就绪。点击开始解析后发起本次请求。"));
    });
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        status_->setText(message); credentialsLoaded_ = false; setBusy(false);
    });
    setBusy(false);
    refreshStyle();
    QTimer::singleShot(0, this, [this] {
        const auto error = validateAiConnection(connection_);
        if (!error.isEmpty()) { status_->setText(error); return; }
        credentials_->load(connection_.providerId);
    });
}
AiInstructionsDialog::~AiInstructionsDialog() { client_->cancel(); key_.fill(QChar()); key_.clear(); }
void AiInstructionsDialog::done(int result) {
    client_->cancel();
    QDialog::done(result);
}
void AiInstructionsDialog::generate() {
    if (client_->isRunning() || !credentialsLoaded_ || key_.isEmpty()) return;
    setBusy(true); usage_->clear();
    status_->setText(tr("正在理解本次截图和批注…"));
    client_->generate(connection_, key_, input_);
}
void AiInstructionsDialog::setBusy(bool busy) {
    busy_ = busy;
    progress_->setVisible(busy); cancel_->setVisible(busy);
    generate_->setEnabled(!busy && credentialsLoaded_ && !key_.isEmpty());
    configure_->setEnabled(!busy); output_->setReadOnly(busy);
    emptyTitle_->setText(busy ? tr("正在理解你的批注") : tr("准备好开始解析"));
    emptyHint_->setText(busy ? tr("正在识别画面对象、批注和布局关系…") : tr("解析结果会保留批注原话、坐标和移动信息。"));
    updateOutputActions();
}
void AiInstructionsDialog::updateOutputActions() {
    const bool hasText = !output_->toPlainText().trimmed().isEmpty();
    const bool available = hasText && !busy_;
    content_->setCurrentIndex(hasText && !busy_ ? 1 : 0);
    copy_->setEnabled(available); save_->setEnabled(available);
    copy_->setVisible(hasText && !busy_); save_->setVisible(hasText && !busy_);
    generate_->setVisible(!busy_);
    generate_->setText(hasText ? tr("重新解析") : tr("开始解析"));
    for (const auto button : {generate_, copy_}) {
        const bool primary = button == copy_ ? hasText : !hasText;
        if (button->property("primary").toBool() != primary) {
            button->setProperty("primary", primary);
            button->style()->unpolish(button); button->style()->polish(button); button->update();
        }
    }
}
void AiInstructionsDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) refreshStyle();
}
void AiInstructionsDialog::refreshStyle() {
    // Applying a stylesheet can synchronously emit another PaletteChange.
    if (refreshingStyle_) return;
    refreshingStyle_ = true;
    const auto colors = palette();
    const auto css = QString(
        "QDialog#aiInstructionsDialog QFrame[aiPanel=\"true\"] { background:%1; border:1px solid %2; border-radius:12px; }"
        "QDialog#aiInstructionsDialog QLabel#aiInputPreview { background:%3; border:1px solid %2; border-radius:6px; }"
        "QDialog#aiInstructionsDialog QPlainTextEdit#aiInstructionsText { background:transparent; border:0; padding:0; }")
        .arg(colors.color(QPalette::Base).name(), colors.color(QPalette::Midlight).name(), colors.color(QPalette::AlternateBase).name());
    if (styleSheet() != css) setStyleSheet(css);
    refreshingStyle_ = false;
}
} // namespace h2d
