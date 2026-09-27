#pragma once
#include "settings.h"
#include <QString>
namespace h2d {
// Name of a language choice as shown in the settings dialog, written in that
// language so it stays readable whatever the interface currently uses.
QString languageDisplayName(LanguageMode mode);

// Locale the choice resolves to, e.g. "zh_CN" or "en". "System" follows the
// operating system and falls back to English on non-Chinese systems, because
// Simplified Chinese is the built-in source language of the interface.
QString resolveLanguage(LanguageMode mode);

// Locale currently installed by installLanguage(). Empty before startup.
QString currentLanguage();

// Installs the interface and Qt translations for the given choice. Called once
// during startup: the interface is built in C++ constructors rather than .ui
// files, so a language change needs a restart to rebuild every label.
bool installLanguage(LanguageMode mode);

// True when the running interface is Simplified Chinese.
bool isChineseInterface();

// Region labels such as "色块区域" and "整个图片" double as stable identifiers in
// the feedback JSON, so they are never rewritten. Translate them for display only.
QString localizedLabel(const QString &label);
} // namespace h2d
