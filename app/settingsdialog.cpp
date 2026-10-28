#include "settingsdialog.h"
#include "aisettingspage.h"
#include "diagnosticspage.h"
#include "diagnostics.h"
#include "i18n.h"
#include "ocr.h"
#include "ui.h"
#include "updatechecker.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
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
SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent, AiCredentials *credentials) : QDialog(parent) {
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
    embedFeedbackImage_ = new QCheckBox(defaults);
    embedFeedbackImage_->setObjectName("defaultEmbedImage");
    copyJsonTextWithImage_ = new QCheckBox(defaults);
    copyJsonTextWithImage_->setObjectName("defaultCopyJsonTextWithImage");
    includeWindowTitle_ = new QCheckBox(defaults);
    includeWindowTitle_->setObjectName("includeWindowTitle");
    includeRecognizedText_ = new QCheckBox(defaults);
    includeRecognizedText_->setObjectName("includeRecognizedText");
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
    defaultsLayout->addWidget(embedFeedbackImage_);
    defaultsLayout->addWidget(copyJsonTextWithImage_);
    defaultsLayout->addWidget(includeRecognizedText_);
    defaultsLayout->addWidget(includeWindowTitle_);
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

    auto scrollPage = new QWidget;
    scrollPage->setObjectName("settingsScrollPage");
    auto scrollLayout = new QVBoxLayout(scrollPage);
    scrollLayout->setContentsMargins(20, 22, 20, 20);
    scrollLayout->setSpacing(14);
    auto scrollHint = new QLabel(scrollPage);
    scrollHint->setObjectName("scrollSettingsHint");
    scrollHint->setWordWrap(true);
    scrollLayout->addWidget(scrollHint);
    auto scrollForm = new QFormLayout;
    scrollAxis_ = new QComboBox(scrollPage);
    scrollAxis_->setObjectName("scrollDefaultAxis");
    scrollAxis_->addItem({}, static_cast<int>(Qt::Vertical));
    scrollAxis_->addItem({}, static_cast<int>(Qt::Horizontal));
    auto axisLabel = new QLabel(scrollPage);
    axisLabel->setObjectName("scrollAxisLabel");
    axisLabel->setBuddy(scrollAxis_);
    scrollForm->addRow(axisLabel, scrollAxis_);
    scrollLayout->addLayout(scrollForm);
    scrollAutoCrop_ = new QCheckBox(scrollPage);
    scrollAutoCrop_->setObjectName("scrollDefaultAutoCrop");
    scrollLayout->addWidget(scrollAutoCrop_);
    scrollUltraLong_ = new QCheckBox(scrollPage);
    scrollUltraLong_->setObjectName("scrollUltraLong");
    scrollLayout->addWidget(scrollUltraLong_);
    scrollLayout->addStretch();
    tabs->addTab(scrollPage, {});

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

    auto aboutContent = new QWidget;
    aboutContent->setObjectName("aboutContent");
    auto aboutLayout = new QVBoxLayout(aboutContent);
    aboutLayout->setContentsMargins(20, 22, 20, 20);
    aboutLayout->setSpacing(18);
    versionLabel_ = new QLabel(aboutContent);
    versionLabel_->setObjectName("settingsSection");
    aboutLayout->addWidget(versionLabel_);
    auto description = mutedLabel({}, aboutContent);
    description->setObjectName("aboutDescription");
    description->setWordWrap(true);
    aboutLayout->addWidget(description);

    // The note explains why the program exists, but it is not something every
    // reader needs on screen while changing a setting, so it opens on request.
    auto noteButton = textButton({}, false, aboutContent);
    noteButton->setObjectName("aboutNoteButton");
    aboutLayout->addWidget(noteButton, 0, Qt::AlignLeft);

    auto license = mutedLabel({}, aboutContent);
    license->setObjectName("aboutLicense");
    license->setWordWrap(true);
    license->setTextFormat(Qt::RichText);
    license->setOpenExternalLinks(true);
    aboutLayout->addWidget(license);
    checkUpdatesOnStartup_ = new QCheckBox(aboutContent);
    checkUpdatesOnStartup_->setObjectName("checkUpdatesOnStartup");
    aboutLayout->addWidget(checkUpdatesOnStartup_);
    auto status = new QLabel(aboutContent);
    status->setObjectName("updateStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    aboutLayout->addWidget(status);
    auto check = textButton({}, true, aboutContent);
    check->setObjectName("checkUpdates");
    auto releases = textButton({}, false, aboutContent);
    releases->setObjectName("openReleases");
    auto cancelUpdate = textButton({}, false, aboutContent);
    cancelUpdate->setObjectName("cancelUpdate");
    cancelUpdate->hide();
    auto updateActions = new QHBoxLayout;
    updateActions->addWidget(check);
    updateActions->addWidget(releases);
    updateActions->addWidget(cancelUpdate);
    updateActions->addStretch();
    aboutLayout->addLayout(updateActions);
    auto progressBar = new QProgressBar(aboutContent);
    progressBar->setObjectName("updateProgress");
    progressBar->setVisible(false);
    aboutLayout->addWidget(progressBar);
    auto feedback = mutedLabel({}, aboutContent);
    feedback->setObjectName("aboutFeedback");
    feedback->setWordWrap(true);
    // The group number is the whole point of the line, so let it be selected.
    feedback->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutLayout->addWidget(feedback);
    aboutLayout->addStretch();
    // The note makes the page taller than a small window, so it scrolls like the
    // shortcut page instead of pushing the update controls out of reach.
    auto aboutScroll = new QScrollArea(tabs);
    aboutScroll->setObjectName("settingsAboutPage");
    aboutScroll->setWidgetResizable(true);
    aboutScroll->setFrameShape(QFrame::NoFrame);
    aboutScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    aboutScroll->setWidget(aboutContent);
    tabs->addTab(aboutScroll, {});
    tabs->addTab(new DiagnosticsPage(tabs), {});
    auto *aiScroll = new QScrollArea(tabs);
    aiScroll->setObjectName("settingsAiPage");
    aiScroll->setWidgetResizable(true);
    aiScroll->setFrameShape(QFrame::NoFrame);
    aiScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    aiPage_ = new AiSettingsPage(aiScroll, credentials);
    aiScroll->setWidget(aiPage_);
    tabs->addTab(aiScroll, {});
    connect(noteButton, &QPushButton::clicked, this, [this] {
        // Built on demand so it always reflects the language that is active when
        // it is opened, and so a reader who never asks pays nothing for it.
        auto *note = new QDialog(this);
        note->setObjectName("authorNoteDialog");
        note->setAttribute(Qt::WA_DeleteOnClose);
        note->setWindowModality(Qt::WindowModal);
        note->setWindowTitle(tr("作者留言"));
        note->setFixedWidth(520);
        auto layout = new QVBoxLayout(note);
        layout->setContentsMargins(24, 22, 24, 20);
        layout->setSpacing(16);
        auto heading = new QLabel(tr("作者留言"), note);
        heading->setObjectName("authorNoteHeading");
        layout->addWidget(heading);
        auto text = new QLabel(note);
        text->setObjectName("authorNoteText");
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text->setText(tr("我是印迹，学生个人开发者。\n\n"
                         "做它的原因很直接：Vibe Coding 时 AI 像一台许愿机，一句话就能变出能跑的界面；"
                         "可一旦要改细节，想法很清楚，但要表达就很困难。于是我想，能不能把需求直接标在图上——"
                         "哪一块、多大、往哪挪，让想法先被看见，再被实现。EditHere 就是这个想法的产物。\n\n"
                         "最近在忙秋招，更新会慢一些，但不会停，很多设想中的功能也在逐步实现。"
                         "我希望能从 EditHere 出发，挖掘更多人与 AI 交互的创新工具，开发最好的人机交互软件！\n\n"
                         "如果你觉得它有用，欢迎在 GitHub 点一个 Star，这对我很重要，或把遇到的问题提给我。"
                         "感谢使用它的你。"));
        layout->addWidget(text);
        auto actions = new QHBoxLayout;
        auto close = textButton(tr("关闭"), false, note);
        close->setObjectName("authorNoteClose");
        auto star = textButton(tr("去 GitHub 点个 Star"), true, note);
        star->setObjectName("authorNoteStar");
        star->setToolTip(tr("在浏览器中打开 EditHere 的 GitHub 仓库。"));
        actions->addWidget(close);
        actions->addStretch();
        actions->addWidget(star);
        layout->addLayout(actions);
        connect(close, &QPushButton::clicked, note, &QDialog::accept);
        connect(star, &QPushButton::clicked, note, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Inginnng/EditHere")));
        });
        note->adjustSize();
        note->show();
    });
    updater_ = new UpdateChecker(this);
    connect(cancelUpdate, &QPushButton::clicked, updater_, &UpdateChecker::cancelDownload);
    connect(check, &QPushButton::clicked, this, [this, status, check] {
        if (updater_->downloading()) return;
        status->setText(tr("正在检查更新…"));
        check->setEnabled(false);
        updater_->check();
    });
    connect(updater_, &UpdateChecker::finished, this, [this] { offerUpdate(updater_->lastResult()); });
    connect(releases, &QPushButton::clicked, this, [this, releases, status, progressBar, cancelUpdate] {
        if (releases->property("updateAvailable").toBool()) {
            const auto &result = updateResult_;
            const bool isInstaller = installedCopy();
            if (!UpdateChecker::canAutoInstall(result, isInstaller, windowsUpdates)) {
                if (!QDesktopServices::openUrl(result.url))
                    status->setText(tr("无法打开浏览器，请访问：%1").arg(result.url.toString()));
                return;
            }
            status->setText(tr("正在下载安装器…"));
            releases->setEnabled(false);
            findChild<QPushButton *>("checkUpdates")->setEnabled(false);
            progressBar->setVisible(true);
            progressBar->setRange(0, 100);
            progressBar->setValue(0);
            cancelUpdate->show();
            updater_->downloadAndInstall(result.installer, result.installerHash, isInstaller, result.tagName);
        } else {
            const auto url = releases->property("releaseUrl").toUrl();
            const auto releaseUrl = url.isEmpty() ? UpdateChecker::releasesUrl() : url;
            if (!QDesktopServices::openUrl(releaseUrl))
                status->setText(tr("无法打开浏览器，请访问：%1").arg(releaseUrl.toString()));
        }
    });
    connect(updater_, &UpdateChecker::downloadProgress, this,
            [progressBar](qint64 received, qint64 total) {
                if (total > 0)
                    progressBar->setValue(static_cast<int>(received * 100 / total));
            });
    connect(updater_, &UpdateChecker::installerPreparing, this, [this, status, progressBar] {
        status->setText(tr("正在检查安装条件并准备更新，当前工作将保留到安装器就绪。"));
        progressBar->setRange(0, 0);
    });
    connect(updater_, &UpdateChecker::installStarted, this, [this] {
        // Updating is not saving the settings draft. Restore the live preview,
        // and let the controller own the already-confirmed application exit.
        reject();
        emit updateInstallStarted();
    });
    connect(updater_, &UpdateChecker::installFailed, this,
            [this, status, releases, progressBar, cancelUpdate](const QString &message) {
                status->setText(message);
                releases->setEnabled(true);
                findChild<QPushButton *>("checkUpdates")->setEnabled(true);
                progressBar->setVisible(false);
                cancelUpdate->hide();
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
    const QStringList tabTitles{tr("快捷键"), tr("外观"), tr("默认行为"), tr("长截图"),
                                tr("工具栏"), tr("关于与更新"), tr("诊断日志"), tr("AI 接口")};
    for (int i = 0; i < tabs_->count() && i < tabTitles.size(); ++i)
        tabs_->setTabText(i, tabTitles[i]);
    if (aiPage_) aiPage_->retranslate();
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
    includeWindowTitle_->setText(tr("导出时附带窗口标题"));
    includeWindowTitle_->setToolTip(tr("关闭后反馈 JSON 不包含窗口标题，应用名称与页面描述仍会保留。"));
    includeRecognizedText_->setText(tr("导出时附带区域文字"));
    includeRecognizedText_->setToolTip(tr("自动在本机识别批注区域文字。关闭后导出 JSON 不包含识别文字，项目仍保留结果。"));
    embedFeedbackImage_->setText(tr("原图嵌入 JSON"));
    embedFeedbackImage_->setToolTip(tr("未勾选时将 JSON 与原图作为独立文件复制。"));
    copyJsonTextWithImage_->setText(tr("复制 JSON 内容时携带原图信息"));
    copyJsonTextWithImage_->setToolTip(tr("默认不携带原图信息；也可在复制按钮旁的选项中快速切换。"));
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
    if (auto hint = findChild<QLabel *>("scrollSettingsHint"))
        hint->setText(tr("长截图默认由你滚动页面；截取时仍可切换方向和自动裁剪。"));
    if (auto label = findChild<QLabel *>("scrollAxisLabel"))
        label->setText(tr("默认截图方向"));
    scrollAxis_->setItemText(0, tr("垂直"));
    scrollAxis_->setItemText(1, tr("水平"));
    scrollAutoCrop_->setText(tr("反向滚动时自动裁剪"));
    scrollUltraLong_->setText(tr("超长截图（最长 200 万像素）"));
    scrollUltraLong_->setToolTip(tr("分块采集并保存 PNG；超过编辑器图像上限时可保存，复制、贴图和编辑受图像大小限制。"));
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
            checkbox->setToolTip(tr("打开 JSON 预览，可查看或复制反馈。"));
        else if (definition.id == "copyJson")
            checkbox->setToolTip(tr("将 JSON 与配套原图文件复制到剪贴板。"));
    }
    if (versionLabel_)
        versionLabel_->setText(tr("EditHere · 改这里") + "  " EDITHERE_VERSION);
    if (auto description = findChild<QLabel *>("aboutDescription"))
        description->setText(tr("截图、批注与布局调整，让设计修改意见更清楚。"));
    if (auto note = findChild<QPushButton *>("aboutNoteButton")) {
        note->setText(tr("作者留言"));
        note->setAccessibleName(tr("作者留言"));
        note->setToolTip(tr("关于为什么做 EditHere，以及怎样参与。"));
    }
    if (auto license = findChild<QLabel *>("aboutLicense"))
        license->setText(tr(R"(<a href="https://github.com/Inginnng/EditHere/blob/codex/native/LICENSING.md">查看许可说明</a>)"));
    checkUpdatesOnStartup_->setText(tr("启动时检查更新"));
    if (auto status = findChild<QLabel *>("updateStatus"); status && status->text().isEmpty())
        status->setText(tr("尚未检查更新。"));
    if (auto check = findChild<QPushButton *>("checkUpdates"))
        check->setText(tr("检查更新"));
    if (auto releases = findChild<QPushButton *>("openReleases"))
        releases->setText(releases->property("automaticUpdate").toBool() ? tr("立即更新") : tr("打开发布页"));
    if (auto cancelUpdate = findChild<QPushButton *>("cancelUpdate"))
        cancelUpdate->setText(tr("取消"));
    if (auto feedback = findChild<QLabel *>("aboutFeedback"))
        feedback->setText(tr("也可以加入 QQ 群 1018416966（反馈信息专用）；加群时请说明来自 GitHub。"));
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
        diagnostics::write(diagnostics::Level::Error, "settings.language", "Interface translation could not be loaded");
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
    updater_->cancelDownload();
    // Cancelling undoes the live language preview along with every other edit.
    applyLanguage(languageOnEntry_);
    QDialog::reject();
}
void SettingsDialog::showUpdates(bool checkNow) {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsAboutPage"));
    if (checkNow)
        QTimer::singleShot(0, findChild<QPushButton *>("checkUpdates"), &QPushButton::click);
}
void SettingsDialog::offerUpdate(const UpdateChecker::Result &result, bool installNow) {
    updateResult_ = result;
    showUpdates();
    findChild<QLabel *>("updateStatus")->setText(result.message);
    findChild<QPushButton *>("checkUpdates")->setEnabled(true);
    auto releases = findChild<QPushButton *>("openReleases");
    releases->setProperty("updateAvailable", result.status == UpdateChecker::Available);
    releases->setProperty("releaseUrl", result.url);
    const bool automatic = UpdateChecker::canAutoInstall(result, installedCopy(), windowsUpdates);
    releases->setProperty("automaticUpdate", automatic);
    releases->setText(automatic ? tr("立即更新") : tr("打开发布页"));
    // Use the release already checked at startup, including its matched package
    // and checksum; do not start a second GitHub/Gitee query.
    if (installNow && result.status == UpdateChecker::Available)
        QTimer::singleShot(0, releases, &QPushButton::click);
}
void SettingsDialog::showToolbar() {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsToolbarPage"));
}
void SettingsDialog::showAi() {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsAiPage"));
}
void SettingsDialog::setLaunchAtLoginNotice(const QString &notice) {
    launchAtLoginNotice_->setText(notice);
    launchAtLoginNotice_->setVisible(!notice.isEmpty());
}
void SettingsDialog::setDraft(const AppSettings &settings) {
    aiPage_->setSettings(settings);
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
    legacyEmbedOriginal_ = settings.embedOriginal;
    embedFeedbackImage_->setChecked(settings.imageExportMode == ImageExportMode::Embedded);
    copyJsonTextWithImage_->setChecked(settings.copyJsonTextWithImage);
    legacyIncludeFeedbackPreview_ = settings.includeFeedbackPreview;
    includeRecognizedText_->setChecked(settings.includeRecognizedText);
    includeWindowTitle_->setChecked(settings.includeWindowTitle);
    confirmBeforeDiscard_->setChecked(settings.confirmBeforeDiscard);
    checkUpdatesOnStartup_->setChecked(settings.checkUpdatesOnStartup);
    feedbackDir_->setText(QDir::toNativeSeparators(settings.feedbackDir));
    scrollAxis_->setCurrentIndex(scrollAxis_->findData(static_cast<int>(settings.scrollAxis)));
    scrollAutoCrop_->setChecked(settings.scrollAutoCrop);
    scrollUltraLong_->setChecked(settings.scrollUltraLong);
    defaultTool_->setCurrentIndex(settings.defaultTool);
}
AppSettings SettingsDialog::settings() const {
    AppSettings result;
    aiPage_->applyTo(result);
    result.captureStyle = captureStyle_;
    result.captureOnStartup = captureOnStartup_->isChecked();
    result.launchAtLogin = launchAtLogin_->isChecked();
    result.fitImageOnOpen = fitImageOnOpen_->isChecked();
    result.imageExportMode = embedFeedbackImage_->isChecked() ? ImageExportMode::Embedded : ImageExportMode::File;
    result.copyJsonTextWithImage = copyJsonTextWithImage_->isChecked();
    result.embedOriginal = legacyEmbedOriginal_;
    result.includeFeedbackPreview = legacyIncludeFeedbackPreview_;
    result.includeRecognizedText = includeRecognizedText_->isChecked();
    result.includeWindowTitle = includeWindowTitle_->isChecked();
    result.confirmBeforeDiscard = confirmBeforeDiscard_->isChecked();
    result.checkUpdatesOnStartup = checkUpdatesOnStartup_->isChecked();
    result.feedbackDir = QDir::fromNativeSeparators(feedbackDir_->text().trimmed());
    result.scrollAxis = static_cast<Qt::Orientation>(scrollAxis_->currentData().toInt());
    result.scrollAutoCrop = scrollAutoCrop_->isChecked();
    result.scrollUltraLong = scrollUltraLong_->isChecked();
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
        diagnostics::write(diagnostics::Level::Error, "settings.apply", error);
        error_->setText(error);
        error_->show();
        return;
    }
    accept();
}
} // namespace h2d
