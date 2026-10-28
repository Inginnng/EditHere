#include "aisettingspage.h"
#include "aicredentials.h"
#include "aifeedback.h"
#include "ui.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace h2d {
AiSettingsPage::AiSettingsPage(QWidget *parent, AiCredentials *credentials) : QWidget(parent),
    credentials_(credentials ? credentials : new AiCredentials(this)), client_(new AiFeedbackClient(this)) {
    setObjectName("aiSettingsContent");
    auto *layout = new QVBoxLayout(this);
    // This page lives in a widgetResizable QScrollArea. Recompute its minimum
    // content size when advanced rows appear, so the scroll area grows the page
    // instead of compressing nested forms into the old viewport height.
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    description_ = mutedLabel({}, this);
    description_->setWordWrap(true);
    description_->setObjectName("aiDescription");
    layout->addWidget(description_);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    provider_ = new QComboBox(this);
    provider_->setObjectName("aiProvider");
    for (const auto &preset : aiProviderPresets()) provider_->addItem(preset.name, preset.id);
    providerLabel_ = new QLabel(this);
    providerLabel_->setBuddy(provider_);
    form->addRow(providerLabel_, provider_);
    auto *keyRow = new QHBoxLayout;
    apiKey_ = new QLineEdit(this);
    apiKey_->setObjectName("aiApiKey");
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setMaxLength(16384);
    apiKey_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    showKey_ = textButton({}, false, this);
    showKey_->setObjectName("aiShowKey");
    showKey_->setCheckable(true);
    keyRow->addWidget(apiKey_, 1);
    keyRow->addWidget(showKey_);
    keyLabel_ = new QLabel(this);
    keyLabel_->setBuddy(apiKey_);
    form->addRow(keyLabel_, keyRow);
    layout->addLayout(form);
    auto *keyActions = new QHBoxLayout;
    saveKey_ = textButton({}, false, this);
    saveKey_->setObjectName("aiSaveKey");
    deleteKey_ = textButton({}, false, this);
    deleteKey_->setObjectName("aiDeleteKey");
    test_ = textButton({}, true, this);
    test_->setObjectName("aiTestConnection");
    keyActions->addWidget(saveKey_);
    keyActions->addWidget(deleteKey_);
    keyActions->addStretch();
    keyActions->addWidget(test_);
    layout->addLayout(keyActions);
    keyHint_ = mutedLabel({}, this);
    keyHint_->setWordWrap(true);
    keyHint_->setObjectName("aiKeyStorageHint");
    layout->addWidget(keyHint_);
    note_ = mutedLabel({}, this);
    note_->setWordWrap(true);
    note_->setTextFormat(Qt::PlainText);
    note_->setObjectName("aiProviderNote");
    layout->addWidget(note_);
    auto *links = new QHBoxLayout;
    docs_ = textButton({}, false, this);
    docs_->setObjectName("aiProviderDocs");
    prompt_ = textButton({}, false, this);
    prompt_->setObjectName("aiViewPrompt");
    links->addWidget(docs_);
    links->addWidget(prompt_);
    links->addStretch();
    layout->addLayout(links);
    status_ = new QLabel(this);
    status_->setObjectName("aiConnectionStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    advancedButton_ = textButton({}, false, this);
    advancedButton_->setObjectName("aiAdvancedToggle");
    advancedButton_->setCheckable(true);
    layout->addWidget(advancedButton_, 0, Qt::AlignLeft);
    advanced_ = new QWidget(this);
    advanced_->setObjectName("aiAdvancedFields");
    auto *advancedForm = new QFormLayout(advanced_);
    advancedForm->setSizeConstraint(QLayout::SetMinAndMaxSize);
    advancedForm->setContentsMargins(0, 0, 0, 0);
    advancedForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    baseUrl_ = new QLineEdit(advanced_);
    baseUrl_->setObjectName("aiBaseUrl");
    baseUrl_->setMaxLength(4096);
    baseLabel_ = new QLabel(advanced_);
    baseLabel_->setBuddy(baseUrl_);
    advancedForm->addRow(baseLabel_, baseUrl_);
    model_ = new QLineEdit(advanced_);
    model_->setObjectName("aiModel");
    model_->setMaxLength(200);
    modelLabel_ = new QLabel(advanced_);
    modelLabel_->setBuddy(model_);
    advancedForm->addRow(modelLabel_, model_);
    protocol_ = new QComboBox(advanced_);
    protocol_->setObjectName("aiProtocol");
    protocol_->addItem("OpenAI Responses", "responses");
    protocol_->addItem("OpenAI Chat Completions", "chat-completions");
    protocol_->addItem("Anthropic Messages", "anthropic-messages");
    protocolLabel_ = new QLabel(advanced_);
    protocolLabel_->setBuddy(protocol_);
    advancedForm->addRow(protocolLabel_, protocol_);
    timeout_ = new QSpinBox(advanced_);
    timeout_->setObjectName("aiTimeout");
    timeout_->setRange(10, 600);
    timeoutLabel_ = new QLabel(advanced_);
    timeoutLabel_->setBuddy(timeout_);
    advancedForm->addRow(timeoutLabel_, timeout_);
    extra_ = new QPlainTextEdit(advanced_);
    extra_->setObjectName("aiExtraInstructions");
    extra_->setMaximumHeight(100);
    extraLabel_ = new QLabel(advanced_);
    extraLabel_->setBuddy(extra_);
    advancedForm->addRow(extraLabel_, extra_);
    reset_ = textButton({}, false, advanced_);
    reset_->setObjectName("aiResetPreset");
    advancedForm->addRow(reset_);
    layout->addWidget(advanced_);
    advanced_->hide();
    layout->addStretch();

    connect(showKey_, &QPushButton::toggled, this, [this](bool checked) {
        apiKey_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        retranslate();
    });
    connect(apiKey_, &QLineEdit::textEdited, this, [this] {
        keysByProvider_[currentProvider_] = apiKey_->text();
        loadedKeys_.insert(currentProvider_);
        dirtyKeys_.insert(currentProvider_);
        setStatus(tr("密钥已修改，点击“保存密钥”后持久保存。"));
    });
    connect(provider_, &QComboBox::currentIndexChanged, this, &AiSettingsPage::switchProvider);
    connect(advancedButton_, &QPushButton::toggled, this, [this](bool expanded) {
        advanced_->setVisible(expanded);
        retranslate();
    });
    connect(reset_, &QPushButton::clicked, this, [this] {
        const auto config = defaultAiConnection(currentProvider_);
        baseUrl_->setText(config.baseUrl);
        model_->setText(config.model);
        protocol_->setCurrentIndex(protocol_->findData(config.protocol));
        timeout_->setValue(config.timeoutSeconds);
        extra_->setPlainText(config.extraInstructions);
        connections_.remove(currentProvider_);
        setStatus(tr("已恢复此提供商的默认地址、模型和提示词。密钥保持不变。"));
    });
    connect(saveKey_, &QPushButton::clicked, this, [this] {
        if (apiKey_->text().trimmed().isEmpty()) { setStatus(tr("请先输入 API Key。"), true); return; }
        keysByProvider_[currentProvider_] = apiKey_->text().trimmed();
        credentialBusy(true);
        setStatus(tr("正在保存密钥…"));
        credentials_->save(currentProvider_, apiKey_->text().trimmed());
    });
    connect(deleteKey_, &QPushButton::clicked, this, [this] {
        credentialBusy(true);
        setStatus(tr("正在删除密钥…"));
        credentials_->remove(currentProvider_);
    });
    connect(test_, &QPushButton::clicked, this, [this] {
        if (client_->isRunning()) { client_->cancel(); return; }
        const auto config = currentConnection();
        const auto error = validateAiConnection(config);
        if (!error.isEmpty()) { setStatus(error, true); return; }
        if (apiKey_->text().trimmed().isEmpty()) { setStatus(tr("请先输入 API Key。"), true); return; }
        setStatus(tr("正在测试图文接口；只发送内置测试图片，不发送当前批注。"));
        provider_->setEnabled(false);
        advanced_->setEnabled(false);
        saveKey_->setEnabled(false);
        deleteKey_->setEnabled(false);
        apiKey_->setEnabled(false);
        test_->setText(tr("取消测试"));
        client_->testConnection(config, apiKey_->text().trimmed());
    });
    auto finishTest = [this] {
        credentialBusy(credentialsBusy_);
        retranslate();
        if (isVisible()) loadKey();
    };
    connect(client_, &AiFeedbackClient::completed, this, [this, finishTest](const QString &) {
        finishTest();
        setStatus(tr("连接成功，模型已通过图文接口测试。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this, finishTest](const QString &message) {
        finishTest();
        setStatus(message, true);
    });
    connect(credentials_, &AiCredentials::loaded, this, [this](const QString &id, const QString &key) {
        const bool accept = !loadedKeys_.contains(id) && !dirtyKeys_.contains(id);
        if (accept) keysByProvider_[id] = key;
        loadedKeys_.insert(id);
        if (id == currentProvider_ && accept) {
            apiKey_->setText(key);
            setStatus(key.isEmpty() ? tr("此提供商尚未保存 API Key。") : tr("已从系统凭据库读取 API Key。"));
        }
        if (loadingProvider_ == id) {
            loadingProvider_.clear();
            credentialBusy(false);
            if (id != currentProvider_ && isVisible()) loadKey();
        }
    });
    connect(credentials_, &AiCredentials::saved, this, [this](const QString &id) {
        dirtyKeys_.remove(id);
        loadedKeys_.insert(id);
        credentialBusy(false);
        setStatus(tr("API Key 已保存到系统凭据库。"));
        if (id != currentProvider_ && isVisible()) loadKey();
    });
    connect(credentials_, &AiCredentials::removed, this, [this](const QString &id) {
        dirtyKeys_.remove(id);
        loadedKeys_.insert(id);
        keysByProvider_[id].clear();
        if (id == currentProvider_) apiKey_->clear();
        credentialBusy(false);
        setStatus(tr("已删除此提供商的 API Key。"));
        if (id != currentProvider_ && isVisible()) loadKey();
    });
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        const auto failedLoad = loadingProvider_;
        loadingProvider_.clear();
        credentialBusy(false);
        setStatus(message, true);
        if (isVisible() && (failedLoad.isEmpty() || failedLoad != currentProvider_)) loadKey();
    });
    connect(prompt_, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setObjectName("aiPromptDialog");
        dialog.setWindowTitle(tr("EditHere 内置提示词"));
        dialog.resize(620, 480);
        auto *body = new QVBoxLayout(&dialog);
        auto *text = new QPlainTextEdit(&dialog);
        text->setObjectName("aiBuiltinPrompt");
        text->setReadOnly(true);
        text->setPlainText(aiSystemPrompt());
        body->addWidget(text);
        auto *close = textButton(tr("关闭"), true, &dialog);
        body->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    });
    connect(docs_, &QPushButton::clicked, this, [this] {
        for (const auto &preset : aiProviderPresets())
            if (preset.id == currentProvider_ && QUrl(preset.docsUrl).scheme() == "https")
                QDesktopServices::openUrl(QUrl(preset.docsUrl));
    });
    retranslate();
}
AiConnectionSettings AiSettingsPage::currentConnection() const {
    auto config = defaultAiConnection(currentProvider_);
    config.providerId = currentProvider_;
    config.baseUrl = baseUrl_->text().trimmed();
    config.model = model_->text().trimmed();
    config.protocol = protocol_->currentData().toString();
    config.timeoutSeconds = timeout_->value();
    config.extraInstructions = extra_->toPlainText();
    return config;
}
void AiSettingsPage::rememberConnection() {
    if (currentProvider_.isEmpty()) return;
    const auto config = currentConnection();
    if (config == defaultAiConnection(currentProvider_)) connections_.remove(currentProvider_);
    else connections_[currentProvider_] = config;
    if (dirtyKeys_.contains(currentProvider_)) keysByProvider_[currentProvider_] = apiKey_->text();
}
void AiSettingsPage::setSettings(const AppSettings &settings) {
    client_->cancel();
    connections_ = settings.aiProviders;
    currentProvider_.clear();
    const QSignalBlocker blocker(provider_);
    int index = provider_->findData(settings.aiProviderId);
    if (index < 0) {
        provider_->addItem(settings.aiProviderId, settings.aiProviderId);
        index = provider_->count() - 1;
    }
    provider_->setCurrentIndex(index);
    switchProvider();
}
void AiSettingsPage::applyTo(AppSettings &settings) const {
    settings.aiProviderId = currentProvider_;
    settings.aiProviders = connections_;
    const auto config = currentConnection();
    if (config == defaultAiConnection(currentProvider_)) settings.aiProviders.remove(currentProvider_);
    else settings.aiProviders[currentProvider_] = config;
}
void AiSettingsPage::switchProvider() {
    rememberConnection();
    currentProvider_ = provider_->currentData().toString();
    auto config = connections_.value(currentProvider_, defaultAiConnection(currentProvider_));
    baseUrl_->setText(config.baseUrl);
    model_->setText(config.model);
    protocol_->setCurrentIndex(protocol_->findData(config.protocol));
    timeout_->setValue(config.timeoutSeconds);
    extra_->setPlainText(config.extraInstructions);
    apiKey_->setText(keysByProvider_.value(currentProvider_));
    showKey_->setChecked(false);
    status_->clear();
    updateNote();
    if (currentProvider_ == "custom") advancedButton_->setChecked(true);
    if (isVisible()) loadKey();
}
void AiSettingsPage::loadKey() {
    if (currentProvider_.isEmpty() || loadedKeys_.contains(currentProvider_) ||
        dirtyKeys_.contains(currentProvider_) || !loadingProvider_.isEmpty() ||
        credentialsBusy_ || client_->isRunning()) return;
    loadingProvider_ = currentProvider_;
    // Reads can open an OS keyring prompt. Serialize them with explicit writes
    // and connection tests, so a delayed read cannot replace a newly saved key.
    credentialBusy(true);
    credentials_->load(currentProvider_);
}
void AiSettingsPage::updateNote() {
    note_->clear();
    docs_->setEnabled(false);
    for (const auto &preset : aiProviderPresets()) {
        if (preset.id != currentProvider_) continue;
        note_->setText(preset.note);
        docs_->setEnabled(!preset.docsUrl.isEmpty());
        break;
    }
}
void AiSettingsPage::setStatus(const QString &message, bool error) {
    status_->setProperty("error", error);
    status_->setText(message);
}
void AiSettingsPage::credentialBusy(bool busy) {
    credentialsBusy_ = busy;
    const bool editable = !busy && !client_->isRunning();
    provider_->setEnabled(editable);
    apiKey_->setEnabled(editable);
    saveKey_->setEnabled(editable);
    deleteKey_->setEnabled(editable);
    advanced_->setEnabled(!client_->isRunning());
    test_->setEnabled(!busy);
}
void AiSettingsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    loadKey();
}
void AiSettingsPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
void AiSettingsPage::retranslate() {
    for (const auto &preset : aiProviderPresets()) {
        const int index = provider_->findData(preset.id);
        if (index >= 0) provider_->setItemText(index, preset.name);
    }
    updateNote();
    description_->setText(tr("将批注和截图交给所选模型，生成可直接复制的修改意见。选择提供商、填写 API Key 即可使用。"));
    providerLabel_->setText(tr("提供商"));
    provider_->setAccessibleName(providerLabel_->text());
    keyLabel_->setText("API Key");
    apiKey_->setAccessibleName("API Key");
    apiKey_->setPlaceholderText(tr("输入此提供商的 API Key"));
    showKey_->setText(showKey_->isChecked() ? tr("隐藏") : tr("显示"));
    saveKey_->setText(tr("保存密钥"));
    deleteKey_->setText(tr("删除密钥"));
    test_->setText(client_->isRunning() ? tr("取消测试") : tr("测试连接"));
    keyHint_->setText(tr("“保存密钥”和“删除密钥”立即更新系统凭据库；底部“保存”保存其他配置。截图仅在你生成意见时发送给所选提供商，API 调用可能计费。"));
    test_->setToolTip(tr("发送一张内置小图验证接口和识图能力，不发送当前截图或批注。"));
    advancedButton_->setText(advancedButton_->isChecked() ? tr("收起高级配置") : tr("高级配置"));
    baseLabel_->setText(tr("API 地址"));
    baseUrl_->setAccessibleName(baseLabel_->text());
    baseUrl_->setPlaceholderText("https://api.example.com/v1");
    modelLabel_->setText(tr("模型"));
    model_->setAccessibleName(modelLabel_->text());
    model_->setPlaceholderText(tr("支持图片输入的模型名称"));
    protocolLabel_->setText(tr("接口协议"));
    protocol_->setAccessibleName(protocolLabel_->text());
    timeoutLabel_->setText(tr("超时时间"));
    timeout_->setSuffix(tr(" 秒"));
    timeout_->setAccessibleName(timeoutLabel_->text());
    extraLabel_->setText(tr("补充要求"));
    extra_->setAccessibleName(extraLabel_->text());
    extra_->setPlaceholderText(tr("可选，例如：使用中文；按页面分组。内置精度规则始终保留。"));
    reset_->setText(tr("恢复此提供商默认配置"));
    prompt_->setText(tr("查看内置提示词"));
    docs_->setText(tr("官方 API 文档"));
}
} // namespace h2d
