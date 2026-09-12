#include "settings.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
namespace h2d {
namespace {
QString settingsPath(const QString &filePath) {
    if (!filePath.isEmpty())
        return filePath;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("settings.ini");
}
QString themeName(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return "light";
    case ThemeMode::Dark:
        return "dark";
    case ThemeMode::System:
        return "system";
    }
    return {};
}
QString validateToolbarActions(const QStringList &actions) {
    QSet<QString> known;
    for (const auto &definition : toolbarActionDefinitions())
        known.insert(definition.id);
    QSet<QString> selected;
    for (const auto &id : actions) {
        if (!known.contains(id))
            return "包含无法识别的工具栏操作。";
        if (selected.contains(id))
            return "工具栏操作不能重复。";
        selected.insert(id);
    }
    return {};
}
bool validCombination(QKeyCombination combination) {
    const auto key = combination.key();
    return key != Qt::Key_unknown && key != 0 && key != Qt::Key_Shift && key != Qt::Key_Control &&
           key != Qt::Key_Alt && key != Qt::Key_Meta && key != Qt::Key_AltGr;
}
} // namespace
QVector<ShortcutDefinition> shortcutDefinitions() {
    return {{"capture", "截图", true, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2)},
            {"open", "打开图片或项目", false, QKeySequence(QKeySequence::Open)},
            {"paste", "粘贴图片", false, QKeySequence(QKeySequence::Paste)},
            {"save", "保存项目", false, QKeySequence(QKeySequence::Save)},
            {"export", "查看 JSON", false, QKeySequence(Qt::CTRL | Qt::Key_E)},
            {"copy", "复制带批注图片", false, QKeySequence(QKeySequence::Copy)},
            {"copyJson", "复制 JSON", false, {}},
            {"saveImage", "保存图片", false, {}},
            {"hideAnnotations", "隐藏 / 显示批注标记", false, {}},
            {"addGlobalNote", "添加全局批注", false, {}},
            {"undo", "撤销", false, QKeySequence(QKeySequence::Undo)},
            {"redo", "重做", false, QKeySequence(QKeySequence::Redo)},
            {"fit", "适应窗口", false, QKeySequence(Qt::CTRL | Qt::Key_0)},
            {"delete", "删除所选批注", false, QKeySequence(Qt::Key_Delete)},
            {"close", "取消操作 / 关闭截图", false, QKeySequence(Qt::Key_Escape)},
            {"smart", "智能选块", false, QKeySequence(Qt::Key_B)},
            {"point", "点标注", false, QKeySequence(Qt::Key_P)},
            {"rectangle", "框选标注", false, QKeySequence(Qt::Key_R)},
            {"adjust", "调整批注", false, QKeySequence(Qt::Key_V)},
            {"explode", "切换大爆炸", false, QKeySequence(Qt::Key_E)},
            {"component", "调整组件", false, QKeySequence(Qt::Key_M)}};
}
QVector<ToolbarActionDefinition> toolbarActionDefinitions() {
    return {{"saveProject", "保存项目"},
            {"saveImage", "保存图片"},
            {"exportJson", "查看 JSON"},
            {"copyJson", "复制 JSON"},
            {"copyImage", "复制带批注图片"},
            {"capture", "重新截图"},
            {"fit", "适应图片"}};
}
AppSettings defaultSettings() {
    AppSettings settings;
    for (const auto &definition : shortcutDefinitions())
        settings.shortcuts.insert(definition.id, definition.defaultKey);
    return settings;
}
QString validateSettings(const AppSettings &settings) {
    if (themeName(settings.theme).isEmpty())
        return "请选择有效的外观模式。";
    if (settings.defaultTool < 0 || settings.defaultTool > 3)
        return "请选择有效的默认标注工具。";
    const auto toolbarError = validateToolbarActions(settings.toolbarActions);
    if (!toolbarError.isEmpty())
        return toolbarError;
    QMap<int, QString> assigned;
    const auto definitions = shortcutDefinitions();
    for (const auto &definition : definitions) {
        if (!settings.shortcuts.contains(definition.id))
            return "快捷键配置不完整，请恢复默认后重试。";
        const auto sequence = settings.shortcuts.value(definition.id);
        if (sequence.isEmpty())
            continue;
        if (sequence.count() != 1 || !validCombination(sequence[0]))
            return QString("“%1”只支持一个有效的组合键。").arg(definition.label);
        const auto combination = sequence[0];
        const auto key = combination.key();
        const auto modifiers = combination.keyboardModifiers();
        if (definition.global && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            !(key >= Qt::Key_F1 && key <= Qt::Key_F24) && key != Qt::Key_Print)
            return "截图快捷键需要包含 Ctrl、Alt 或 Command / Win，或使用 F1–F24、Print Screen。";
        const auto code = combination.toCombined();
        if (assigned.contains(code))
            return QString("“%1”和“%2”使用了相同的快捷键 %3。")
                .arg(assigned.value(code), definition.label, sequence.toString(QKeySequence::NativeText));
        assigned.insert(code, definition.label);
    }
    if (settings.shortcuts.size() != definitions.size())
        return "包含无法识别的快捷键配置。";
    return {};
}
AppSettings loadSettings(const QString &filePath) {
    auto result = defaultSettings();
    QSettings source(settingsPath(filePath), QSettings::IniFormat);
    if (source.status() != QSettings::NoError)
        return result;
    bool validVersion = false;
    const int version = source.value("version", 1).toInt(&validVersion);
    if (!validVersion || version != 1)
        return result;
    const auto theme = source.value("appearance/theme", "system").toString();
    if (theme == "light")
        result.theme = ThemeMode::Light;
    else if (theme == "dark")
        result.theme = ThemeMode::Dark;
    else if (theme != "system")
        return defaultSettings();
    // Missing or malformed new preferences keep their individual defaults on upgrade.
    auto boolean = [&](const QString &key, bool fallback) {
        const auto value = source.value(key).toString().toLower();
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        return fallback;
    };
    result.captureOnStartup = boolean("defaults/captureOnStartup", true);
    result.fitImageOnOpen = boolean("defaults/fitImageOnOpen", true);
    result.embedOriginal = boolean("defaults/embedOriginal", true);
    result.checkUpdatesOnStartup = boolean("updates/checkOnStartup", false);
    if (source.contains("toolbar/actions")) {
        const auto actions = source.value("toolbar/actions").toStringList();
        if (validateToolbarActions(actions).isEmpty())
            result.toolbarActions = actions;
    }
    bool validTool = false;
    const auto tool = source.value("defaults/tool", 0).toInt(&validTool);
    if (validTool && tool >= 0 && tool <= 3)
        result.defaultTool = tool;
    for (const auto &definition : shortcutDefinitions()) {
        const auto key = "shortcuts/" + definition.id;
        if (!source.contains(key))
            continue;
        const auto text = source.value(key).toString();
        const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!text.isEmpty() && sequence.isEmpty())
            return defaultSettings();
        result.shortcuts[definition.id] = sequence;
    }
    if (source.status() != QSettings::NoError || !validateSettings(result).isEmpty())
        return defaultSettings();
    return result;
}
bool saveSettings(const AppSettings &settings, QString *error, const QString &filePath) {
    const auto invalid = validateSettings(settings);
    if (!invalid.isEmpty()) {
        if (error)
            *error = invalid;
        return false;
    }
    const auto path = settingsPath(filePath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = "无法创建设置目录。";
        return false;
    }
    QSettings target(path, QSettings::IniFormat);
    target.setAtomicSyncRequired(true);
    target.setValue("version", 1);
    target.setValue("defaults/captureOnStartup", settings.captureOnStartup);
    target.setValue("defaults/fitImageOnOpen", settings.fitImageOnOpen);
    target.setValue("defaults/embedOriginal", settings.embedOriginal);
    target.setValue("defaults/tool", settings.defaultTool);
    target.setValue("updates/checkOnStartup", settings.checkUpdatesOnStartup);
    target.setValue("appearance/theme", themeName(settings.theme));
    target.setValue("toolbar/actions", settings.toolbarActions);
    for (const auto &definition : shortcutDefinitions())
        target.setValue("shortcuts/" + definition.id,
                        settings.shortcuts.value(definition.id).toString(QKeySequence::PortableText));
    target.sync();
    if (target.status() != QSettings::NoError) {
        if (error)
            *error = "无法保存设置，请检查配置文件的访问权限。";
        return false;
    }
    if (error)
        error->clear();
    return true;
}
} // namespace h2d
