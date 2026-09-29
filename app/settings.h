#pragma once
#include "capturesession.h"
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
namespace h2d {
enum class ThemeMode { System, Light, Dark };
// Interface language. "System" follows the operating system and falls back to
// English on non-Chinese systems; the built-in source language is Simplified
// Chinese, so zh_CN needs no translation file.
enum class LanguageMode { System, SimplifiedChinese, English };
// Language the screen reader uses. Kept apart from the interface language,
// because reading a Chinese page on an English interface is a normal thing to do.
enum class OcrLanguageMode { System, SimplifiedChinese, English };
struct AppSettings {
    ThemeMode theme = ThemeMode::System;
    LanguageMode language = LanguageMode::System;
    OcrLanguageMode ocrLanguage = OcrLanguageMode::System;
    QMap<QString, QKeySequence> shortcuts;
    QStringList toolbarActions = {"saveProject", "saveImage", "exportJson", "copyJsonText", "copyJson", "copyImage"};
    bool captureOnStartup = true;
    bool launchAtLogin = false;
    bool fitImageOnOpen = true;
    bool embedOriginal = true;
    // Whether closing the annotation window asks about unsaved changes. Turning it off
    // is what the "don't ask again" box on that question does: from then on closing
    // throws the changes away instead of stopping to ask. It is a preference rather
    // than a one-off so that it can be turned back on in the settings.
    bool confirmBeforeDiscard = true;
    bool checkUpdatesOnStartup = false;
    QString feedbackDir; // Empty = default CacheLocation/feedback.
    int defaultTool = 0; // Canvas::Smart, Point, Rectangle, Adjust.
    // The corner radius, border and shadow every capture starts from. Set from the
    // capture window's style panel, and only when the user asks for it to be kept.
    CaptureStyle captureStyle;
    bool operator==(const AppSettings &) const = default;
};
struct ShortcutDefinition {
    QString id, label;
    bool global = false;
    QKeySequence defaultKey;
};
struct ToolbarActionDefinition {
    QString id, label;
};
QVector<ShortcutDefinition> shortcutDefinitions();
QVector<ToolbarActionDefinition> toolbarActionDefinitions();
AppSettings defaultSettings();
AppSettings loadSettings(const QString &filePath = {});
bool saveSettings(const AppSettings &settings, QString *error = nullptr, const QString &filePath = {});
bool hasSeenGuide(const QString &filePath = {});
bool markGuideSeen(QString *error = nullptr, const QString &filePath = {});
QString validateSettings(const AppSettings &settings);
} // namespace h2d
