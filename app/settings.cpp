#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
namespace h2d {
namespace {
// Raised whenever the look a capture starts with changes, so a settings file written
// before the change is not mistaken for one that asked for the old look.
constexpr int kCaptureStyleVersion = 1;
// Free functions have no tr(); the enclosing "h2d" context groups them so the
// translation file stays easy to review.
inline QString tr(const char *text) {
    return QCoreApplication::translate("h2d", text);
}
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
QString languageName(LanguageMode mode) {
    switch (mode) {
    case LanguageMode::SimplifiedChinese:
        return "zh_CN";
    case LanguageMode::English:
        return "en";
    case LanguageMode::System:
        return "system";
    }
    return {};
}
bool parseLanguage(const QString &name, LanguageMode *mode) {
    if (name == "system")
        *mode = LanguageMode::System;
    else if (name == "zh_CN")
        *mode = LanguageMode::SimplifiedChinese;
    else if (name == "en")
        *mode = LanguageMode::English;
    else
        return false;
    return true;
}
// The recogniser and the interface use the same three choices, so they are stored
// under the same names and only the type differs.
QString languageName(OcrLanguageMode mode) {
    return languageName(static_cast<LanguageMode>(mode));
}
QString validateToolbarActions(const QStringList &actions) {
    QSet<QString> known;
    for (const auto &definition : toolbarActionDefinitions())
        known.insert(definition.id);
    QSet<QString> selected;
    for (const auto &id : actions) {
        if (!known.contains(id))
            return tr("包含无法识别的工具栏操作。");
        if (selected.contains(id))
            return tr("工具栏操作不能重复。");
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
    return {{"capture", tr("截图"), true, QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_2)},
            {"annotate", tr("新建批注（空窗口）"), true, QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_1)},
            {"open", tr("打开图片或项目"), false, QKeySequence(QKeySequence::Open)},
            {"paste", tr("粘贴图片"), false, QKeySequence(QKeySequence::Paste)},
            {"save", tr("保存项目"), false, QKeySequence(QKeySequence::Save)},
            {"export", tr("查看 JSON"), false, QKeySequence(Qt::CTRL | Qt::Key_E)},
            {"copy", tr("复制带批注图片"), false, QKeySequence(QKeySequence::Copy)},
            {"copyJson", tr("复制 JSON 文件"), false, {}},
            {"saveImage", tr("保存图片"), false, {}},
            {"hideAnnotations", tr("隐藏 / 显示批注标记"), false, {}},
            {"addGlobalNote", tr("添加全局批注"), false, {}},
            {"undo", tr("撤销"), false, QKeySequence(QKeySequence::Undo)},
            {"redo", tr("重做"), false, QKeySequence(QKeySequence::Redo)},
            {"fit", tr("适应窗口"), false, QKeySequence(Qt::CTRL | Qt::Key_0)},
            {"delete", tr("删除所选批注"), false, QKeySequence(Qt::Key_Delete)},
            {"close", tr("取消操作 / 关闭截图"), false, QKeySequence(Qt::Key_Escape)},
            {"smart", tr("智能选块"), false, QKeySequence(Qt::Key_B)},
            {"point", tr("点标注"), false, QKeySequence(Qt::Key_P)},
            {"rectangle", tr("框选标注"), false, QKeySequence(Qt::Key_R)},
            {"adjust", tr("调整批注"), false, QKeySequence(Qt::Key_V)},
            {"explode", tr("切换大爆炸"), false, QKeySequence(Qt::Key_E)},
            {"component", tr("调整组件"), false, QKeySequence(Qt::Key_M)}};
}
QVector<ToolbarActionDefinition> toolbarActionDefinitions() {
    return {{"saveProject", tr("保存项目")},
            {"saveImage", tr("保存图片")},
            {"exportJson", tr("查看 JSON")},
            {"copyJsonText", tr("复制 JSON 内容")},
            {"copyJson", tr("复制 JSON 文件")},
            {"copyImage", tr("复制带批注图片")},
            {"capture", tr("重新截图")},
            {"fit", tr("适应图片")}};
}
AppSettings defaultSettings() {
    AppSettings settings;
    for (const auto &definition : shortcutDefinitions())
        settings.shortcuts.insert(definition.id, definition.defaultKey);
    return settings;
}
QString validateSettings(const AppSettings &settings) {
    if (themeName(settings.theme).isEmpty())
        return tr("请选择有效的外观模式。");
    if (languageName(settings.language).isEmpty())
        return tr("请选择有效的界面语言。");
    if (settings.defaultTool < 0 || settings.defaultTool > 3)
        return tr("请选择有效的默认标注工具。");
    const auto toolbarError = validateToolbarActions(settings.toolbarActions);
    if (!toolbarError.isEmpty())
        return toolbarError;
    if (!settings.feedbackDir.isEmpty()) {
        const QFileInfo info(settings.feedbackDir);
        if (!info.isAbsolute())
            return tr("反馈临时目录必须是绝对路径。");
        if (info.isFile())
            return tr("反馈临时目录不能指向一个已有文件。");
    }
    if (settings.scrollAxis != Qt::Vertical && settings.scrollAxis != Qt::Horizontal)
        return tr("请选择有效的长截图方向。");
    QMap<int, QString> assigned;
    const auto definitions = shortcutDefinitions();
    for (const auto &definition : definitions) {
        if (!settings.shortcuts.contains(definition.id))
            return tr("快捷键配置不完整，请恢复默认后重试。");
        const auto sequence = settings.shortcuts.value(definition.id);
        if (sequence.isEmpty())
            continue;
        if (sequence.count() != 1 || !validCombination(sequence[0]))
            return tr("“%1”只支持一个有效的组合键。").arg(definition.label);
        const auto combination = sequence[0];
        const auto key = combination.key();
        const auto modifiers = combination.keyboardModifiers();
        if (definition.global && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            !(key >= Qt::Key_F1 && key <= Qt::Key_F24) && key != Qt::Key_Print)
            return tr("“%1”快捷键需要包含 Ctrl、Alt 或 Command / Win，或使用 F1–F24、Print Screen。")
                .arg(definition.label);
        const auto code = combination.toCombined();
        if (assigned.contains(code))
            return tr("“%1”和“%2”使用了相同的快捷键 %3。")
                .arg(assigned.value(code), definition.label, sequence.toString(QKeySequence::NativeText));
        assigned.insert(code, definition.label);
    }
    if (settings.shortcuts.size() != definitions.size())
        return tr("包含无法识别的快捷键配置。");
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
    // A preference added in a later release starts from its default on upgrade, so
    // a missing key is fine while a malformed one still resets the whole file.
    if (source.contains("appearance/language") &&
        !parseLanguage(source.value("appearance/language").toString(), &result.language))
        return defaultSettings();
    // The recogniser language arrived after the first settings file did, so a
    // value that cannot be read leaves the default in place instead of resetting
    // everything the user configured.
    const auto ocr = source.value("appearance/ocrLanguage", "system").toString();
    if (ocr == "zh_CN")
        result.ocrLanguage = OcrLanguageMode::SimplifiedChinese;
    else if (ocr == "en")
        result.ocrLanguage = OcrLanguageMode::English;
    else if (ocr == "system")
        result.ocrLanguage = OcrLanguageMode::System;
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
    result.launchAtLogin = boolean("defaults/launchAtLogin", false);
    result.fitImageOnOpen = boolean("defaults/fitImageOnOpen", true);
    result.embedOriginal = boolean("defaults/embedOriginal", true);
    result.confirmBeforeDiscard = boolean("defaults/confirmBeforeDiscard", true);
    result.checkUpdatesOnStartup = boolean("updates/checkOnStartup", true);
    const QString scrollAxis = source.value("capture/scrollAxis", QStringLiteral("vertical")).toString();
    result.scrollAxis = scrollAxis == QLatin1String("horizontal") ? Qt::Horizontal : Qt::Vertical;
    result.scrollAutoCrop = boolean("capture/scrollAutoCrop", false);
    result.scrollUltraLong = boolean("capture/scrollUltraLong", false);
    // The capture style arrived after the first settings file did, so a missing key
    // means "the built-in look" rather than "the user turned it off".
    bool numeric = false;
    const auto radius = source.value("capture/cornerRadius", result.captureStyle.cornerRadius).toInt(&numeric);
    if (numeric && radius >= 0 && radius <= 100)
        result.captureStyle.cornerRadius = radius;
    const auto borderWidth =
        source.value("capture/borderWidth", result.captureStyle.borderWidth).toInt(&numeric);
    if (numeric && borderWidth >= 0 && borderWidth <= 100)
        result.captureStyle.borderWidth = borderWidth;
    const auto strength =
        source.value("capture/shadowStrength", result.captureStyle.shadowStrength).toInt(&numeric);
    if (numeric && strength >= 0 && strength <= 100)
        result.captureStyle.shadowStrength = strength;
    result.captureStyle.border = boolean("capture/border", result.captureStyle.border);
    const QColor borderColor(source.value("capture/borderColor").toString());
    if (borderColor.isValid())
        result.captureStyle.borderColor = borderColor;
    const QColor shadowColor(source.value("capture/shadowColor").toString());
    if (shadowColor.isValid())
        result.captureStyle.shadowColor = shadowColor;
    // The shadow used to start switched off, and settings files written back then say
    // so. That "off" was never a choice anybody made, so it is only honoured from a
    // file that was written after the default changed; without this marker an upgrade
    // would quietly keep the old look and the shadow would stay invisible.
    const int styleVersion = source.value("capture/styleVersion", 0).toInt(&numeric);
    if (numeric && styleVersion >= 1)
        result.captureStyle.shadow = boolean("capture/shadow", result.captureStyle.shadow);
    if (source.contains("defaults/feedbackDir"))
        result.feedbackDir = source.value("defaults/feedbackDir").toString();
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
    // An upgrade must not replace the user's settings if an existing shortcut
    // already uses the new annotation default. They can choose another key later.
    if (!source.contains("shortcuts/annotate")) {
        const auto annotationKey = result.shortcuts.value("annotate");
        for (auto it = result.shortcuts.cbegin(); it != result.shortcuts.cend(); ++it) {
            if (it.key() != "annotate" && it.value() == annotationKey) {
                result.shortcuts["annotate"] = {};
                break;
            }
        }
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
            *error = tr("无法创建设置目录。");
        return false;
    }
    QSettings target(path, QSettings::IniFormat);
    target.setAtomicSyncRequired(true);
    target.setValue("version", 1);
    target.setValue("defaults/captureOnStartup", settings.captureOnStartup);
    target.setValue("defaults/launchAtLogin", settings.launchAtLogin);
    target.setValue("defaults/fitImageOnOpen", settings.fitImageOnOpen);
    target.setValue("defaults/embedOriginal", settings.embedOriginal);
    target.setValue("defaults/confirmBeforeDiscard", settings.confirmBeforeDiscard);
    target.setValue("defaults/tool", settings.defaultTool);
    target.setValue("updates/checkOnStartup", settings.checkUpdatesOnStartup);
    target.setValue("defaults/feedbackDir", settings.feedbackDir);
    target.setValue("capture/scrollAxis", settings.scrollAxis == Qt::Horizontal ? "horizontal" : "vertical");
    target.setValue("capture/scrollAutoCrop", settings.scrollAutoCrop);
    target.setValue("capture/scrollUltraLong", settings.scrollUltraLong);
    target.setValue("appearance/theme", themeName(settings.theme));
    target.setValue("appearance/language", languageName(settings.language));
    target.setValue("appearance/ocrLanguage", languageName(settings.ocrLanguage));
    // The capture style is written as plain numbers so a settings file stays readable
    // and hand-editable, the same way every other key in it is.
    target.setValue("capture/cornerRadius", settings.captureStyle.cornerRadius);
    target.setValue("capture/border", settings.captureStyle.border);
    target.setValue("capture/borderWidth", settings.captureStyle.borderWidth);
    target.setValue("capture/borderColor", settings.captureStyle.borderColor.name(QColor::HexArgb));
    target.setValue("capture/shadow", settings.captureStyle.shadow);
    target.setValue("capture/shadowStrength", settings.captureStyle.shadowStrength);
    target.setValue("capture/shadowColor", settings.captureStyle.shadowColor.isValid()
        ? settings.captureStyle.shadowColor.name(QColor::HexArgb) : QString());
    // Written so a later change of the built-in look can tell a file that never had a
    // say from one that did.
    target.setValue("capture/styleVersion", kCaptureStyleVersion);
    target.setValue("toolbar/actions", settings.toolbarActions);
    for (const auto &definition : shortcutDefinitions())
        target.setValue("shortcuts/" + definition.id,
                        settings.shortcuts.value(definition.id).toString(QKeySequence::PortableText));
    target.sync();
    if (target.status() != QSettings::NoError) {
        if (error)
            *error = tr("无法保存设置，请检查配置文件的访问权限。");
        return false;
    }
    if (error)
        error->clear();
    return true;
}
bool hasSeenGuide(const QString &filePath) {
    QSettings source(settingsPath(filePath), QSettings::IniFormat);
    const auto value = source.value("onboarding/seen").toString().toLower();
    return source.status() == QSettings::NoError && (value == "true" || value == "1");
}
bool markGuideSeen(QString *error, const QString &filePath) {
    const auto path = settingsPath(filePath);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = tr("无法创建设置目录。");
        return false;
    }
    QSettings target(path, QSettings::IniFormat);
    target.setAtomicSyncRequired(true);
    target.setValue("onboarding/seen", true);
    target.sync();
    if (target.status() != QSettings::NoError) {
        if (error)
            *error = tr("无法保存引导状态，请检查配置文件的访问权限。");
        return false;
    }
    if (error)
        error->clear();
    return true;
}
} // namespace h2d
