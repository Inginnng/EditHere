#include "aiinstructionsdialog.h"
#include "aicredentials.h"
#include "aiprovidericons.h"
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
#include <QScreen>
#include <QScrollArea>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace h2d {
AiInstructionsDialog::AiInstructionsDialog(const AppSettings &settings, AiFeedbackInput input,
                                         QWidget *parent, AiCredentials *credentials)
    : QDialog(parent), input_(std::move(input)),
      credentials_(credentials ? credentials : new AiCredentials(this)), client_(new AiFeedbackClient(this)) {
    setObjectName("aiInstructionsDialog");
    setWindowTitle(tr("AI解析"));
    setMinimumSize(720, 580);
    const auto available = screen()->availableGeometry().size() - QSize(48, 64);
    resize(QSize(1080, 740).boundedTo(available).expandedTo(minimumSize()));
    connection_ = settings.aiProviders.value(settings.aiProviderId, defaultAiConnection(settings.aiProviderId));
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto header = new QWidget(this); header->setObjectName("aiAnalysisHeader");
    auto headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(28, 24, 28, 22);
    auto heading = new QHBoxLayout;
    auto titles = new QVBoxLayout;
    titles->setSpacing(4);
    auto title = new QLabel(tr("AI解析"), this);
    title->setObjectName("aiAnalysisTitle");
    titles->addWidget(title);
    titles->addWidget(mutedLabel(tr("把截图和批注，整理成可直接交付的文字。"), header));
    heading->addLayout(titles, 1);
    configure_ = textButton(tr("API 设置"), false, this); configure_->setObjectName("aiInstructionsSettings");
    configure_->setIcon(glyph("settings"));
    configure_->setProperty("quiet", true);
    heading->addWidget(configure_);
    headerLayout->addLayout(heading);
    layout->addWidget(header);
    QString providerName = settings.aiProviderId;
    for (const auto &preset : aiProviderPresets()) if (preset.id == settings.aiProviderId) providerName = preset.name;
    auto workspace = new QWidget(this);
    auto columns = new QHBoxLayout(workspace); columns->setContentsMargins(0, 0, 0, 0); columns->setSpacing(0);
    sourcePanel_ = new QWidget(workspace); sourcePanel_->setObjectName("aiAnalysisSources");
    auto inputLayout = new QVBoxLayout(sourcePanel_);
    inputLayout->setContentsMargins(24, 24, 24, 24); inputLayout->setSpacing(16);
    auto sourcesHeading = new QHBoxLayout;
    auto sourcesTitle = new QLabel(tr("本次画面"), sourcePanel_); sourcesTitle->setProperty("analysisSection", true);
    sourcesHeading->addWidget(sourcesTitle); sourcesHeading->addStretch();
    sourcesHeading->addWidget(mutedLabel(tr("%1 张").arg(input_.images.size()), sourcePanel_));
    inputLayout->addLayout(sourcesHeading);
    auto previews = new QScrollArea(sourcePanel_); previews->setWidgetResizable(true);
    previews->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto previewContent = new QWidget(previews);
    previewContent->setObjectName("aiAnalysisPreviewList");
    auto previewLayout = new QVBoxLayout(previewContent); previewLayout->setContentsMargins(0, 0, 6, 0); previewLayout->setSpacing(12);
    for (qsizetype i = 0; i < input_.images.size(); ++i) {
        auto preview = new QLabel(previewContent); preview->setObjectName("aiInputPreview");
        preview->setFixedHeight(142); preview->setMinimumWidth(0); preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        preview->setAlignment(Qt::AlignCenter);
        preview->setPixmap(QPixmap::fromImage(input_.images[i]).scaled(186, 126, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (i < input_.imageLabels.size()) preview->setToolTip(input_.imageLabels[i]);
        previewLayout->addWidget(preview);
        auto caption = mutedLabel(tr("画面 %1 · %2 × %3").arg(i + 1).arg(input_.images[i].width()).arg(input_.images[i].height()), previewContent);
        caption->setWordWrap(true); previewLayout->addWidget(caption);
    }
    previewLayout->addStretch(); previews->setWidget(previewContent); inputLayout->addWidget(previews, 1);
    auto providerRow = new QHBoxLayout; providerRow->setSpacing(10);
    providerIcon_ = new QLabel(sourcePanel_); providerIcon_->setFixedSize(24, 24); providerRow->addWidget(providerIcon_);
    auto summary = new QLabel(providerName, sourcePanel_); summary->setObjectName("aiInstructionsSummary");
    summary->setTextFormat(Qt::PlainText); summary->setProperty("analysisSection", true);
    providerRow->addWidget(summary, 1); inputLayout->addLayout(providerRow);
    auto model = mutedLabel(connection_.model, sourcePanel_); model->setTextFormat(Qt::PlainText);
    model->setWordWrap(true); model->setToolTip(connection_.baseUrl); inputLayout->addWidget(model);
    auto disclosure = mutedLabel(tr("图片仅发往所选 API。\n交付给 Agent 的是纯文本。"), sourcePanel_);
    disclosure->setWordWrap(true); inputLayout->addWidget(disclosure);
    columns->addWidget(sourcePanel_);
    auto resultCard = new QWidget(workspace); resultCard->setObjectName("aiAnalysisResult");
    auto resultLayout = new QVBoxLayout(resultCard); resultLayout->setContentsMargins(28, 24, 28, 24); resultLayout->setSpacing(16);
    auto resultHeading = new QHBoxLayout;
    auto resultTitle = new QLabel(tr("修改意见"), resultCard);
    resultTitle->setProperty("analysisSection", true);
    resultHeading->addWidget(resultTitle); resultHeading->addStretch();
    auto editable = mutedLabel(tr("可直接编辑"), resultCard); resultHeading->addWidget(editable);
    resultLayout->addLayout(resultHeading);
    content_ = new QStackedWidget(resultCard);
    auto empty = new QWidget(content_); empty->setObjectName("aiInstructionsEmpty");
    auto emptyLayout = new QVBoxLayout(empty); emptyLayout->setContentsMargins(16, 16, 16, 16);
    emptyLayout->setSpacing(14); emptyLayout->addStretch();
    emptyIcon_ = new QLabel(empty); emptyIcon_->setObjectName("aiAnalysisEmptyIcon");
    emptyIcon_->setAlignment(Qt::AlignCenter); emptyIcon_->setFixedSize(64, 64);
    emptyLayout->addWidget(emptyIcon_, 0, Qt::AlignHCenter);
    emptyTitle_ = new QLabel(empty); emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyTitle_->setObjectName("aiAnalysisEmptyTitle"); emptyTitle_->setWordWrap(true); emptyLayout->addWidget(emptyTitle_);
    emptyHint_ = mutedLabel({}, empty); emptyHint_->setAlignment(Qt::AlignCenter); emptyHint_->setWordWrap(true);
    emptyLayout->addWidget(emptyHint_); emptyLayout->addStretch();
    content_->addWidget(empty);
    output_ = new QPlainTextEdit(this); output_->setObjectName("aiInstructionsText");
    output_->setAccessibleName(tr("修改意见"));
    output_->setPlaceholderText(tr("生成后可在这里核对、编辑，再复制给 Agent。"));
    content_->addWidget(output_); resultLayout->addWidget(content_, 1);
    columns->addWidget(resultCard, 1); layout->addWidget(workspace, 1);
    progress_ = new QProgressBar(resultCard); progress_->setRange(0, 0); progress_->setTextVisible(false);
    progress_->setFixedHeight(3); progress_->hide(); resultLayout->addWidget(progress_);
    usage_ = mutedLabel({}, resultCard); usage_->setObjectName("aiInstructionsUsage");
    usage_->hide(); resultLayout->addWidget(usage_);
    auto footer = new QWidget(this); footer->setObjectName("aiAnalysisFooter");
    auto footerLayout = new QVBoxLayout(footer); footerLayout->setContentsMargins(28, 16, 28, 18); footerLayout->setSpacing(12);
    status_ = new QLabel(tr("正在读取 API Key…"), footer); status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("aiInstructionsStatus"); status_->setWordWrap(true); footerLayout->addWidget(status_);
    auto row = new QHBoxLayout;
    generate_ = textButton(tr("开始解析"), true, this); generate_->setObjectName("aiInstructionsGenerate");
    cancel_ = textButton(tr("停止"), false, this); cancel_->setObjectName("aiInstructionsCancel");
    copy_ = textButton(tr("复制文字"), false, this); copy_->setObjectName("aiInstructionsCopy");
    save_ = textButton(tr("另存为…"), false, this); save_->setObjectName("aiInstructionsSave");
    save_->setProperty("quiet", true);
    row->addWidget(save_); row->addStretch();
    row->addWidget(cancel_); row->addWidget(generate_); row->addWidget(copy_);
    footerLayout->addLayout(row); layout->addWidget(footer);
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
        QTextCursor cursor(output_->document());
        cursor.select(QTextCursor::Document);
        QTextBlockFormat format;
        format.setLineHeight(145, QTextBlockFormat::ProportionalHeight);
        cursor.mergeBlockFormat(format);
        cursor.setPosition(0); output_->setTextCursor(cursor);
        status_->setProperty("error", false);
        status_->setText(tr("解析完成，请核对后复制给 Agent。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this](const QString &message) {
        setBusy(false); status_->setProperty("error", true); status_->setText(message);
        status_->style()->unpolish(status_); status_->style()->polish(status_);
    });
    connect(client_, &AiFeedbackClient::usageAvailable, this, [this](qint64 input, qint64 output) {
        usage_->setText(tr("输入 %1 · 输出 %2 tokens").arg(input).arg(output));
        usage_->show();
    });
    connect(credentials_, &AiCredentials::loaded, this, [this](const QString &id, const QString &key) {
        if (id != connection_.providerId) return;
        key_ = key; credentialsLoaded_ = true; setBusy(false);
        status_->setText(key.isEmpty() ? tr("请先在 API 设置中填写并保存此提供商的 Key。")
                                      : tr("已就绪，开始解析后生成修改意见。"));
    });
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        status_->setText(message); status_->setProperty("error", true);
        status_->style()->unpolish(status_); status_->style()->polish(status_);
        credentialsLoaded_ = false; setBusy(false);
    });
    setBusy(false);
    updateSourceWidth();
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
    setBusy(true); usage_->clear(); usage_->hide();
    status_->setProperty("error", false);
    status_->style()->unpolish(status_); status_->style()->polish(status_);
    status_->setText(tr("正在理解本次截图和批注…"));
    client_->generate(connection_, key_, input_);
}
void AiInstructionsDialog::setBusy(bool busy) {
    busy_ = busy;
    progress_->setVisible(busy); cancel_->setVisible(busy);
    generate_->setEnabled(!busy && credentialsLoaded_ && !key_.isEmpty());
    configure_->setEnabled(!busy); output_->setReadOnly(busy);
    emptyTitle_->setText(busy ? tr("正在理解你的批注") : tr("让修改意见更清楚"));
    emptyHint_->setText(busy ? tr("正在识别画面对象、批注和布局关系…") : tr("结合画面理解每条批注，\n保留原话、坐标和移动信息。"));
    if (!busy && credentialsLoaded_ && key_.isEmpty()) {
        emptyTitle_->setText(tr("先连接你的 AI 服务"));
        emptyHint_->setText(tr("填写 API Key 后，就可以解析当前批注。"));
    }
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
void AiInstructionsDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updateSourceWidth();
}
void AiInstructionsDialog::updateSourceWidth() {
    if (sourcePanel_) sourcePanel_->setFixedWidth(width() < 880 ? 242 : 284);
}
void AiInstructionsDialog::refreshStyle() {
    // Applying a stylesheet can synchronously emit another PaletteChange.
    if (refreshingStyle_) return;
    refreshingStyle_ = true;
    const bool dark = isDarkTheme();
    const auto css = QString(R"(
QDialog#aiInstructionsDialog { background:%1; }
QDialog#aiInstructionsDialog QWidget { font-size:13px; }
QWidget#aiAnalysisHeader { background:%1; border-bottom:1px solid %3; }
QWidget#aiAnalysisSources { background:%2; border-right:1px solid %3; }
QWidget#aiAnalysisPreviewList { background:%2; }
QWidget#aiAnalysisResult { background:%1; }
QWidget#aiAnalysisFooter { background:%1; border-top:1px solid %3; }
QDialog#aiInstructionsDialog QLabel#aiAnalysisTitle { font-size:24px; font-weight:600; }
QDialog#aiInstructionsDialog QLabel[analysisSection="true"] { font-size:14px; font-weight:600; }
QDialog#aiInstructionsDialog QLabel#aiAnalysisEmptyTitle { font-size:20px; font-weight:600; }
QLabel#aiAnalysisEmptyIcon { background:%2; border-radius:18px; }
QLabel#aiInputPreview { background:%1; border:1px solid %3; border-radius:8px; }
QDialog#aiInstructionsDialog QPlainTextEdit#aiInstructionsText { background:transparent; border:0; padding:0; font-size:14px; }
QPushButton { padding:9px 16px; border-radius:7px; }
QPushButton[quiet="true"] { background:transparent; color:%4; }
QPushButton[quiet="true"]:hover { background:%2; }
QLabel#aiInstructionsStatus { color:%4; }
QLabel#aiInstructionsStatus[error="true"] { color:%6; }
QProgressBar { border:0; background:%2; }
QProgressBar::chunk { background:%5; }
)").arg(dark ? "#23252b" : "#ffffff", dark ? "#1d1f24" : "#f7f8fa",
       dark ? "#34373f" : "#e9ebef", dark ? "#a7acb8" : "#717783", accent().name(),
       dark ? "#ff929c" : "#bd3545");
    if (styleSheet() != css) setStyleSheet(css);
    if (providerIcon_) providerIcon_->setPixmap(aiProviderIcon(connection_.providerId).pixmap(24, 24));
    if (emptyIcon_) emptyIcon_->setPixmap(glyph("edit", dark ? QColor("#a0b5d5") : QColor("#687e9f")).pixmap(28, 28));
    refreshingStyle_ = false;
}
} // namespace h2d
