#include "aisettingspage.h"
#include "aicredentials.h"
#include "aifeedback.h"
#include "aiprovidericons.h"
#include "ui.h"
#include "aistyle.h"
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace h2d {
namespace {
// Keep the standard combo's interaction and accessibility, while drawing a
// theme-aware vector arrow instead of Fusion's partially styled subcontrol.
class SettingsComboBox final : public QComboBox {
  public:
    using QComboBox::QComboBox;
  protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        const auto arrow = QStyle::visualRect(layoutDirection(), rect(),
                                              QRect(width() - 25, (height() - 16) / 2, 16, 16));
        glyph("chevron-down").paint(&painter, arrow, Qt::AlignCenter,
                                    isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
};
QString providerFamily(const QString &provider) {
    if (provider.startsWith("glm-")) return "glm";
    if (provider.startsWith("kimi-")) return "kimi";
    return provider;
}
QIcon disclosureIcon(bool expanded) {
    const auto down = glyph("chevron-down");
    return expanded ? QIcon(down.pixmap(16, 16).transformed(QTransform().rotate(180))) : down;
}
}
AiSettingsPage::AiSettingsPage(QWidget *parent, AiCredentials *credentials) : QWidget(parent),
    credentials_(credentials ? credentials : new AiCredentials(this)), client_(new AiFeedbackClient(this)) {
    setObjectName("aiSettingsContent");
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    layout->setContentsMargins(36, 30, 36, 28);
    layout->setSpacing(0);
    title_ = new QLabel(this);
    title_->setObjectName("aiSettingsTitle");
    layout->addWidget(title_);
    layout->addSpacing(8);
    description_ = mutedLabel({}, this);
    description_->setWordWrap(true);
    description_->setObjectName("aiDescription");
    layout->addWidget(description_);
    layout->addSpacing(28);

    // The hidden model keeps regional platform IDs distinct for persistence and
    // asynchronous credentials. The visible family picker only groups the UI.
    provider_ = new QComboBox(this);
    provider_->setObjectName("aiProvider");
    for (const auto &preset : aiProviderPresets()) provider_->addItem(preset.name, preset.id);
    provider_->hide();
    auto *providerHeading = new QHBoxLayout;
    providerHeading->setContentsMargins(0, 0, 0, 0);
    providerLabel_ = new QLabel(this);
    providerLabel_->setProperty("aiFieldLabel", true);
    providerHeading->addWidget(providerLabel_);
    providerHeading->addStretch();
    docs_ = textButton({}, false, this);
    docs_->setObjectName("aiProviderDocs");
    docs_->setProperty("aiQuiet", true);
    providerHeading->addWidget(docs_);
    layout->addLayout(providerHeading);
    layout->addSpacing(6);
    auto *providerRow = new QHBoxLayout;
    providerRow->setSpacing(10);
    family_ = new SettingsComboBox(this);
    family_->setObjectName("aiProviderFamily");
    family_->setIconSize({24, 24});
    family_->setMinimumWidth(120);
    family_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const auto &family : {"openai", "claude", "deepseek", "glm", "kimi", "custom"})
        family_->addItem(aiProviderIcon(family), {}, family);
    providerLabel_->setBuddy(family_);
    providerRow->addWidget(family_, 1);
    region_ = new SettingsComboBox(this);
    region_->setObjectName("aiProviderRegion");
    region_->setMinimumContentsLength(6);
    region_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    providerRow->addWidget(region_);
    layout->addLayout(providerRow);
    layout->addSpacing(20);
    connect(family_, &QComboBox::currentIndexChanged, this, [this] {
        const auto family = family_->currentData().toString();
        const auto fallback = family == "glm" || family == "kimi" ? family + "-cn" : family;
        provider_->setCurrentIndex(provider_->findData(lastFamilyProvider_.value(family, fallback)));
    });
    connect(region_, &QComboBox::currentIndexChanged, this, [this] {
        const auto id = region_->currentData().toString();
        if (!id.isEmpty()) provider_->setCurrentIndex(provider_->findData(id));
    });

    keyLabel_ = new QLabel(this);
    keyLabel_->setProperty("aiFieldLabel", true);
    layout->addWidget(keyLabel_);
    layout->addSpacing(8);
    auto *keyRow = new QHBoxLayout;
    keyRow->setSpacing(6);
    apiKey_ = new QLineEdit(this);
    apiKey_->setObjectName("aiApiKey");
    apiKey_->setEchoMode(QLineEdit::Password);
    apiKey_->setMaxLength(16384);
    apiKey_->setMinimumWidth(60);
    apiKey_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    keyLabel_->setBuddy(apiKey_);
    showKey_ = apiKey_->addAction(glyph("eye"), QLineEdit::TrailingPosition);
    showKey_->setObjectName("aiShowKey");
    showKey_->setCheckable(true);
    keyRow->addWidget(apiKey_, 1);
    keyMore_ = iconButton("more", {}, this);
    keyMore_->setObjectName("aiKeyActions");
    keyMore_->setFixedSize(36, 42);
    auto *keyMenu = new QMenu(keyMore_);
    clearKey_ = keyMenu->addAction(QString());
    clearKey_->setObjectName("aiClearKey");
    keyMore_->setMenu(keyMenu);
    keyRow->addWidget(keyMore_);
    layout->addLayout(keyRow);
    layout->addSpacing(8);
    keyHint_ = mutedLabel({}, this);
    keyHint_->setWordWrap(true);
    keyHint_->setObjectName("aiKeyStorageHint");
    layout->addWidget(keyHint_);
    layout->addSpacing(20);
    modelLabel_ = new QLabel(this);
    modelLabel_->setProperty("aiFieldLabel", true);
    layout->addWidget(modelLabel_);
    layout->addSpacing(8);
    model_ = new QLineEdit(this);
    model_->setObjectName("aiModel");
    model_->setMaxLength(200);
    modelLabel_->setBuddy(model_);
    layout->addWidget(model_);
    layout->addSpacing(20);
    baseLabel_ = new QLabel(this);
    baseLabel_->setProperty("aiFieldLabel", true);
    layout->addWidget(baseLabel_);
    layout->addSpacing(8);
    baseUrl_ = new QLineEdit(this);
    baseUrl_->setObjectName("aiBaseUrl");
    baseUrl_->setMaxLength(4096);
    baseLabel_->setBuddy(baseUrl_);
    layout->addWidget(baseUrl_);
    layout->addSpacing(20);

    auto *testRow = new QHBoxLayout;
    testRow->setSpacing(14);
    test_ = textButton({}, false, this);
    test_->setObjectName("aiTestConnection");
    test_->setProperty("aiOutline", true);
    testRow->addWidget(test_, 0, Qt::AlignTop);
    status_ = new QLabel(this);
    status_->setObjectName("aiConnectionStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setProperty("muted", true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    testRow->addWidget(status_, 1, Qt::AlignVCenter);
    layout->addLayout(testRow);
    layout->addSpacing(22);
    auto *separator = new QFrame(this);
    separator->setObjectName("aiSettingsDivider");
    separator->setFixedHeight(1);
    layout->addWidget(separator);
    layout->addSpacing(12);

    auto *advancedHeader = new QHBoxLayout;
    advancedHeader->setSpacing(8);
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
    advanced_ = new QWidget(this);
    advanced_->setObjectName("aiAdvancedFields");
    auto *advancedLayout = new QVBoxLayout(advanced_);
    advancedLayout->setContentsMargins(0, 18, 0, 0);
    advancedLayout->setSpacing(8);
    protocolLabel_ = new QLabel(advanced_);
    protocolLabel_->setProperty("aiFieldLabel", true);
    advancedLayout->addWidget(protocolLabel_);
    protocol_ = new SettingsComboBox(advanced_);
    protocol_->setObjectName("aiProtocol");
    protocol_->addItem("OpenAI Responses", "responses");
    protocol_->addItem("OpenAI Chat Completions", "chat-completions");
    protocol_->addItem("Anthropic Messages", "anthropic-messages");
    protocolLabel_->setBuddy(protocol_);
    advancedLayout->addWidget(protocol_);
    advancedLayout->addSpacing(12);
    timeoutLabel_ = new QLabel(advanced_);
    timeoutLabel_->setProperty("aiFieldLabel", true);
    advancedLayout->addWidget(timeoutLabel_);
    timeout_ = new QSpinBox(advanced_);
    timeout_->setObjectName("aiTimeout");
    timeout_->setRange(10, 600);
    timeout_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    timeout_->setMaximumWidth(160);
    timeoutLabel_->setBuddy(timeout_);
    advancedLayout->addWidget(timeout_);
    advancedLayout->addSpacing(12);
    extraLabel_ = new QLabel(advanced_);
    extraLabel_->setProperty("aiFieldLabel", true);
    advancedLayout->addWidget(extraLabel_);
    extra_ = new QPlainTextEdit(advanced_);
    extra_->setObjectName("aiExtraInstructions");
    extra_->setFixedHeight(92);
    extraLabel_->setBuddy(extra_);
    advancedLayout->addWidget(extra_);
    reset_ = textButton({}, false, advanced_);
    reset_->setObjectName("aiResetPreset");
    reset_->setProperty("aiQuiet", true);
    advancedLayout->addWidget(reset_, 0, Qt::AlignLeft);
    layout->addWidget(advanced_);
    advanced_->hide();
    layout->addStretch(1);
    note_ = new QLabel(this);
    note_->setObjectName("aiProviderNote");
    note_->hide();

    connect(showKey_, &QAction::toggled, this, [this](bool checked) {
        apiKey_->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        retranslate();
    });
    connect(apiKey_, &QLineEdit::textEdited, this, &AiSettingsPage::markKeyEdited);
    connect(clearKey_, &QAction::triggered, this, [this] {
        apiKey_->clear();
        markKeyEdited();
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
        setStatus(tr("已恢复连接预设，密钥草稿保持不变。"));
    });
    connect(test_, &QPushButton::clicked, this, [this] {
        if (client_->isRunning()) { client_->cancel(); return; }
        const auto config = currentConnection();
        const auto error = validateAiConnection(config);
        if (!error.isEmpty()) { setStatus(error, true); return; }
        if (apiKey_->text().trimmed().isEmpty()) { setStatus(tr("请先输入 API Key。"), true); return; }
        setStatus(tr("正在验证图文接口…"));
        client_->testConnection(config, apiKey_->text().trimmed());
        credentialBusy(credentialsBusy_);
        retranslate();
    });
    connect(client_, &AiFeedbackClient::completed, this, [this](const QString &) {
        credentialBusy(credentialsBusy_);
        retranslate();
        setStatus(tr("连接成功，支持图片输入。"));
    });
    connect(client_, &AiFeedbackClient::failed, this, [this](const QString &message) {
        credentialBusy(credentialsBusy_);
        retranslate();
        setStatus(message, true);
    });
    connect(client_, &AiFeedbackClient::cancelled, this, [this] {
        credentialBusy(credentialsBusy_); retranslate(); setStatus(tr("已停止测试。"));
    });
    connect(credentials_, &AiCredentials::loaded, this, [this](const QString &id, const QString &key) {
        const bool accept = !loadedKeys_.contains(id) && !dirtyKeys_.contains(id);
        if (accept) keysByProvider_[id] = key;
        loadedKeys_.insert(id);
        if (id == currentProvider_ && accept) {
            apiKey_->setText(key);
            setStatus({});
        }
        if (loadingProvider_ == id) {
            loadingProvider_.clear();
            credentialBusy(false);
            if (id != currentProvider_ && isVisible()) loadKey();
        }
    });
    auto saved = [this](const QString &id) {
        if (!savingCredentials_ || id != savingProvider_) return;
        dirtyKeys_.remove(id);
        loadedKeys_.insert(id);
        keysByProvider_[id] = keysByProvider_.value(id).trimmed();
        if (id == currentProvider_) apiKey_->setText(keysByProvider_.value(id));
        savingProvider_.clear();
        saveNextCredential();
    };
    connect(credentials_, &AiCredentials::saved, this, saved);
    connect(credentials_, &AiCredentials::removed, this, saved);
    connect(credentials_, &AiCredentials::failed, this, [this](const QString &message) {
        const auto failedLoad = loadingProvider_;
        loadingProvider_.clear();
        setStatus(message, true);
        if (savingCredentials_) { finishCredentialSave(false); return; }
        credentialBusy(false);
        if (isVisible() && !failedLoad.isEmpty() && failedLoad != currentProvider_) loadKey();
    });
    connect(prompt_, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setObjectName("aiPromptDialog");
        dialog.setWindowTitle(tr("EditHere 内置提示词"));
        dialog.resize(680, 520);
        auto *body = new QVBoxLayout(&dialog);
        body->setContentsMargins(24, 24, 24, 24);
        auto *text = new QPlainTextEdit(&dialog);
        text->setObjectName("aiBuiltinPrompt");
        text->setReadOnly(true);
        text->setPlainText(aiSystemPrompt());
        body->addWidget(text);
        auto *close = textButton(tr("关闭"), false, &dialog);
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
    const auto config = connections_.value(currentProvider_, defaultAiConnection(currentProvider_));
    baseUrl_->setText(config.baseUrl);
    model_->setText(config.model);
    protocol_->setCurrentIndex(protocol_->findData(config.protocol));
    timeout_->setValue(config.timeoutSeconds);
    extra_->setPlainText(config.extraInstructions);
    apiKey_->setText(keysByProvider_.value(currentProvider_));
    showKey_->setChecked(false);
    setStatus(dirtyKeys_.contains(currentProvider_) ? tr("有未保存的密钥修改。") : QString());
    updateNote();
    if (currentProvider_ == "custom") advancedButton_->setChecked(true);
    if (isVisible()) loadKey();
}
void AiSettingsPage::loadKey() {
    if (currentProvider_.isEmpty() || loadedKeys_.contains(currentProvider_) ||
        dirtyKeys_.contains(currentProvider_) || !loadingProvider_.isEmpty() ||
        credentialsBusy_ || client_->isRunning()) return;
    loadingProvider_ = currentProvider_;
    credentialBusy(true);
    credentials_->load(currentProvider_);
}
void AiSettingsPage::markKeyEdited() {
    if (currentProvider_.isEmpty()) return;
    keysByProvider_[currentProvider_] = apiKey_->text();
    loadedKeys_.insert(currentProvider_);
    dirtyKeys_.insert(currentProvider_);
    setStatus(apiKey_->text().trimmed().isEmpty() ? tr("保存设置后将移除此服务商的密钥。")
                                               : tr("有未保存的密钥修改。"));
}
bool AiSettingsPage::hasPendingCredentials() const { return !dirtyKeys_.isEmpty(); }
void AiSettingsPage::saveCredentials(std::function<void(bool)> finished) {
    if (!finished) return;
    if (dirtyKeys_.isEmpty()) { finished(true); return; }
    if (credentialsBusy_ || client_->isRunning()) {
        setStatus(tr("请等待当前操作完成后再保存。"), true);
        finished(false);
        return;
    }
    saveQueue_ = dirtyKeys_.values();
    std::sort(saveQueue_.begin(), saveQueue_.end());
    saveFinished_ = std::move(finished);
    savingCredentials_ = true;
    credentialBusy(true);
    setStatus(tr("正在保存密钥…"));
    saveNextCredential();
}
void AiSettingsPage::saveNextCredential() {
    if (saveQueue_.isEmpty()) { finishCredentialSave(true); return; }
    savingProvider_ = saveQueue_.takeFirst();
    const auto key = keysByProvider_.value(savingProvider_).trimmed();
    if (key.isEmpty()) credentials_->remove(savingProvider_);
    else credentials_->save(savingProvider_, key);
}
void AiSettingsPage::finishCredentialSave(bool success) {
    savingCredentials_ = false;
    savingProvider_.clear();
    saveQueue_.clear();
    credentialBusy(false);
    if (success) setStatus(tr("密钥修改已保存。"));
    auto finished = std::move(saveFinished_);
    saveFinished_ = {};
    if (finished) finished(success);
}
void AiSettingsPage::updateNote() {
    note_->clear();
    docs_->setEnabled(false);
    for (const auto &preset : aiProviderPresets()) {
        if (preset.id != currentProvider_) continue;
        note_->setText(preset.note);
        family_->setToolTip(preset.note);
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
    family_->setEnabled(editable);
    region_->setEnabled(editable);
    apiKey_->setEnabled(editable);
    keyMore_->setEnabled(editable);
    clearKey_->setEnabled(editable);
    showKey_->setEnabled(editable);
    baseUrl_->setEnabled(editable);
    model_->setEnabled(editable);
    advanced_->setEnabled(editable);
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
    auto family = providerFamily(currentProvider_);
    if (family_->findData(family) < 0) family = "custom";
    if (!currentProvider_.isEmpty()) lastFamilyProvider_[family] = currentProvider_;
    const QSignalBlocker familyBlocker(family_);
    family_->setCurrentIndex(family_->findData(family));
    const QSignalBlocker regionBlocker(region_);
    region_->clear();
    if (family == "glm" || family == "kimi") {
        region_->addItem(tr("中国区"), family + "-cn");
        region_->addItem(tr("国际区"), family + "-global");
        region_->setCurrentIndex(region_->findData(currentProvider_));
    }
    region_->setVisible(region_->count() > 0);
    region_->setAccessibleName(tr("服务地区"));
}
void AiSettingsPage::updateAppearance() {
    if (updatingAppearance_) return;
    updatingAppearance_ = true;
    const QString css = aiTextStyleSheet("settings.qss");
    if (styleSheet() != css) setStyleSheet(css);
    showKey_->setIcon(glyph(showKey_->isChecked() ? "eye-off" : "eye"));
    keyMore_->setIcon(glyph("more"));
    advancedButton_->setIcon(disclosureIcon(advancedButton_->isChecked()));
    for (int i = 0; i < family_->count(); ++i) family_->setItemIcon(i, aiProviderIcon(family_->itemData(i).toString()));
    updatingAppearance_ = false;
}
void AiSettingsPage::retranslate() {
    for (const auto &preset : aiProviderPresets()) {
        const int index = provider_->findData(preset.id);
        if (index >= 0) provider_->setItemText(index, preset.name);
    }
    const QMap<QString, QString> labels{{"openai", "OpenAI"}, {"claude", "Claude"}, {"deepseek", "DeepSeek"},
        {"glm", "GLM"}, {"kimi", "Kimi"}, {"custom", tr("自定义服务")}};
    for (int i = 0; i < family_->count(); ++i) family_->setItemText(i, labels.value(family_->itemData(i).toString()));
    syncProviderChoices();
    updateNote();
    title_->setText(tr("AI 解析"));
    description_->setText(tr("选择服务商，连接用于解析批注的模型。"));
    providerLabel_->setText(tr("服务商"));
    provider_->setAccessibleName(providerLabel_->text());
    family_->setAccessibleName(providerLabel_->text());
    keyLabel_->setText("API Key");
    apiKey_->setAccessibleName("API Key");
    apiKey_->setPlaceholderText(tr("输入 API Key"));
    showKey_->setIcon(glyph(showKey_->isChecked() ? "eye-off" : "eye"));
    showKey_->setText(showKey_->isChecked() ? tr("隐藏密钥") : tr("显示密钥"));
    showKey_->setToolTip(showKey_->text());
    clearKey_->setText(tr("清空密钥"));
    clearKey_->setToolTip(tr("清空后点击“保存设置”，才会删除系统凭据库中此服务商的密钥。"));
    keyMore_->setToolTip(tr("密钥操作"));
    keyMore_->setAccessibleName(keyMore_->toolTip());
    test_->setText(client_->isRunning() ? tr("取消测试") : tr("测试连接"));
    keyHint_->setText(tr("点击“保存设置”后生效。"));
    keyHint_->setToolTip(tr("截图仅在你点击解析时发送给所选提供商，API 调用可能计费。"));
    test_->setToolTip(tr("发送内置小图验证连接和识图能力，不发送当前截图或批注。"));
    advancedButton_->setText(tr("高级选项"));
    advancedButton_->setIcon(disclosureIcon(advancedButton_->isChecked()));
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
    extra_->setPlaceholderText(tr("例如：使用中文、按页面区域分组、精简说明。"));
    reset_->setText(tr("恢复连接预设"));
    reset_->setToolTip(tr("恢复地址、模型和协议，保留密钥草稿。"));
    prompt_->setText(tr("内置提示词"));
    docs_->setText(tr("官方文档 ↗"));
}
} // namespace h2d
