#include "aiinstructionsdialog.h"
#include "aicredentials.h"
#include "aifeedbackformat.h"
#include "aiprovidericons.h"
#include "aipromptdialog.h"
#include "aistyle.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace h2d {
namespace {
class SourceImageView final : public QWidget {
  public:
    explicit SourceImageView(QWidget *parent) : QWidget(parent) {
        setMinimumSize(160, 160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(const QImage &image) { image_ = image; update(); }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (image_.isNull()) return;
        const auto size = image_.size().scaled((this->size() - QSize(24, 24)).expandedTo(QSize(1, 1)), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.drawImage(target, image_);
    }
  private:
    QImage image_;
};
}
AiInstructionsDialog::AiInstructionsDialog(const AppSettings &settings, AiFeedbackInput input,
                                         QWidget *parent, AiCredentials *credentials, AiInstructionsDraft draft,
                                         AiCredentials *providerProbe)
    : QDialog(parent), input_(std::move(input)),
      credentials_(credentials ? credentials : new AiCredentials(this)),
      providerProbe_(providerProbe), client_(new AiFeedbackClient(this)) {
    setObjectName("aiInstructionsDialog");
    setWindowTitle(tr("AI 解析（预览功能）"));
    setMinimumSize(760, 580);
    const auto available = screen()->availableGeometry().size() - QSize(48, 64);
    resize(QSize(1120, 780).boundedTo(available).expandedTo(minimumSize()));
    settings_ = settings;
    connection_ = settings.aiProviders.value(settings.aiProviderId, defaultAiConnection(settings.aiProviderId));
    connection_.providerId = settings.aiProviderId;
    systemPrompt_ = settings.aiSystemPrompt;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);

    auto *header = new QWidget(this); header->setObjectName("aiAnalysisHeader");
    auto *heading = new QHBoxLayout(header); heading->setContentsMargins(28, 22, 28, 22); heading->setSpacing(14);
    auto *mark = new QLabel(header); mark->setObjectName("aiAnalysisMark"); mark->setFixedSize(42, 42);
    mark->setAlignment(Qt::AlignCenter); mark->setPixmap(glyph("edit").pixmap(22, 22));
    heading->addWidget(mark);
    auto *titles = new QVBoxLayout; titles->setSpacing(4);
    auto *title = new QLabel(tr("AI 解析（预览功能）"), header); title->setObjectName("aiAnalysisTitle");
    titles->addWidget(title); titles->addWidget(mutedLabel(tr("预览功能：把图形化修改转成文字需求，结果请核对后再交给 Agent。"), header));
    heading->addLayout(titles, 1);
    configure_ = textButton(tr("API 设置"), false, header);
    configure_->setObjectName("aiInstructionsSettings");
    configure_->setIcon(aiProviderIcon(connection_.providerId)); configure_->setIconSize({20, 20});
    // The label follows what is actually configured, not a built-in default.
    configure_->setToolTip(tr("正在读取已配置的服务商…")); configure_->setAccessibleName(tr("API 设置"));
    heading->addWidget(configure_);
    providerSwitch_ = iconButton("chevron-down", tr("切换服务商"), header);
    providerSwitch_->setObjectName("aiInstructionsProviderSwitch");
    providerSwitch_->setFixedWidth(36);
    providerSwitch_->hide();
    heading->addWidget(providerSwitch_); layout->addWidget(header);

