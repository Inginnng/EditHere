#include "settingsdialog.h"
#include "ui.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
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
    auto tabs = new QTabWidget(this);
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
void SettingsDialog::setDraft(const AppSettings &settings) {
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it)
        it.value()->setKeySequence(settings.shortcuts.value(it.key()));
    theme_->setCurrentIndex(theme_->findData(static_cast<int>(settings.theme)));
}
AppSettings SettingsDialog::settings() const {
    AppSettings result;
    result.theme = static_cast<ThemeMode>(theme_->currentData().toInt());
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
