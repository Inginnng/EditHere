#pragma once
#include "settings.h"
#include <QString>
namespace h2d {
// Name of a language choice as shown in the settings dialog, written in that
// language so it stays readable whatever the interface currently uses.
QString languageDisplayName(LanguageMode mode);

// Locale the choice resolves to, e.g. "zh_CN", "zh_TW" or "en". "System" follows
// the operating system: a traditional-script locale resolves to zh_TW, any other
// Chinese locale to zh_CN, and everything else to English, because Simplified
// Chinese is the built-in source language of the interface.
QString resolveLanguage(LanguageMode mode);

// Locale currently installed by installLanguage(). Empty before startup.
QString currentLanguage();

// Installs the interface and Qt translations for the given choice. Called once
// during startup: the interface is built in C++ constructors rather than .ui
// files, so a language change needs a restart to rebuild every label.
bool installLanguage(LanguageMode mode);

// True when the running interface uses Chinese characters, in either script.
bool isChineseInterface();

// Region labels such as "色块区域" and "整个图片" double as stable identifiers in
// the feedback JSON, so they are never rewritten. Translate them for display only.
QString localizedLabel(const QString &label);
} // namespace h2d
