#include "settingsdialog.h"
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
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
namespace h2d {
SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent) : QDialog(parent) {
    setObjectName("settingsDialog");
    setWindowTitle("HelpDesign 设置");
    setMinimumSize(500, 420);
    resize(620, 640);
    auto root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);
    auto title = new QLabel("设置", this);
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
    auto explanation = mutedLabel("点击组合键后按下新的快捷键。清空即停用。", shortcuts);
    explanation->setWordWrap(true);
    shortcutLayout->addWidget(explanation);
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
            auto label = new QLabel("全局快捷键", rows);
            label->setObjectName("settingsSection");
            form->addRow(label);
            globalSection = true;
        } else if (!definition.global && !localSection) {
            auto label = new QLabel("应用内快捷键", rows);
            label->setObjectName("settingsSection");
            form->addRow(label);
            localSection = true;
        }
        auto input = new QKeySequenceEdit(rows);
        input->setObjectName("shortcut_" + definition.id);
        input->setMaximumSequenceLength(1);
        input->setClearButtonEnabled(true);
        input->setAttribute(Qt::WA_StyledBackground);
        if (auto field = input->findChild<QLineEdit *>())
            field->setPlaceholderText("点击录制快捷键");
        for (auto clear : input->findChildren<QToolButton *>())
            clear->setIcon(glyph("close"));
        input->setMinimumWidth(200);
        input->setAccessibleName(definition.label + "快捷键");
        input->setToolTip(definition.global ? "在其他应用中也可使用；清空则停用。"
                                            : "在 HelpDesign 编辑窗口中使用；清空则停用。");
        keys_.insert(definition.id, input);
        form->addRow(definition.label, input);
        connect(input, &QKeySequenceEdit::keySequenceChanged, this, [this] { error_->hide(); });
    }
    scroll->setWidget(rows);
    shortcutLayout->addWidget(scroll, 1);
    tabs->addTab(shortcuts, "快捷键");
    auto appearance = new QWidget;
    auto appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->setContentsMargins(20, 22, 20, 20);
    appearanceLayout->setSpacing(14);
    auto appearanceTitle = new QLabel("外观模式", appearance);
    appearanceTitle->setObjectName("settingsSection");
    appearanceLayout->addWidget(appearanceTitle);
    auto themeDescription = mutedLabel("选择适合你的界面。跟随系统会随系统外观自动切换。", appearance);
    themeDescription->setWordWrap(true);
    appearanceLayout->addWidget(themeDescription);
    theme_ = new QComboBox(appearance);
    theme_->setObjectName("themeMode");
    theme_->setAccessibleName("外观模式");
    theme_->addItem("跟随系统", static_cast<int>(ThemeMode::System));
    theme_->addItem("亮色", static_cast<int>(ThemeMode::Light));
    theme_->addItem("暗色", static_cast<int>(ThemeMode::Dark));
    theme_->setMinimumHeight(36);
    appearanceLayout->addWidget(theme_);
    appearanceLayout->addStretch();
    tabs->addTab(appearance, "外观");
    auto defaults = new QWidget;
    auto defaultsLayout = new QVBoxLayout(defaults);
    defaultsLayout->setContentsMargins(20, 22, 20, 20);
    defaultsLayout->setSpacing(18);
    auto defaultsDescription = mutedLabel("为下一次截图和导出设置习惯。不会改变正在编辑的内容。", defaults);
    defaultsDescription->setWordWrap(true);
    defaultsLayout->addWidget(defaultsDescription);
    captureOnStartup_ = new QCheckBox("启动后立即截图", defaults);
    captureOnStartup_->setObjectName("captureOnStartup");
    captureOnStartup_->setToolTip("关闭后启动时只驻留托盘；点击托盘或按全局快捷键开始截图。");
    fitImageOnOpen_ = new QCheckBox("打开图片时自动适应窗口", defaults);
    fitImageOnOpen_->setObjectName("fitImageOnOpen");
    fitImageOnOpen_->setToolTip("关闭后以 100% 显示，仍可随时缩放或使用适应窗口。");
    embedOriginal_ = new QCheckBox("导出 JSON 默认包含原图", defaults);
    embedOriginal_->setObjectName("defaultEmbedOriginal");
    defaultsLayout->addWidget(captureOnStartup_);
    defaultsLayout->addWidget(fitImageOnOpen_);
    defaultsLayout->addWidget(embedOriginal_);
    auto exportHint = mutedLabel("包含原图的 JSON 可独立还原。保存项目始终包含原图。", defaults);
    exportHint->setWordWrap(true);
    defaultsLayout->addWidget(exportHint);
    auto toolForm = new QFormLayout;
    defaultTool_ = new QComboBox(defaults);
    defaultTool_->setObjectName("defaultTool");
    defaultTool_->addItems({"智能选块", "点标注", "框选标注", "调整批注"});
    defaultTool_->setAccessibleName("默认标注工具");
    toolForm->addRow("默认标注工具", defaultTool_);
    defaultsLayout->addLayout(toolForm);
    defaultsLayout->addStretch();
    tabs->addTab(defaults, "默认行为");

    auto toolbar = new QWidget;
    toolbar->setObjectName("settingsToolbarPage");
    auto toolbarLayout = new QVBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(20, 22, 20, 20);
    toolbarLayout->setSpacing(18);
    auto toolbarTitle = new QLabel("显示在底部工具栏", toolbar);
    toolbarTitle->setObjectName("settingsSection");
    toolbarLayout->addWidget(toolbarTitle);
    auto toolbarDescription = mutedLabel("标注、调整和缩放工具始终保留。勾选常用操作，保存后立即显示在工具栏。", toolbar);
    toolbarDescription->setWordWrap(true);
    toolbarLayout->addWidget(toolbarDescription);
    for (const auto &definition : toolbarActionDefinitions()) {
        auto checkbox = new QCheckBox(definition.label, toolbar);
        checkbox->setObjectName("toolbar_" + definition.id);
        checkbox->setAccessibleName("在工具栏显示" + definition.label);
        if (definition.id == "saveImage")
            checkbox->setToolTip("保存包含批注和布局调整的图片。");
        else if (definition.id == "exportJson")
            checkbox->setToolTip("打开 JSON 预览，可查看、复制或保存文件。");
        else if (definition.id == "copyJson")
            checkbox->setToolTip("将 JSON 直接复制到剪贴板。");
        toolbarActions_.insert(definition.id, checkbox);
        toolbarLayout->addWidget(checkbox);
        connect(checkbox, &QCheckBox::toggled, this, [this] { error_->hide(); });
    }
    auto toolbarHint = mutedLabel("未勾选的操作会收进“更多”菜单，仍可使用快捷键。", toolbar);
    toolbarHint->setWordWrap(true);
    toolbarLayout->addWidget(toolbarHint);
    toolbarLayout->addStretch();
    tabs->addTab(toolbar, "工具栏");

    auto about = new QWidget;
    about->setObjectName("settingsAboutPage");
    auto aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(20, 22, 20, 20);
    aboutLayout->setSpacing(18);
    auto version = new QLabel("HelpDesign  " HELPDESIGN_VERSION, about);
    version->setObjectName("settingsSection");
    aboutLayout->addWidget(version);
    auto description = mutedLabel("截图、批注与布局调整，让设计修改意见更清楚。", about);
    description->setWordWrap(true);
    aboutLayout->addWidget(description);
    checkUpdatesOnStartup_ = new QCheckBox("启动时检查更新", about);
    checkUpdatesOnStartup_->setObjectName("checkUpdatesOnStartup");
    aboutLayout->addWidget(checkUpdatesOnStartup_);
    auto updateHint = mutedLabel(
        "仅检查正式版，有更新时提醒。不会自动下载或安装。检查只访问 GitHub，不上传截图或批注。", about);
    updateHint->setWordWrap(true);
    aboutLayout->addWidget(updateHint);
    auto status = new QLabel("尚未检查更新。", about);
    status->setObjectName("updateStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    aboutLayout->addWidget(status);
    auto check = textButton("检查更新", true, about);
    check->setObjectName("checkUpdates");
    auto releases = textButton("打开发布页", false, about);
    releases->setObjectName("openReleases");
    auto updateActions = new QHBoxLayout;
    updateActions->addWidget(check);
    updateActions->addWidget(releases);
    updateActions->addStretch();
    aboutLayout->addLayout(updateActions);
    auto privateHint = mutedLabel("私有仓库需要访问权限。若已安装 GitHub CLI "
                                  "并登录，会使用其现有登录状态；否则可在浏览器登录后查看发布页。",
                                  about);
    privateHint->setWordWrap(true);
    aboutLayout->addWidget(privateHint);
    aboutLayout->addStretch();
    tabs->addTab(about, "关于与更新");
    updater_ = new UpdateChecker(this);
    connect(check, &QPushButton::clicked, this, [this, status, check] {
        status->setText("正在检查更新…");
        check->setEnabled(false);
        updater_->check();
    });
    connect(updater_, &UpdateChecker::finished, this,
            [status, check, releases](UpdateChecker::Status state, const QString &message, const QUrl &url) {
                status->setText(message);
                check->setEnabled(true);
                releases->setText(state == UpdateChecker::Available ? "前往下载" : "打开发布页");
                releases->setProperty("releaseUrl", url);
            });
    connect(releases, &QPushButton::clicked, this, [releases, status] {
        const auto url = releases->property("releaseUrl").toUrl();
        if (!QDesktopServices::openUrl(url.isEmpty() ? UpdateChecker::releasesUrl() : url))
            status->setText("无法打开浏览器，请访问 github.com/Inginnng/HelpDesign/releases。");
    });
    error_ = new QLabel(this);
    error_->setObjectName("errorLabel");
    error_->setProperty("error", true);
    error_->setWordWrap(true);
    error_->hide();
    root->addWidget(error_);
    auto actions = new QHBoxLayout;
    auto reset = textButton("恢复默认", false, this);
    reset->setObjectName("settingsReset");
    auto cancel = textButton("取消", false, this);
    cancel->setObjectName("settingsCancel");
    auto saveButton = textButton("保存", true, this);
    saveButton->setObjectName("settingsSave");
    saveButton->setDefault(true);
    actions->addWidget(reset);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(saveButton);
    root->addLayout(actions);
    connect(reset, &QPushButton::clicked, this, [this] {
        setDraft(defaultSettings());
        error_->hide();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] { error_->hide(); });
    setDraft(settings);
}
void SettingsDialog::showUpdates(bool checkNow) {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsAboutPage"));
    if (checkNow)
        QTimer::singleShot(0, findChild<QPushButton *>("checkUpdates"), &QPushButton::click);
}
void SettingsDialog::showToolbar() {
    tabs_->setCurrentWidget(findChild<QWidget *>("settingsToolbarPage"));
}
void SettingsDialog::setDraft(const AppSettings &settings) {
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it)
        it.value()->setKeySequence(settings.shortcuts.value(it.key()));
    for (auto it = toolbarActions_.cbegin(); it != toolbarActions_.cend(); ++it)
        it.value()->setChecked(settings.toolbarActions.contains(it.key()));
    theme_->setCurrentIndex(theme_->findData(static_cast<int>(settings.theme)));
    captureOnStartup_->setChecked(settings.captureOnStartup);
    fitImageOnOpen_->setChecked(settings.fitImageOnOpen);
    embedOriginal_->setChecked(settings.embedOriginal);
    checkUpdatesOnStartup_->setChecked(settings.checkUpdatesOnStartup);
    defaultTool_->setCurrentIndex(settings.defaultTool);
}
AppSettings SettingsDialog::settings() const {
    AppSettings result;
    result.captureOnStartup = captureOnStartup_->isChecked();
    result.fitImageOnOpen = fitImageOnOpen_->isChecked();
    result.embedOriginal = embedOriginal_->isChecked();
    result.checkUpdatesOnStartup = checkUpdatesOnStartup_->isChecked();
    result.defaultTool = defaultTool_->currentIndex();
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
