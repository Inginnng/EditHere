#pragma once
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QVector>
namespace h2d {
enum class ThemeMode { System, Light, Dark };
struct AppSettings {
    ThemeMode theme = ThemeMode::System;
    QMap<QString, QKeySequence> shortcuts;
    bool captureOnStartup = true;
    bool fitImageOnOpen = true;
    bool embedOriginal = true;
    bool checkUpdatesOnStartup = false;
    int defaultTool = 0; // Canvas::Smart, Point, Rectangle, Adjust.
    bool operator==(const AppSettings &) const = default;
};
struct ShortcutDefinition {
    QString id, label;
    bool global = false;
    QKeySequence defaultKey;
};
QVector<ShortcutDefinition> shortcutDefinitions();
AppSettings defaultSettings();
AppSettings loadSettings(const QString &filePath = {});
bool saveSettings(const AppSettings &settings, QString *error = nullptr, const QString &filePath = {});
QString validateSettings(const AppSettings &settings);
} // namespace h2d
