#include "settingsdialog.h"
#include "i18n.h"
#include "ocr.h"
#include "ui.h"
#include "updatechecker.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
namespace h2d {
namespace {
bool installedCopy() {
#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\EditHere\\Installer", QSettings::NativeFormat);
    const QString registered = QFileInfo(reg.value("InstallDir").toString()).canonicalFilePath();
    return !registered.isEmpty() && registered.compare(
        QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath(), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}
#ifdef Q_OS_WIN
constexpr bool windowsUpdates = true;
#else
constexpr bool windowsUpdates = false;
#endif
}
SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent) : QDialog(parent) {
    setObjectName("settingsDialog");
    setMinimumSize(500, 440);
    resize(620, 680);
    languageOnEntry_ = settings.language;
    appliedLanguage_ = settings.language;
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);
    auto title = new QLabel(this);
    title->setObjectName("settingsTitle");
    auto titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    root->addWidget(title);
    auto tabs = tabs_ = new QTabWidget(this);
    tabs->setObjectName("settingsTabs");
    root->addWidget(tabs, 1);
    auto shortcuts = new QWidget;
    auto shortcutLayout = new QVBoxLayout(shortcuts);
    shortcutLayout->setContentsMargins(16, 16, 16, 8);
    shortcutLayout->setSpacing(12);
    auto scroll = new QScrollArea(shortcuts);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto rows = new QWidget;
    auto form = new QFormLayout(rows);
    form->setContentsMargins(0, 0, 8, 8);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    bool globalSection = false, localSection = false;
    for (const auto &definition : shortcutDefinitions()) {
        if (definition.global && !globalSection) {
            auto label = new QLabel(rows);
            label->setObjectName("settingsSection");
            shortcutGlobalLabel_ = label;
            form->addRow(label);
            globalSection = true;
        } else if (!definition.global && !localSection) {
            auto label = new QLabel(rows);
            label->setObjectName("settingsSection");
            shortcutLocalLabel_ = label;
            form->addRow(label);
            localSection = true;
        }
        auto input = new QKeySequenceEdit(rows);
        input->setObjectName("shortcut_" + definition.id);
        input->setMaximumSequenceLength(1);
        input->setClearButtonEnabled(true);
        input->setAttribute(Qt::WA_StyledBackground);
        for (auto clear : input->findChildren<QToolButton *>())
            clear->setIcon(glyph("close"));
        input->setMinimumWidth(200);
        keys_.insert(definition.id, input);
        auto rowLabel = new QLabel(rows);
        rowLabel->setBuddy(input);
        shortcutLabels_.append(rowLabel);
        form->addRow(rowLabel, input);
        connect(input, &QKeySequenceEdit::keySequenceChanged, this, [this] { error_->hide(); });
    }
    scroll->setWidget(rows);
    shortcutLayout->addWidget(scroll, 1);
    tabs->addTab(shortcuts, {});
    auto appearance = new QWidget;
    auto appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->setContentsMargins(20, 22, 20, 20);
    appearanceLayout->setSpacing(14);
    appearanceTitle_ = new QLabel(appearance);
    appearanceTitle_->setObjectName("settingsSection");
    appearanceLayout->addWidget(appearanceTitle_);
    auto themeDescription = mutedLabel({}, appearance);
    themeDescription->setObjectName("themeDescription");
    themeDescription->setWordWrap(true);
    appearanceLayout->addWidget(themeDescription);
    theme_ = new QComboBox(appearance);
    theme_->setObjectName("themeMode");
    theme_->addItem({}, static_cast<int>(ThemeMode::System));
    theme_->addItem({}, static_cast<int>(ThemeMode::Light));
    theme_->addItem({}, static_cast<int>(ThemeMode::Dark));
    theme_->setMinimumHeight(36);
    appearanceLayout->addWidget(theme_);
    languageTitle_ = new QLabel(appearance);
    languageTitle_->setObjectName("settingsSection");
    appearanceLayout->addWidget(languageTitle_);
    auto languageDescription = mutedLabel({}, appearance);
    languageDescription->setObjectName("languageDescription");
    languageDescription->setWordWrap(true);
    appearanceLayout->addWidget(languageDescription);
    language_ = new QComboBox(appearance);
    language_->setObjectName("interfaceLanguage");
    language_->addItem({}, static_cast<int>(LanguageMode::System));
    language_->addItem({}, static_cast<int>(LanguageMode::SimplifiedChinese));
    language_->addItem({}, static_cast<int>(LanguageMode::English));
    language_->setMinimumHeight(36);
    appearanceLayout->addWidget(language_);
    ocrTitle_ = new QLabel(appearance);
    ocrTitle_->setObjectName("settingsSection");
    appearanceLayout->addWidget(ocrTitle_);
    auto ocrDescription = mutedLabel({}, appearance);
    ocrDescription->setObjectName("ocrLanguageDescription");
    ocrDescription->setWordWrap(true);
    appearanceLayout->addWidget(ocrDescription);
    ocrLanguage_ = new QComboBox(appearance);
    ocrLanguage_->setObjectName("ocrLanguage");
    // The reader and the interface offer the same three choices, so the stored value
    // means the same thing on both sides.
    ocrLanguage_->addItem({}, static_cast<int>(OcrLanguageMode::System));
    ocrLanguage_->addItem({}, static_cast<int>(OcrLanguageMode::SimplifiedChinese));
    ocrLanguage_->addItem({}, static_cast<int>(OcrLanguageMode::English));
    ocrLanguage_->setMinimumHeight(36);
    appearanceLayout->addWidget(ocrLanguage_);
    appearanceLayout->addStretch();
    tabs->addTab(appearance, {});
    auto defaults = new QWidget;
    auto defaultsLayout = new QVBoxLayout(defaults);
    defaultsLayout->setContentsMargins(20, 22, 20, 20);
    defaultsLayout->setSpacing(18);
    captureOnStartup_ = new QCheckBox(defaults);
    captureOnStartup_->setObjectName("captureOnStartup");
    launchAtLogin_ = new QCheckBox(defaults);
    launchAtLogin_->setObjectName("launchAtLogin");
    fitImageOnOpen_ = new QCheckBox(defaults);
    fitImageOnOpen_->setObjectName("fitImageOnOpen");
    embedOriginal_ = new QCheckBox(defaults);
    embedOriginal_->setObjectName("defaultEmbedOriginal");
    confirmBeforeDiscard_ = new QCheckBox(defaults);
    confirmBeforeDiscard_->setObjectName("confirmBeforeDiscard");
    defaultsLayout->addWidget(captureOnStartup_);
    auto loginLayout = new QVBoxLayout;
    loginLayout->setSpacing(6);
    loginLayout->addWidget(launchAtLogin_);
    launchAtLoginNotice_ = mutedLabel({}, defaults);
    launchAtLoginNotice_->setObjectName("launchAtLoginNotice");
    launchAtLoginNotice_->setWordWrap(true);
    launchAtLoginNotice_->hide();
    loginLayout->addWidget(launchAtLoginNotice_);
    defaultsLayout->addLayout(loginLayout);
    defaultsLayout->addWidget(fitImageOnOpen_);
    defaultsLayout->addWidget(embedOriginal_);
    defaultsLayout->addWidget(confirmBeforeDiscard_);
    auto feedbackRow = new QHBoxLayout;
    feedbackDir_ = new QLineEdit(defaults);
    feedbackDir_->setObjectName("feedbackDir");
    feedbackDir_->setMinimumWidth(280);
    auto browse = new QToolButton(defaults);
    browse->setText("…");
    browse->setObjectName("feedbackDirBrowse");
    connect(browse, &QToolButton::clicked, this, [this] {
        const auto dir = QFileDialog::getExistingDirectory(
            this, tr("选择反馈临时目录"),
            feedbackDir_->text().isEmpty() ? QString() : feedbackDir_->text());
        if (!dir.isEmpty())
            feedbackDir_->setText(QDir::toNativeSeparators(dir));
    });
    feedbackRow->addWidget(feedbackDir_, 1);
    feedbackRow->addWidget(browse);
    auto feedbackLabel = new QLabel(defaults);
    feedbackLabel->setObjectName("feedbackDirLabel");
    feedbackLabel->setBuddy(feedbackDir_);
    auto feedbackForm = new QFormLayout;
    feedbackForm->addRow(feedbackLabel, feedbackRow);
    defaultsLayout->addLayout(feedbackForm);
    auto exportHint = mutedLabel({}, defaults);
    exportHint->setObjectName("exportHint");
    exportHint->setWordWrap(true);
    defaultsLayout->addWidget(exportHint);
    auto toolForm = new QFormLayout;
    defaultTool_ = new QComboBox(defaults);
    defaultTool_->setObjectName("defaultTool");
    defaultTool_->addItems({"", "", "", ""});
    auto toolLabel = new QLabel(defaults);
    toolLabel->setObjectName("defaultToolLabel");
    toolLabel->setBuddy(defaultTool_);
    toolForm->addRow(toolLabel, defaultTool_);
    defaultsLayout->addLayout(toolForm);
    defaultsLayout->addStretch();
    tabs->addTab(defaults, {});

