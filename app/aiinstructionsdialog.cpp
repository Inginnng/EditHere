#include "aiinstructionsdialog.h"
#include "aicredentials.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace h2d {
AiInstructionsDialog::AiInstructionsDialog(const AppSettings &settings, AiFeedbackInput input,
                                         QWidget *parent, AiCredentials *credentials)
    : QDialog(parent), input_(std::move(input)),
      credentials_(credentials ? credentials : new AiCredentials(this)), client_(new AiFeedbackClient(this)) {
    setObjectName("aiInstructionsDialog");
    setWindowTitle(tr("生成修改意见"));
    resize(780, 680); setMinimumSize(520, 450);
    connection_ = settings.aiProviders.value(settings.aiProviderId, defaultAiConnection(settings.aiProviderId));
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22); layout->setSpacing(12);
    auto title = new QLabel(tr("把批注整理为可执行的修改意见"), this);
    auto font = title->font(); font.setPointSize(font.pointSize() + 4); font.setBold(true); title->setFont(font);
    layout->addWidget(title);
    QString providerName = settings.aiProviderId;
    for (const auto &preset : aiProviderPresets()) if (preset.id == settings.aiProviderId) providerName = preset.name;
    auto summary = new QLabel(tr("%1 · %2\n将本次 %3 张截图和批注发送至 %4。复制和保存的结果仅含文字。")
                                 .arg(providerName, connection_.model).arg(input_.images.size())
                                 .arg(QUrl(connection_.baseUrl).host()), this);
    summary->setObjectName("aiInstructionsSummary"); summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true); layout->addWidget(summary);
    auto snapshot = mutedLabel(tr("使用打开此窗口时的批注快照。原始图片和批注保留在当前项目中。"), this);
    snapshot->setWordWrap(true); layout->addWidget(snapshot);
    output_ = new QPlainTextEdit(this); output_->setObjectName("aiInstructionsText");
    output_->setAccessibleName(tr("修改意见"));
    output_->setPlaceholderText(tr("生成后可在这里核对、编辑，再复制给 Agent。"));
    layout->addWidget(output_, 1);
    progress_ = new QProgressBar(this); progress_->setRange(0, 0); progress_->setTextVisible(false);
    progress_->hide(); layout->addWidget(progress_);
    status_ = new QLabel(tr("正在读取 API Key…"), this); status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("aiInstructionsStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    usage_ = mutedLabel({}, this); usage_->setObjectName("aiInstructionsUsage"); layout->addWidget(usage_);
    auto row = new QHBoxLayout;
    configure_ = textButton(tr("API 设置…"), false, this); configure_->setObjectName("aiInstructionsSettings");
    generate_ = textButton(tr("生成"), true, this); generate_->setObjectName("aiInstructionsGenerate");
    cancel_ = textButton(tr("停止"), false, this); cancel_->setObjectName("aiInstructionsCancel");
    copy_ = textButton(tr("复制文字"), false, this); copy_->setObjectName("aiInstructionsCopy");
    save_ = textButton(tr("保存文字…"), false, this); save_->setObjectName("aiInstructionsSave");
    auto close = textButton(tr("关闭"), false, this); close->setObjectName("aiInstructionsClose");
    row->addWidget(configure_); row->addStretch(); row->addWidget(close);
    row->addWidget(save_); row->addWidget(copy_); row->addWidget(cancel_); row->addWidget(generate_);
    layout->addLayout(row);
    connect(configure_, &QPushButton::clicked, this, [this] { done(Configure); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
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
        status_->setText(tr("已生成。请核对对象、数值和“待核对”项后使用。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this](const QString &message) {
        setBusy(false); status_->setText(message);
    });
    connect(client_, &AiFeedbackClient::usageAvailable, this, [this](qint64 input, qint64 output) {
        usage_->setText(tr("本次 API 用量：输入 %1 tokens，输出 %2 tokens（以提供商返回为准）").arg(input).arg(output));
    });
    connect(credentials_, &AiCredentials::loaded, this, [this](const QString &id, const QString &key) {
        if (id != connection_.providerId) return;
        key_ = key; credentialsLoaded_ = true; setBusy(false);
        status_->setText(key.isEmpty() ? tr("请先在 API 设置中填写并保存此提供商的 Key。")
                                      : tr("准备就绪。点击生成后开始本次请求。"));
    });
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        status_->setText(message); credentialsLoaded_ = false; setBusy(false);
    });
    setBusy(false);
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
    updateOutputActions();
}
void AiInstructionsDialog::updateOutputActions() {
    const bool available = !output_->toPlainText().trimmed().isEmpty() && !busy_;
    copy_->setEnabled(available); save_->setEnabled(available);
}
} // namespace h2d
