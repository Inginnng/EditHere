#include "aisettingspage.h"
#include "aicredentials.h"
#include "aifeedback.h"
#include "ui.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QMenu>
#include <QStyle>
#include <QAction>
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
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);
    title_ = new QLabel(this);
    title_->setObjectName("aiSettingsTitle");
    layout->addWidget(title_);
    description_ = mutedLabel({}, this);
    description_->setWordWrap(true);
    description_->setObjectName("aiDescription");
    layout->addWidget(description_);

    // Keep a single provider-id model for persistence and asynchronous key reads.
    // The visible picker groups regional variants, as in CC Switch's preset bar.
    provider_ = new QComboBox(this);
    provider_->setObjectName("aiProvider");
    for (const auto &preset : aiProviderPresets()) provider_->addItem(preset.name, preset.id);
    provider_->hide();
    providerLabel_ = new QLabel(this);
    providerLabel_->hide();
    providerChoices_ = new QWidget(this);
    providerChoices_->setObjectName("aiProviderChoices");
    auto *choices = new QGridLayout(providerChoices_);
    choices->setContentsMargins(0, 0, 0, 0);
    choices->setSpacing(7);
    const QStringList families{"openai", "claude", "deepseek", "glm", "kimi", "custom"};
    for (int i = 0; i < families.size(); ++i) {
        const auto family = families[i];
        auto *button = textButton({}, false, providerChoices_);
        button->setObjectName("aiProviderChoice_" + family);
        button->setProperty("aiPreset", true);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        familyButtons_[family] = button;
        choices->addWidget(button, i / 3, i % 3);
        connect(button, &QPushButton::clicked, this, [this, family] {
            const auto id = lastFamilyProvider_.value(family, family == "glm" || family == "kimi" ? family + "-cn" : family);
            provider_->setCurrentIndex(provider_->findData(id));
            syncProviderChoices();
        });
    }
    layout->addWidget(providerChoices_);

    auto *summary = new QFrame(this);
    summary->setObjectName("aiProviderSummary");
    auto *summaryRow = new QHBoxLayout(summary);
    summaryRow->setContentsMargins(12, 10, 12, 10);
    summaryRow->setSpacing(10);
    providerMark_ = new QLabel(summary);
    providerMark_->setObjectName("aiProviderMark");
    providerMark_->setAlignment(Qt::AlignCenter);
    providerMark_->setFixedSize(32, 32);
    summaryRow->addWidget(providerMark_);
    auto *identity = new QVBoxLayout;
    identity->setSpacing(2);
    providerName_ = new QLabel(summary);
    providerName_->setObjectName("aiSelectedProviderName");
    providerHost_ = mutedLabel({}, summary);
    providerHost_->setObjectName("aiSelectedProviderHost");
    providerHost_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    identity->addWidget(providerName_);
    identity->addWidget(providerHost_);
    summaryRow->addLayout(identity, 1);
    region_ = new QComboBox(summary);
    region_->setObjectName("aiProviderRegion");
    summaryRow->addWidget(region_);
    docs_ = textButton({}, false, summary);
    docs_->setObjectName("aiProviderDocs");
    docs_->setProperty("aiQuiet", true);
    summaryRow->addWidget(docs_);
    layout->addWidget(summary);
    connect(region_, &QComboBox::currentIndexChanged, this, [this] {
        const auto id = region_->currentData().toString();
        if (!id.isEmpty()) provider_->setCurrentIndex(provider_->findData(id));
    });

    connectionCard_ = new QFrame(this);
    connectionCard_->setObjectName("aiConnectionCard");
    auto *connection = new QVBoxLayout(connectionCard_);
    connection->setSizeConstraint(QLayout::SetMinAndMaxSize);
    connection->setContentsMargins(14, 14, 14, 12);
    connection->setSpacing(10);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    apiKey_ = new QLineEdit(connectionCard_);
    apiKey_->setObjectName("aiApiKey");
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setMaxLength(16384);
    apiKey_->setMinimumWidth(60);
    apiKey_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    auto *keyRow = new QHBoxLayout;
    keyRow->setSpacing(5);
    showKey_ = iconButton("eye", {}, connectionCard_);
    showKey_->setObjectName("aiShowKey");
    showKey_->setCheckable(true);
    showKey_->setFixedSize(30, 34);
    saveKey_ = textButton({}, false, connectionCard_);
    saveKey_->setObjectName("aiSaveKey");
    saveKey_->setProperty("aiCompact", true);
    keyMore_ = iconButton("more", {}, connectionCard_);
    keyMore_->setObjectName("aiKeyActions");
    keyMore_->setFixedSize(28, 34);
    auto *keyMenu = new QMenu(keyMore_);
    deleteAction_ = keyMenu->addAction(QString());
    deleteKey_ = new QPushButton(this);
    deleteKey_->setObjectName("aiDeleteKey");
    deleteKey_->hide();
    connect(deleteAction_, &QAction::triggered, deleteKey_, &QPushButton::click);
    keyMore_->setMenu(keyMenu);
    keyRow->addWidget(apiKey_, 1);
    keyRow->addWidget(showKey_);
    keyRow->addWidget(saveKey_);
    keyRow->addWidget(keyMore_);
    keyLabel_ = new QLabel(connectionCard_);
    keyLabel_->setBuddy(apiKey_);
    form->addRow(keyLabel_, keyRow);
    model_ = new QLineEdit(connectionCard_);
    model_->setObjectName("aiModel");
    model_->setMaxLength(200);
    modelLabel_ = new QLabel(connectionCard_);
    modelLabel_->setBuddy(model_);
    form->addRow(modelLabel_, model_);
    baseUrl_ = new QLineEdit(connectionCard_);
    baseUrl_->setObjectName("aiBaseUrl");
    baseUrl_->setMaxLength(4096);
    baseLabel_ = new QLabel(connectionCard_);
    baseLabel_->setBuddy(baseUrl_);
    form->addRow(baseLabel_, baseUrl_);
    connection->addLayout(form);
    keyHint_ = mutedLabel({}, connectionCard_);
    keyHint_->setWordWrap(true);
    keyHint_->setObjectName("aiKeyStorageHint");
    connection->addWidget(keyHint_);
    auto *testRow = new QHBoxLayout;
    testRow->setSpacing(12);
    status_ = new QLabel(connectionCard_);
    status_->setObjectName("aiConnectionStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setProperty("muted", true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(status_, 1);
    test_ = textButton({}, true, connectionCard_);
    test_->setObjectName("aiTestConnection");
    test_->setProperty("aiCompact", true);
    testRow->addWidget(test_);
    connection->addLayout(testRow);
    layout->addWidget(connectionCard_);
    // Platform-specific account/quota details are available on the selected
    // provider, without turning the main form into a wall of warnings.
    note_ = mutedLabel({}, this);
    note_->setObjectName("aiProviderNote");
    note_->hide();

    auto *advancedHeader = new QHBoxLayout;
    advancedButton_ = textButton({}, false, this);
    advancedButton_->setObjectName("aiAdvancedToggle");
    advancedButton_->setProperty("aiQuiet", true);
    advancedButton_->setCheckable(true);
    advancedHeader->addWidget(advancedButton_);
    advancedHeader->addStretch();
    prompt_ = textButton({}, false, this);
    prompt_->setObjectName("aiViewPrompt");
    prompt_->setProperty("aiQuiet", true);
    advancedHeader->addWidget(prompt_);
    layout->addLayout(advancedHeader);
    advanced_ = new QFrame(this);
    advanced_->setObjectName("aiAdvancedFields");
    auto *advancedForm = new QFormLayout(advanced_);
    advancedForm->setSizeConstraint(QLayout::SetMinAndMaxSize);
    advancedForm->setContentsMargins(14, 14, 14, 14);
    advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    advancedForm->setHorizontalSpacing(12);
    advancedForm->setVerticalSpacing(10);
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
    extra_->setMinimumHeight(76);
    extra_->setMaximumHeight(96);
    extraLabel_ = new QLabel(advanced_);
    extraLabel_->setBuddy(extra_);
    advancedForm->addRow(extraLabel_, extra_);
    reset_ = textButton({}, false, advanced_);
    reset_->setObjectName("aiResetPreset");
    reset_->setProperty("aiQuiet", true);
    advancedForm->addRow(reset_);
    layout->addWidget(advanced_);
    advanced_->hide();
    layout->addStretch();
    connect(baseUrl_, &QLineEdit::textChanged, this, [this] {
        providerHost_->setText(QUrl(baseUrl_->text()).host());
        providerHost_->setToolTip(baseUrl_->text());
    });

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
        providerChoices_->setEnabled(false);
        region_->setEnabled(false);
        advanced_->setEnabled(false);
        baseUrl_->setEnabled(false);
        model_->setEnabled(false);
        saveKey_->setEnabled(false);
        deleteKey_->setEnabled(false);
        keyMore_->setEnabled(false);
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
    updateAppearance();
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
    syncProviderChoices();
    auto config = connections_.value(currentProvider_, defaultAiConnection(currentProvider_));
    baseUrl_->setText(config.baseUrl);
    model_->setText(config.model);
    protocol_->setCurrentIndex(protocol_->findData(config.protocol));
    timeout_->setValue(config.timeoutSeconds);
    extra_->setPlainText(config.extraInstructions);
    apiKey_->setText(keysByProvider_.value(currentProvider_));
    showKey_->setChecked(false);
    setStatus({});
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
        providerMark_->setToolTip(preset.note);
        providerName_->setToolTip(preset.note);
        docs_->setEnabled(!preset.docsUrl.isEmpty());
        break;
    }
}
void AiSettingsPage::setStatus(const QString &message, bool error) {
    status_->setProperty("error", error);
    status_->setText(message);
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}
void AiSettingsPage::credentialBusy(bool busy) {
    credentialsBusy_ = busy;
    const bool editable = !busy && !client_->isRunning();
    provider_->setEnabled(editable);
    providerChoices_->setEnabled(editable);
    region_->setEnabled(editable);
    apiKey_->setEnabled(editable);
    saveKey_->setEnabled(editable);
    deleteKey_->setEnabled(editable);
    keyMore_->setEnabled(editable);
    deleteAction_->setEnabled(editable);
    showKey_->setEnabled(editable);
    baseUrl_->setEnabled(!client_->isRunning());
    model_->setEnabled(!client_->isRunning());
    advanced_->setEnabled(!client_->isRunning());
    test_->setEnabled(!busy);
}
void AiSettingsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateAppearance();
    loadKey();
}
void AiSettingsPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        updateAppearance();
}
void AiSettingsPage::syncProviderChoices() {
    QString family = currentProvider_;
    if (family.startsWith("glm-")) family = "glm";
    if (family.startsWith("kimi-")) family = "kimi";
    if (!familyButtons_.contains(family)) family = "custom";
    if (!currentProvider_.isEmpty()) lastFamilyProvider_[family] = currentProvider_;
    for (auto it = familyButtons_.cbegin(); it != familyButtons_.cend(); ++it) {
        const QSignalBlocker blocker(it.value());
        it.value()->setChecked(it.key() == family);
    }
    const QSignalBlocker blocker(region_);
    region_->clear();
    if (family == "glm" || family == "kimi") {
        region_->addItem(tr("中国区"), family + "-cn");
        region_->addItem(tr("国际区"), family + "-global");
        region_->setCurrentIndex(region_->findData(currentProvider_));
    }
    region_->setVisible(region_->count() > 0);
    region_->setAccessibleName(tr("服务地区"));
    const QMap<QString, QString> names{{"openai", "OpenAI"}, {"claude", "Claude"}, {"deepseek", "DeepSeek"},
        {"glm", "GLM"}, {"kimi", "Kimi"}, {"custom", tr("自定义服务")}};
    const QMap<QString, QString> marks{{"openai", "O"}, {"claude", "C"}, {"deepseek", "D"},
        {"glm", "G"}, {"kimi", "K"}, {"custom", "+"}};
    providerName_->setText(names.value(family));
    providerMark_->setText(marks.value(family));
    providerHost_->setText(QUrl(baseUrl_->text()).host());
}
void AiSettingsPage::updateAppearance() {
    if (updatingAppearance_) return;
    updatingAppearance_ = true;
    const bool dark = isDarkTheme();
    const QString surface = dark ? "#282a30" : "#ffffff";
    const QString subtle = dark ? "#303239" : "#f1f2f6";
    const QString border = dark ? "#40424c" : "#e1e2e9";
    const QString muted = dark ? "#a0a2ae" : "#777985";
    const QString selected = dark ? "#263a59" : "#edf4ff";
    const QString accentColor = accent().name();
    setStyleSheet(QString(R"(
QLabel#aiSettingsTitle { font-size:19px; font-weight:600; }
QLabel#aiSelectedProviderName { font-weight:600; }
QLabel#aiProviderMark { background:%1; color:%5; border-radius:9px; font-size:17px; font-weight:600; }
QFrame#aiProviderSummary { background:%2; border-radius:10px; }
QFrame#aiConnectionCard, QFrame#aiAdvancedFields { background:%1; border:1px solid %3; border-radius:12px; }
QPushButton[aiPreset="true"] { background:%1; border:1px solid %3; border-radius:8px; padding:7px 10px; min-height:20px; }
QPushButton[aiPreset="true"]:hover { background:%2; }
QPushButton[aiPreset="true"]:checked { background:%6; border:1px solid %5; color:%5; }
QPushButton[aiQuiet="true"] { background:transparent; border:0; color:%4; padding:6px 8px; min-height:20px; }
QPushButton[aiQuiet="true"]:hover { background:%2; color:%5; }
QPushButton[aiCompact="true"] { padding:6px 10px; min-height:20px; }
QPushButton#aiShowKey, QPushButton#aiKeyActions { padding:0; min-width:0; min-height:0; }
QPushButton#aiKeyActions::menu-indicator { image:none; width:0; }
QLabel#aiConnectionStatus[error="true"] { color:%7; }
)").arg(surface, subtle, border, muted, accentColor, selected, dark ? "#ff8a8a" : "#c63636"));
    showKey_->setIcon(glyph(showKey_->isChecked() ? "eye-off" : "eye"));
    keyMore_->setIcon(glyph("more"));
    updatingAppearance_ = false;
}
void AiSettingsPage::retranslate() {
    for (const auto &preset : aiProviderPresets()) {
        const int index = provider_->findData(preset.id);
        if (index >= 0) provider_->setItemText(index, preset.name);
    }
    const QMap<QString, QString> labels{{"openai", "OpenAI"}, {"claude", "Claude"}, {"deepseek", "DeepSeek"},
        {"glm", "GLM"}, {"kimi", "Kimi"}, {"custom", tr("自定义")}};
    for (auto it = familyButtons_.cbegin(); it != familyButtons_.cend(); ++it) it.value()->setText(labels.value(it.key()));
    syncProviderChoices();
    updateNote();
    title_->setText(tr("AI 连接"));
    description_->setText(tr("为「AI解析」选择服务商，填入 API Key 即可开始。"));
    providerLabel_->setText(tr("提供商"));
    provider_->setAccessibleName(providerLabel_->text());
    keyLabel_->setText("API Key");
    apiKey_->setAccessibleName("API Key");
    apiKey_->setPlaceholderText(tr("输入此提供商的 API Key"));
    showKey_->setIcon(glyph(showKey_->isChecked() ? "eye-off" : "eye"));
    showKey_->setToolTip(showKey_->isChecked() ? tr("隐藏密钥") : tr("显示密钥"));
    showKey_->setAccessibleName(showKey_->toolTip());
    saveKey_->setText(tr("保存密钥"));
    saveKey_->setToolTip(tr("立即保存到系统凭据库；取消设置不会撤销此操作。"));
    deleteKey_->setText(tr("删除已存密钥"));
    deleteAction_->setText(deleteKey_->text());
    keyMore_->setToolTip(tr("密钥操作"));
    keyMore_->setAccessibleName(keyMore_->toolTip());
    test_->setText(client_->isRunning() ? tr("取消测试") : tr("测试连接"));
    keyHint_->setText(tr("密钥保存到系统凭据库，其他配置随底部“保存”生效。"));
    keyHint_->setToolTip(tr("截图仅在你点击解析时发送给所选提供商，API 调用可能计费。"));
    test_->setToolTip(tr("发送一张内置小图验证接口和识图能力，不发送当前截图或批注。"));
    advancedButton_->setText(advancedButton_->isChecked() ? tr("收起高级选项") : tr("高级选项"));
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
    reset_->setText(tr("恢复预设"));
    reset_->setToolTip(tr("恢复此提供商的地址、模型、协议和补充要求，保留密钥。"));
    prompt_->setText(tr("内置提示词"));
    docs_->setText(tr("官方文档"));
}
} // namespace h2d