    auto toolbar = new QWidget;
    toolbar->setObjectName("settingsToolbarPage");
    auto toolbarLayout = new QVBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(20, 22, 20, 20);
    toolbarLayout->setSpacing(18);
    toolbarTitle_ = new QLabel(toolbar);
    toolbarTitle_->setObjectName("settingsSection");
    toolbarLayout->addWidget(toolbarTitle_);
    for (const auto &definition : toolbarActionDefinitions()) {
        auto checkbox = new QCheckBox(toolbar);
        checkbox->setObjectName("toolbar_" + definition.id);
        toolbarActions_.insert(definition.id, checkbox);
        toolbarLayout->addWidget(checkbox);
        connect(checkbox, &QCheckBox::toggled, this, [this] { error_->hide(); });
    }
    toolbarLayout->addStretch();
    tabs->addTab(toolbar, {});

    auto about = new QWidget;
    about->setObjectName("settingsAboutPage");
    auto aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(20, 22, 20, 20);
    aboutLayout->setSpacing(18);
    versionLabel_ = new QLabel(about);
    versionLabel_->setObjectName("settingsSection");
    aboutLayout->addWidget(versionLabel_);
    auto description = mutedLabel({}, about);
    description->setObjectName("aboutDescription");
    description->setWordWrap(true);
    aboutLayout->addWidget(description);
    auto license = mutedLabel({}, about);
    license->setObjectName("aboutLicense");
    license->setWordWrap(true);
    license->setTextFormat(Qt::RichText);
    license->setOpenExternalLinks(true);
    aboutLayout->addWidget(license);
    checkUpdatesOnStartup_ = new QCheckBox(about);
    checkUpdatesOnStartup_->setObjectName("checkUpdatesOnStartup");
    aboutLayout->addWidget(checkUpdatesOnStartup_);
    auto status = new QLabel(about);
    status->setObjectName("updateStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    aboutLayout->addWidget(status);
    auto check = textButton({}, true, about);
    check->setObjectName("checkUpdates");
    auto releases = textButton({}, false, about);
    releases->setObjectName("openReleases");
    auto updateActions = new QHBoxLayout;
    updateActions->addWidget(check);
    updateActions->addWidget(releases);
    updateActions->addStretch();
    aboutLayout->addLayout(updateActions);
    auto progressBar = new QProgressBar(about);
    progressBar->setObjectName("updateProgress");
    progressBar->setVisible(false);
    aboutLayout->addWidget(progressBar);
    aboutLayout->addStretch();
    tabs->addTab(about, {});
    updater_ = new UpdateChecker(this);
    connect(check, &QPushButton::clicked, this, [this, status, check] {
        if (updater_->downloading()) return;
        status->setText(tr("正在检查更新…"));
        check->setEnabled(false);
        updater_->check();
    });
    connect(updater_, &UpdateChecker::finished, this,
            [this, status, check, releases](UpdateChecker::Status state, const QString &message, const QUrl &url) {
                status->setText(message);
                check->setEnabled(true);
                releases->setProperty("updateAvailable", state == UpdateChecker::Available);
                releases->setProperty("releaseUrl", url);
                const bool automatic = UpdateChecker::canAutoInstall(updater_->lastResult(), installedCopy(), windowsUpdates);
                releases->setProperty("automaticUpdate", automatic);
                // The button label depends on the availability, so refresh it here.
                releases->setText(automatic ? tr("立即更新") : tr("打开发布页"));
            });
    connect(releases, &QPushButton::clicked, this, [this, releases, status, progressBar] {
        if (releases->property("updateAvailable").toBool()) {
            const auto &result = updater_->lastResult();
            const bool isInstaller = installedCopy();
            if (!UpdateChecker::canAutoInstall(result, isInstaller, windowsUpdates)) {
                if (!QDesktopServices::openUrl(result.url))
                    status->setText(tr("无法打开浏览器，请访问 github.com/Inginnng/EditHere/releases。"));
                return;
            }
            status->setText(tr("正在下载安装器…"));
            releases->setEnabled(false);
            findChild<QPushButton *>("checkUpdates")->setEnabled(false);
            progressBar->setVisible(true);
            progressBar->setRange(0, 100);
            progressBar->setValue(0);
            updater_->downloadAndInstall(result.installer, result.installerHash, isInstaller);
        } else {
            const auto url = releases->property("releaseUrl").toUrl();
            if (!QDesktopServices::openUrl(url.isEmpty() ? UpdateChecker::releasesUrl() : url))
                status->setText(tr("无法打开浏览器，请访问 github.com/Inginnng/EditHere/releases。"));
        }
    });
    connect(updater_, &UpdateChecker::downloadProgress, this,
            [progressBar](qint64 received, qint64 total) {
                if (total > 0)
                    progressBar->setValue(static_cast<int>(received * 100 / total));
            });
    connect(updater_, &UpdateChecker::installStarted, this, [this] {
        // Updating is not saving the settings draft. Restore the live preview,
        // and let the controller own the already-confirmed application exit.
        reject();
        emit updateInstallStarted();
    });
    connect(updater_, &UpdateChecker::installFailed, this,
            [this, status, releases, progressBar](const QString &message) {
                status->setText(message);
                releases->setEnabled(true);
                findChild<QPushButton *>("checkUpdates")->setEnabled(true);
                progressBar->setVisible(false);
            });
    error_ = new QLabel(this);
    error_->setObjectName("errorLabel");
    error_->setProperty("error", true);
    error_->setWordWrap(true);
    error_->hide();
    root->addWidget(error_);
    auto actions = new QHBoxLayout;
    auto reset = textButton({}, false, this);
    reset->setObjectName("settingsReset");
    auto guide = textButton({}, false, this);
    guide->setObjectName("restartGuide");
    auto cancel = textButton({}, false, this);
    cancel->setObjectName("settingsCancel");
    auto saveButton = textButton({}, true, this);
    saveButton->setObjectName("settingsSave");
    saveButton->setDefault(true);
    actions->addWidget(reset);
    actions->addWidget(guide);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(saveButton);
    root->addLayout(actions);
    connect(reset, &QPushButton::clicked, this, [this] {
        setDraft(defaultSettings());
        error_->hide();
    });
    connect(cancel, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(guide, &QPushButton::clicked, this, [this] {
        reject();
        emit guideRequested();
    });
    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] { error_->hide(); });
    connect(language_, &QComboBox::currentIndexChanged, this, [this] {
        error_->hide();
        applyLanguage(static_cast<LanguageMode>(language_->currentData().toInt()));
    });
    connect(launchAtLogin_, &QCheckBox::toggled, this, [this] { error_->hide(); });
    // Every label is set in retranslate(), so the constructor has to call it too.
    retranslate();
    setDraft(settings);
}
void SettingsDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        retranslate();
}
void SettingsDialog::retranslate() {
    setWindowTitle(tr("EditHere 设置"));
    if (auto title = findChild<QLabel *>("settingsTitle"))
        title->setText(tr("设置"));
    if (shortcutGlobalLabel_)
        shortcutGlobalLabel_->setText(tr("全局快捷键"));
    if (shortcutLocalLabel_)
        shortcutLocalLabel_->setText(tr("应用内快捷键"));
    const auto definitions = shortcutDefinitions();
    for (int i = 0; i < shortcutLabels_.size() && i < definitions.size(); ++i) {
        shortcutLabels_[i]->setText(definitions[i].label);
        const auto input = keys_.value(definitions[i].id);
        if (!input)
            continue;
        input->setAccessibleName(definitions[i].label + tr("快捷键"));
        input->setToolTip(definitions[i].global ? tr("在其他应用中也可使用；清空则停用。")
                                                : tr("在 EditHere 编辑窗口中使用；清空则停用。"));
        if (auto field = input->findChild<QLineEdit *>())
            field->setPlaceholderText(tr("点击录制快捷键"));
    }
    const QStringList tabTitles{tr("快捷键"), tr("外观"), tr("默认行为"), tr("工具栏"), tr("关于与更新")};
    for (int i = 0; i < tabs_->count() && i < tabTitles.size(); ++i)
        tabs_->setTabText(i, tabTitles[i]);
    if (appearanceTitle_)
        appearanceTitle_->setText(tr("外观模式"));
    if (auto description = findChild<QLabel *>("themeDescription"))
        description->setText(tr("选择适合你的界面。跟随系统会随系统外观自动切换。"));
    theme_->setAccessibleName(tr("外观模式"));
    theme_->setItemText(0, tr("跟随系统"));
    theme_->setItemText(1, tr("亮色"));
    theme_->setItemText(2, tr("暗色"));
    if (languageTitle_)
        languageTitle_->setText(tr("界面语言"));
    if (auto description = findChild<QLabel *>("languageDescription"))
        description->setText(tr("切换后立即预览；保存后生效，取消则回到原来的语言。"));
    language_->setAccessibleName(tr("界面语言"));
    language_->setItemText(0, tr("跟随系统"));
    language_->setItemText(1, languageDisplayName(LanguageMode::SimplifiedChinese));
    language_->setItemText(2, languageDisplayName(LanguageMode::English));
    if (ocrTitle_)
        ocrTitle_->setText(tr("文字识别语言"));
    if (auto description = findChild<QLabel *>("ocrLanguageDescription"))
        description->setText(tr("截图后点击文字识别或按 T 时使用的语言。跟随系统由系统已安装的识别语言决定。"));
    ocrLanguage_->setAccessibleName(tr("文字识别语言"));
    ocrLanguage_->setItemText(0, tr("跟随系统"));
    ocrLanguage_->setItemText(1, ocrLanguageLabel(OcrLanguageMode::SimplifiedChinese));
    ocrLanguage_->setItemText(2, ocrLanguageLabel(OcrLanguageMode::English));
    captureOnStartup_->setText(tr("启动后立即截图"));
    captureOnStartup_->setToolTip(tr("关闭后启动时只驻留托盘；点击托盘或按全局快捷键开始截图。"));
    launchAtLogin_->setText(tr("开机时启动 EditHere"));
    launchAtLogin_->setToolTip(tr("请保留程序所在文件夹；移动后需重新设置。"));
    fitImageOnOpen_->setText(tr("打开图片时自动适应窗口"));
    fitImageOnOpen_->setToolTip(tr("关闭后以 100% 显示，仍可随时缩放或使用适应窗口。"));
    embedOriginal_->setText(tr("导出 JSON 默认包含原图"));
    confirmBeforeDiscard_->setText(tr("关闭批注窗口时询问是否保存"));
    confirmBeforeDiscard_->setToolTip(
        tr("关闭后不再弹出「是否保存当前修改」，直接放弃未保存的批注。在批注窗口里勾选「不再提醒」会关掉这一项。"));
    if (auto label = findChild<QLabel *>("feedbackDirLabel"))
        label->setText(tr("反馈临时目录"));
    feedbackDir_->setPlaceholderText(tr("默认：缓存目录下的 feedback"));
    feedbackDir_->setToolTip(tr("自定义反馈 JSON 临时文件的保存位置。留空使用默认缓存目录。"));
    if (auto browse = findChild<QToolButton *>("feedbackDirBrowse"))
        browse->setToolTip(tr("选择反馈临时目录"));
    if (auto hint = findChild<QLabel *>("exportHint"))
        hint->setText(tr("包含原图的 JSON 可独立还原。保存项目始终包含原图。"));
    if (auto label = findChild<QLabel *>("defaultToolLabel"))
        label->setText(tr("默认标注工具"));
    defaultTool_->setAccessibleName(tr("默认标注工具"));
    const QStringList tools{tr("智能选块"), tr("点标注"), tr("框选标注"), tr("调整批注")};
    for (int i = 0; i < defaultTool_->count() && i < tools.size(); ++i)
        defaultTool_->setItemText(i, tools[i]);
    if (toolbarTitle_)
        toolbarTitle_->setText(tr("显示在底部工具栏"));
    for (const auto &definition : toolbarActionDefinitions()) {
        auto checkbox = toolbarActions_.value(definition.id);
        if (!checkbox)
            continue;
        checkbox->setText(definition.label);
        checkbox->setAccessibleName(tr("在工具栏显示") + definition.label);
        if (definition.id == "saveImage")
            checkbox->setToolTip(tr("保存包含批注和布局调整的图片。"));
        else if (definition.id == "exportJson")
            checkbox->setToolTip(tr("打开 JSON 预览，可查看、复制或保存文件。"));
        else if (definition.id == "copyJson")
            checkbox->setToolTip(tr("将 JSON 直接复制到剪贴板。"));
    }
    if (versionLabel_)
        versionLabel_->setText(tr("EditHere · 改这里") + "  " EDITHERE_VERSION);
    if (auto description = findChild<QLabel *>("aboutDescription"))
        description->setText(tr("截图、批注与布局调整，让设计修改意见更清楚。"));
    if (auto license = findChild<QLabel *>("aboutLicense"))
        license->setText(tr("以 MIT License 发布：可免费商用、修改与分发，保留版权声明即可。"
                            R"(<br><a href="https://github.com/Inginnng/EditHere/blob/codex/native/LICENSING.md">查看许可说明</a>)"));
    checkUpdatesOnStartup_->setText(tr("启动时检查更新"));
    if (auto status = findChild<QLabel *>("updateStatus"); status && status->text().isEmpty())
        status->setText(tr("尚未检查更新。"));
    if (auto check = findChild<QPushButton *>("checkUpdates"))
        check->setText(tr("检查更新"));
    if (auto releases = findChild<QPushButton *>("openReleases"))
        releases->setText(releases->property("automaticUpdate").toBool() ? tr("立即更新") : tr("打开发布页"));
    if (auto reset = findChild<QPushButton *>("settingsReset"))
        reset->setText(tr("恢复默认"));
    if (auto guide = findChild<QPushButton *>("restartGuide")) {
        guide->setText(tr("使用引导"));
        guide->setToolTip(tr("关闭设置并打开使用引导；未保存的设置将不会保存。"));
    }
    if (auto cancel = findChild<QPushButton *>("settingsCancel"))
        cancel->setText(tr("取消"));
    if (auto save = findChild<QPushButton *>("settingsSave"))
        save->setText(tr("保存"));
}
void SettingsDialog::applyLanguage(LanguageMode mode) {
    if (mode == appliedLanguage_)
        return;
    if (!installLanguage(mode)) {
        error_->setText(tr("界面语言加载失败，请重新安装 EditHere。"));
        error_->show();
        QSignalBlocker blocker(language_);
        language_->setCurrentIndex(language_->findData(static_cast<int>(appliedLanguage_)));
        return;
    }
    appliedLanguage_ = mode;
    // Qt sends LanguageChange to every top-level widget, which covers the editor
    // window and its canvases. The tray menu is a child, so it is updated through
    // this signal instead, and this dialog rebuilds its own labels directly.
    emit languageApplied();
    retranslate();
}
void SettingsDialog::reject() {
    // Cancelling undoes the live language preview along with every other edit.
    applyLanguage(languageOnEntry_);
    QDialog::reject();
}
void SettingsDialog::showUpdates(bool checkNow) {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsAboutPage"));
    if (checkNow)
        QTimer::singleShot(0, findChild<QPushButton *>("checkUpdates"), &QPushButton::click);
}
void SettingsDialog::showToolbar() {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsToolbarPage"));
}
void SettingsDialog::setLaunchAtLoginNotice(const QString &notice) {
    launchAtLoginNotice_->setText(notice);
    launchAtLoginNotice_->setVisible(!notice.isEmpty());
}
void SettingsDialog::setDraft(const AppSettings &settings) {
    captureStyle_ = settings.captureStyle;
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it)
        it.value()->setKeySequence(settings.shortcuts.value(it.key()));
    for (auto it = toolbarActions_.cbegin(); it != toolbarActions_.cend(); ++it)
        it.value()->setChecked(settings.toolbarActions.contains(it.key()));
    theme_->setCurrentIndex(theme_->findData(static_cast<int>(settings.theme)));
    language_->setCurrentIndex(language_->findData(static_cast<int>(settings.language)));
    ocrLanguage_->setCurrentIndex(ocrLanguage_->findData(static_cast<int>(settings.ocrLanguage)));
    captureOnStartup_->setChecked(settings.captureOnStartup);
    launchAtLogin_->setChecked(settings.launchAtLogin);
    fitImageOnOpen_->setChecked(settings.fitImageOnOpen);
    embedOriginal_->setChecked(settings.embedOriginal);
    confirmBeforeDiscard_->setChecked(settings.confirmBeforeDiscard);
    checkUpdatesOnStartup_->setChecked(settings.checkUpdatesOnStartup);
    feedbackDir_->setText(QDir::toNativeSeparators(settings.feedbackDir));
    defaultTool_->setCurrentIndex(settings.defaultTool);
}
AppSettings SettingsDialog::settings() const {
    AppSettings result;
    result.captureStyle = captureStyle_;
    result.captureOnStartup = captureOnStartup_->isChecked();
    result.launchAtLogin = launchAtLogin_->isChecked();
    result.fitImageOnOpen = fitImageOnOpen_->isChecked();
    result.embedOriginal = embedOriginal_->isChecked();
    result.confirmBeforeDiscard = confirmBeforeDiscard_->isChecked();
    result.checkUpdatesOnStartup = checkUpdatesOnStartup_->isChecked();
    result.feedbackDir = QDir::fromNativeSeparators(feedbackDir_->text().trimmed());
    result.defaultTool = defaultTool_->currentIndex();
    result.language = static_cast<LanguageMode>(language_->currentData().toInt());
    result.ocrLanguage = static_cast<OcrLanguageMode>(ocrLanguage_->currentData().toInt());
    result.theme = static_cast<ThemeMode>(theme_->currentData().toInt());
    result.toolbarActions.clear();
    for (const auto &definition : toolbarActionDefinitions())
        if (toolbarActions_.value(definition.id)->isChecked())
            result.toolbarActions.append(definition.id);
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it)
        result.shortcuts.insert(it.key(), it.value()->keySequence());
    return result;
}
void SettingsDialog::setApplyHandler(std::function<QString(const AppSettings &)> handler) {
    apply_ = std::move(handler);
}
void SettingsDialog::setUpdatePreparationHandler(std::function<bool()> handler) {
    updater_->setInstallConfirmation(std::move(handler));
}
void SettingsDialog::save() {
    const auto draft = settings();
    auto error = validateSettings(draft);
    if (error.isEmpty() && apply_)
        error = apply_(draft);
    if (!error.isEmpty()) {
        error_->setText(error);
        error_->show();
        return;
    }
    accept();
}
} // namespace h2d