    splitter_ = new QSplitter(Qt::Horizontal, this); splitter_->setObjectName("aiAnalysisSplitter");
    splitter_->setChildrenCollapsible(false); splitter_->setHandleWidth(1);
    sourcePanel_ = new QWidget(splitter_); sourcePanel_->setObjectName("aiAnalysisSources");
    sourcePanel_->setMinimumWidth(224);
    auto *sources = new QVBoxLayout(sourcePanel_); sources->setContentsMargins(20, 20, 20, 20); sources->setSpacing(14);
    QJsonArray facts; QString ignored;
    aiCollectFacts(input_.feedback, &facts, &ignored);
    auto *sourceHeading = new QHBoxLayout;
    auto *sourceTitle = new QLabel(tr("画面与批注"), sourcePanel_); sourceTitle->setProperty("analysisSection", true);
    sourceHeading->addWidget(sourceTitle); sourceHeading->addStretch();
    sourceHeading->addWidget(mutedLabel(tr("%1 条").arg(facts.size()), sourcePanel_)); sources->addLayout(sourceHeading);
    auto *imagePage = new QWidget(sourcePanel_);
    auto *imageLayout = new QVBoxLayout(imagePage); imageLayout->setContentsMargins(0, 0, 0, 0); imageLayout->setSpacing(10);
    auto *imagePicker = new QComboBox(imagePage); imagePicker->setObjectName("aiSourceImage");
    imagePicker->setAccessibleName(tr("选择画面")); imagePicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    imagePicker->setMinimumContentsLength(10); imagePicker->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (qsizetype i = 0; i < input_.images.size(); ++i) {
        auto label = i < input_.imageLabels.size() ? input_.imageLabels[i].section(QChar(0xff1b), 0, 0) : tr("画面 %1").arg(i + 1);
        if (label.size() > 32) label = label.left(31) + QChar(0x2026);
        imagePicker->addItem(label);
    }
    imagePicker->setVisible(input_.images.size() > 1); imageLayout->addWidget(imagePicker);
    auto *imageFrame = new QFrame(imagePage); imageFrame->setObjectName("aiImageFrame"); imageFrame->setFixedHeight(218);
    auto *frameLayout = new QVBoxLayout(imageFrame); frameLayout->setContentsMargins(0, 0, 0, 0);
    auto *preview = new SourceImageView(imageFrame); preview->setObjectName("aiInputPreview");
    preview->setAccessibleName(tr("本次解析的截图")); frameLayout->addWidget(preview);
    imageFrame->setVisible(!input_.images.isEmpty());
    imageLayout->addWidget(imageFrame);
    auto *captionRow = new QHBoxLayout;
    auto *caption = mutedLabel({}, imagePage); caption->setObjectName("aiSourceCaption"); captionRow->addWidget(caption, 1);
    auto *expand = textButton(tr("放大"), false, imagePage); expand->setObjectName("aiExpandImage"); expand->setProperty("quiet", true);
    expand->setVisible(!input_.images.isEmpty()); caption->setVisible(!input_.images.isEmpty());
    captionRow->addWidget(expand); imageLayout->addLayout(captionRow);
    auto updateImage = [this, preview, caption](int index) {
        if (index < 0 || index >= input_.images.size()) return;
        const auto &image = input_.images[index]; preview->setImage(image);
        caption->setText(tr("%1 × %2 px").arg(image.width()).arg(image.height()));
        preview->setToolTip(input_.imageLabels.value(index));
    };
    connect(imagePicker, &QComboBox::currentIndexChanged, this, updateImage); updateImage(0);
    connect(expand, &QPushButton::clicked, this, [this, imagePicker] {
        const auto index = imagePicker->currentIndex();
        if (index < 0 || index >= input_.images.size()) return;
        QDialog dialog(this); dialog.setWindowTitle(tr("查看画面")); dialog.resize(QSize(1000, 720).boundedTo(screen()->availableGeometry().size()));
        auto *body = new QVBoxLayout(&dialog);
        auto *scroll = new QScrollArea(&dialog);
        auto *image = new QLabel(scroll); image->setPixmap(QPixmap::fromImage(input_.images[index]));
        scroll->setWidget(image); body->addWidget(scroll);
        auto *close = textButton(tr("关闭"), false, &dialog); body->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept); dialog.exec();
    });
    auto *noteScroll = new QScrollArea(imagePage); noteScroll->setWidgetResizable(true);
    noteScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *notes = new QWidget(noteScroll); notes->setObjectName("aiSourceNotes");
    auto *noteLayout = new QVBoxLayout(notes); noteLayout->setContentsMargins(0, 0, 4, 0); noteLayout->setSpacing(10);
    for (qsizetype i = 0; i < facts.size(); ++i) {
        const auto fact = facts[i].toObject();
        auto *card = new QFrame(notes); card->setObjectName("aiSourceNote");
        auto *body = new QVBoxLayout(card); body->setContentsMargins(14, 12, 14, 14); body->setSpacing(8);
        const auto scope = aiFactScope(fact);
        auto labelText = tr("批注 %1").arg(i + 1);
        if (!scope.isEmpty()) labelText += " · " + scope;
        auto *label = mutedLabel(labelText, card); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); body->addWidget(label);
        auto text = aiOriginalNotes(fact);
        if (text.isEmpty()) text = tr("位置与尺寸调整");
        auto *note = new QLabel(text, card); note->setTextFormat(Qt::PlainText); note->setWordWrap(true);
        note->setTextInteractionFlags(Qt::TextSelectableByMouse); body->addWidget(note);
        noteLayout->addWidget(card);
    }
    noteLayout->addStretch(); noteScroll->setWidget(notes); imageLayout->addWidget(noteScroll, 1);
    sources->addWidget(imagePage, 1);
    modelLabel_ = mutedLabel(connection_.model, sourcePanel_); modelLabel_->setTextFormat(Qt::PlainText);
    modelLabel_->setWordWrap(true); modelLabel_->setToolTip(connection_.baseUrl); sources->addWidget(modelLabel_);

    auto *resultCard = new QWidget(splitter_); resultCard->setObjectName("aiAnalysisResult"); resultCard->setMinimumWidth(360);
    auto *resultLayout = new QVBoxLayout(resultCard); resultLayout->setContentsMargins(24, 22, 24, 20); resultLayout->setSpacing(14);
    auto *resultHeading = new QHBoxLayout;
    auto *resultTitle = new QLabel(tr("修改说明"), resultCard); resultTitle->setProperty("analysisSection", true);
    resultHeading->addWidget(resultTitle); resultHeading->addStretch();
    count_ = mutedLabel(tr("可编辑"), resultCard); count_->setObjectName("aiOutputCount"); resultHeading->addWidget(count_);
    resultLayout->addLayout(resultHeading);
    content_ = new QStackedWidget(resultCard);
    auto *empty = new QWidget(content_); empty->setObjectName("aiInstructionsEmpty");
    auto *emptyLayout = new QVBoxLayout(empty); emptyLayout->setContentsMargins(20, 20, 20, 20); emptyLayout->setSpacing(12); emptyLayout->addStretch();
    emptyIcon_ = new QLabel(empty); emptyIcon_->setAlignment(Qt::AlignCenter); emptyIcon_->setFixedSize(52, 52);
    emptyIcon_->setObjectName("aiAnalysisEmptyIcon"); emptyLayout->addWidget(emptyIcon_, 0, Qt::AlignHCenter);
    emptyTitle_ = new QLabel(empty); emptyTitle_->setAlignment(Qt::AlignCenter); emptyTitle_->setWordWrap(true);
    emptyTitle_->setObjectName("aiAnalysisEmptyTitle"); emptyLayout->addWidget(emptyTitle_);
    emptyHint_ = mutedLabel({}, empty); emptyHint_->setAlignment(Qt::AlignCenter); emptyHint_->setWordWrap(true);
    emptyLayout->addWidget(emptyHint_); emptyLayout->addStretch(); content_->addWidget(empty);
    output_ = new QPlainTextEdit(content_); output_->setObjectName("aiInstructionsText"); output_->setAccessibleName(tr("修改说明"));
    output_->setPlaceholderText(tr("在这里编辑修改说明…")); output_->setTabChangesFocus(true);
    content_->addWidget(output_); resultLayout->addWidget(content_, 1);
    progress_ = new QProgressBar(resultCard); progress_->setRange(0, 0); progress_->setTextVisible(false); progress_->setFixedHeight(3);
    progress_->hide(); resultLayout->addWidget(progress_);
    extra_ = new QLineEdit(resultCard); extra_->setObjectName("aiRunInstructions"); extra_->setMaxLength(10000);
    extra_->setAccessibleName(tr("本次补充要求")); extra_->setPlaceholderText(tr("补充要求，例如：简短一些，按页面区域排列"));
    extra_->setText(draft.extraInstructions.value_or(connection_.extraInstructions)); resultLayout->addWidget(extra_);
    splitter_->addWidget(sourcePanel_); splitter_->addWidget(resultCard); splitter_->setStretchFactor(1, 1);
    layout->addWidget(splitter_, 1);

    auto *footer = new QWidget(this); footer->setObjectName("aiAnalysisFooter");
    auto *footerLayout = new QHBoxLayout(footer); footerLayout->setContentsMargins(24, 16, 24, 16); footerLayout->setSpacing(10);
    auto *messages = new QVBoxLayout; messages->setSpacing(3);
    status_ = new QLabel(tr("正在读取连接配置…"), footer); status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("aiInstructionsStatus"); status_->setWordWrap(true); messages->addWidget(status_);
    usage_ = mutedLabel({}, footer); usage_->setObjectName("aiInstructionsUsage"); usage_->hide(); messages->addWidget(usage_);
    footerLayout->addLayout(messages, 1);
    prompt_ = textButton(tr("系统提示词"), false, footer);
    prompt_->setObjectName("aiInstructionsPrompt");
    prompt_->setToolTip(tr("查看并手动修改发给模型的系统提示词。"));
    footerLayout->addWidget(prompt_);
    save_ = textButton(tr("导出…"), false, footer); save_->setObjectName("aiInstructionsSave"); save_->setProperty("quiet", true);
    cancel_ = textButton(tr("停止解析"), false, footer); cancel_->setObjectName("aiInstructionsCancel");
    generate_ = textButton(tr("开始解析"), true, footer); generate_->setObjectName("aiInstructionsGenerate");
    copy_ = textButton(tr("复制修改说明"), true, footer); copy_->setObjectName("aiInstructionsCopy"); copy_->setIcon(glyph("json-copy-text"));
    for (auto *button : {save_, cancel_, generate_, copy_}) footerLayout->addWidget(button);
    layout->addWidget(footer);

    connect(configure_, &QPushButton::clicked, this, [this] { done(Configure); });
    connect(prompt_, &QPushButton::clicked, this, [this] {
        if (const auto prompt = editAiSystemPrompt(systemPrompt_, this)) {
            systemPrompt_ = *prompt;
            setStatus(tr("系统提示词已更新，下次解析时生效。"));
        }
    });
    connect(generate_, &QPushButton::clicked, this, &AiInstructionsDialog::generate);
    connect(cancel_, &QPushButton::clicked, client_, &AiFeedbackClient::cancel);
    connect(output_, &QPlainTextEdit::textChanged, this, &AiInstructionsDialog::updateOutputActions);
    connect(copy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(output_->toPlainText()); setStatus(tr("已复制修改说明。"));
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("保存修改说明"), "modification-instructions.txt", tr("文本文件 (*.txt);;Markdown (*.md)"));
        if (path.isEmpty()) return;
        QSaveFile file(path); const auto bytes = output_->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            setStatus(tr("保存失败：%1").arg(file.errorString()), true);
        else setStatus(tr("修改说明已保存。"));
    });
    connect(client_, &AiFeedbackClient::completed, this, [this](const QString &text) {
        hasOutput_ = true; output_->setPlainText(text);
        QTextCursor cursor(output_->document()); cursor.select(QTextCursor::Document);
        QTextBlockFormat format; format.setLineHeight(155, QTextBlockFormat::ProportionalHeight); cursor.mergeBlockFormat(format);
        cursor.setPosition(0); output_->setTextCursor(cursor);
        setBusy(false); setStatus(tr("解析完成，可以编辑或直接复制。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this](const QString &message) { setBusy(false); setStatus(message, true); });
    connect(client_, &AiFeedbackClient::cancelled, this, [this] { setBusy(false); setStatus(tr("已停止解析。")); });
    connect(client_, &AiFeedbackClient::usageAvailable, this, [this](qint64 inputTokens, qint64 outputTokens) {
        usage_->setText(tr("输入 %1 · 输出 %2 tokens").arg(inputTokens).arg(outputTokens)); usage_->show();
    });
    connect(credentials_, &AiCredentials::loaded, this, [this, itemCount = facts.size()](const QString &id, const QString &key) {
        if (id != connection_.providerId) return;
        key_ = key; credentialsLoaded_ = true; activeProbeDone_ = true;
        if (!key.isEmpty() && !configuredProviders_.contains(id)) configuredProviders_.append(id);
        setBusy(false);
        setStatus(key.isEmpty() ? tr("先在 API 设置中保存 Key。") : tr("%1 条批注，准备就绪。").arg(itemCount));
        updateProviderButtonIfReady();
    });
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        credentialsLoaded_ = false; activeProbeDone_ = true; setBusy(false); setStatus(message, true);
        updateProviderButtonIfReady();
    });
    hasOutput_ = !draft.text.isEmpty(); output_->setPlainText(draft.text);
    probeConfiguredProviders();
    setBusy(false); updateSourceWidth(); splitter_->setSizes({330, 740}); refreshStyle();
    QTimer::singleShot(0, this, [this] {
        const auto error = validateAiConnection(connection_);
        if (!error.isEmpty()) { setStatus(error, true); return; }
        credentials_->load(connection_.providerId);
    });
}
AiInstructionsDialog::~AiInstructionsDialog() { client_->cancel(); key_.fill(QChar()); key_.clear(); }
AiInstructionsDraft AiInstructionsDialog::draft() const { return {output_->toPlainText(), extra_->text()}; }
void AiInstructionsDialog::applyTo(AppSettings &settings) const {
    settings.aiProviderId = connection_.providerId;
    settings.aiSystemPrompt = systemPrompt_;
}
void AiInstructionsDialog::done(int result) { client_->cancel(); QDialog::done(result); }
void AiInstructionsDialog::generate() {
    if (client_->isRunning() || !credentialsLoaded_ || key_.isEmpty()) return;
    setBusy(true); usage_->clear(); usage_->hide(); setStatus(tr("正在理解画面与布局调整…"));
    auto connection = connection_; connection.extraInstructions = extra_->text();
    client_->generate(connection, key_, input_, systemPrompt_);
}
void AiInstructionsDialog::setStatus(const QString &text, bool error) {
    status_->setText(text); status_->setProperty("error", error);
    status_->style()->unpolish(status_); status_->style()->polish(status_);
}
void AiInstructionsDialog::setBusy(bool busy) {
    busy_ = busy; progress_->setVisible(busy); cancel_->setVisible(busy);
    generate_->setEnabled(!busy && credentialsLoaded_ && !key_.isEmpty());
    configure_->setEnabled(!busy); providerSwitch_->setEnabled(!busy && providerProbeDone_ && activeProbeDone_);
    prompt_->setEnabled(!busy); extra_->setEnabled(!busy); output_->setReadOnly(busy);
    emptyTitle_->setText(busy ? tr("正在整理修改说明") : tr("修改说明，从这里开始"));
    emptyHint_->setText(busy ? tr("结合画面理解每条批注…") : tr("结合画面、批注和拖动调整，生成可直接交给 Agent 的文字需求。"));
    if (!busy && credentialsLoaded_ && key_.isEmpty()) {
        emptyTitle_->setText(tr("连接你的 AI 服务")); emptyHint_->setText(tr("点击右上角服务商，填写 API Key。"));
    }
    updateOutputActions();
}
void AiInstructionsDialog::updateOutputActions() {
    const bool hasText = !output_->toPlainText().trimmed().isEmpty();
    if (hasText) hasOutput_ = true;
    content_->setCurrentIndex(hasOutput_ ? 1 : 0);
    count_->setText(hasText ? tr("%1 字 · 可编辑").arg(output_->toPlainText().size()) : tr("可编辑"));
    copy_->setEnabled(hasText && !busy_); save_->setEnabled(hasText && !busy_);
    copy_->setVisible(hasText); save_->setVisible(hasText); generate_->setVisible(!busy_);
    generate_->setText(hasOutput_ ? tr("重新解析") : tr("开始解析"));
    for (auto *button : {generate_, copy_}) {
        const bool primary = button == copy_ ? hasText : !hasText;
        if (button->property("primary").toBool() != primary) {
            button->setProperty("primary", primary); button->style()->unpolish(button); button->style()->polish(button);
        }
    }
}
QString AiInstructionsDialog::providerDisplayName(const QString &providerId) const {
    for (const auto &preset : aiProviderPresets())
        if (preset.id == providerId) return preset.name;
    return providerId.isEmpty() ? tr("未配置") : providerId;
}
void AiInstructionsDialog::probeConfiguredProviders() {
    // Ask the credential store which providers really have a key, so the header
    // never advertises a built-in provider that was never configured.
    const auto active = connection_.providerId;
    QStringList others;
    for (const auto &preset : aiProviderPresets())
        if (preset.id != active) others.append(preset.id);
    pendingProviderProbes_ = others.size();
    providerProbeDone_ = others.isEmpty();
    activeProbeDone_ = false;
    if (pendingProviderProbes_ > 0) {
        auto *probe = providerProbe_ ? providerProbe_ : new AiCredentials(this);
        const auto step = [this] {
            if (--pendingProviderProbes_ <= 0) {
                pendingProviderProbes_ = 0;
                providerProbeDone_ = true;
                updateProviderButtonIfReady();
            }
        };
        connect(probe, &AiCredentials::loaded, this, [this, step](const QString &id, const QString &key) {
            if (!key.isEmpty() && !configuredProviders_.contains(id)) configuredProviders_.append(id);
            step();
        });
        connect(probe, &AiCredentials::failed, this, [step](const QString &) { step(); });
        for (const auto &id : others) probe->load(id);
    }
    updateProviderButtonIfReady();
}
void AiInstructionsDialog::updateProviderButtonIfReady() {
    if (providerProbeDone_ && activeProbeDone_) updateProviderButton();
}
void AiInstructionsDialog::updateProviderButton() {
    if (!configure_) return;
    if (modelLabel_) modelLabel_->setVisible(!configuredProviders_.isEmpty());
    if (configuredProviders_.isEmpty()) {
        configure_->setText(tr("配置 API…"));
        configure_->setIcon(QIcon());
        configure_->setToolTip(tr("还没有配置任何服务商，点击打开 API 设置。"));
        configure_->setAccessibleName(configure_->toolTip());
        providerSwitch_->hide();
        return;
    }
    // Use a provider that actually has a key, instead of a built-in default.
    if (!configuredProviders_.contains(connection_.providerId)) switchConnection(configuredProviders_.first());
    configure_->setIcon(aiProviderIcon(connection_.providerId));
    configure_->setText(providerDisplayName(connection_.providerId));
    configure_->setToolTip(tr("API 设置") + " · " + connection_.model);
    configure_->setAccessibleName(configure_->toolTip());
    providerSwitch_->setVisible(configuredProviders_.size() > 1);
    providerSwitch_->setEnabled(!busy_ && providerProbeDone_ && activeProbeDone_);
    if (configuredProviders_.size() < 2) {
        return;
    }
    if (!providerMenu_) {
        providerMenu_ = new QMenu(this);
        providerMenu_->setObjectName("aiProviderMenu");
    }
    providerMenu_->clear();
    for (const auto &id : configuredProviders_) {
        auto *action = providerMenu_->addAction(aiProviderIcon(id), providerDisplayName(id));
        action->setCheckable(true);
        action->setChecked(id == connection_.providerId);
        connect(action, &QAction::triggered, this, [this, id] { switchConnection(id); });
    }
    providerMenu_->addSeparator();
    auto *settingsAction = providerMenu_->addAction(tr("API 设置…"));
    connect(settingsAction, &QAction::triggered, this, [this] { done(Configure); });
    providerSwitch_->setMenu(providerMenu_);
}
void AiInstructionsDialog::switchConnection(const QString &providerId) {
    connection_ = settings_.aiProviders.value(providerId, defaultAiConnection(providerId));
    connection_.providerId = providerId;
    key_.clear(); credentialsLoaded_ = false; activeProbeDone_ = false;
    if (modelLabel_) {
        modelLabel_->setText(connection_.model);
        modelLabel_->setToolTip(connection_.baseUrl);
    }
    setBusy(false);
    const auto error = validateAiConnection(connection_);
    if (!error.isEmpty()) { setStatus(error, true); return; }
    setStatus(tr("正在读取连接配置…"));
    credentials_->load(providerId);
}
void AiInstructionsDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) refreshStyle();
}
void AiInstructionsDialog::resizeEvent(QResizeEvent *event) { QDialog::resizeEvent(event); updateSourceWidth(); }
void AiInstructionsDialog::updateSourceWidth() {
    if (sourcePanel_) sourcePanel_->setMaximumWidth(width() < 880 ? 248 : 440);
}
void AiInstructionsDialog::refreshStyle() {
    if (refreshingStyle_) return;
    refreshingStyle_ = true;
    const auto css = aiTextStyleSheet("analysis.qss"); if (styleSheet() != css) setStyleSheet(css);
    if (configure_) configure_->setIcon(aiProviderIcon(connection_.providerId));
    if (emptyIcon_) emptyIcon_->setPixmap(glyph("edit", isDarkTheme() ? QColor("#9bb8df") : QColor("#6788b4")).pixmap(26, 26));
    refreshingStyle_ = false;
}
} // namespace h2d
