#include "i18n.h"
#include "ui.h"
#include <QCoreApplication>
#include <QEvent>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
namespace h2d {
namespace {
QTranslator *appTranslator = nullptr;
QTranslator *qtTranslator = nullptr;
QString installedLocale;

QString systemLocale() {
    const auto languages = QLocale::system().uiLanguages();
    for (const auto &language : languages) {
        const auto id = QLocale(language).name();
        if (id.startsWith("zh"))
            return "zh_CN";
        if (id.startsWith("en"))
            return "en";
    }
    // English is the only fully translated alternative, so it is the safer guess
    // for a system language the interface does not speak.
    return "en";
}

void removeTranslators() {
    if (appTranslator) {
        QCoreApplication::removeTranslator(appTranslator);
        delete appTranslator;
        appTranslator = nullptr;
    }
    if (qtTranslator) {
        QCoreApplication::removeTranslator(qtTranslator);
        delete qtTranslator;
        qtTranslator = nullptr;
    }
}
} // namespace
QString languageDisplayName(LanguageMode mode) {
    switch (mode) {
    case LanguageMode::SimplifiedChinese:
        return QString::fromUtf8("简体中文");
    case LanguageMode::English:
        return QString::fromUtf8("English");
    case LanguageMode::System:
        break;
    }
    return QCoreApplication::translate("h2d", "跟随系统");
}
QString resolveLanguage(LanguageMode mode) {
    switch (mode) {
    case LanguageMode::SimplifiedChinese:
        return "zh_CN";
    case LanguageMode::English:
        return "en";
    case LanguageMode::System:
        break;
    }
    return systemLocale();
}
QString currentLanguage() {
    return installedLocale;
}
bool isChineseInterface() {
    return installedLocale.startsWith("zh");
}
bool installLanguage(LanguageMode mode) {
    removeTranslators();
    installedLocale = resolveLanguage(mode);
    bool available = true;
    if (installedLocale != "zh_CN") {
        // Simplified Chinese is the source language of the interface, so only the
        // other languages need a compiled translation.
        auto translator = new QTranslator;
        // Be explicit about the extension: whether load() appends ".qm" on its own
        // depends on how the file name is passed, so both spellings are attempted.
        if (translator->load(":/i18n/edithere_" + installedLocale + ".qm") ||
            translator->load(":/i18n/edithere_" + installedLocale)) {
            QCoreApplication::installTranslator(translator);
            appTranslator = translator;
        } else {
            delete translator;
            available = false;
        }
    }
    // Qt's own strings (file dialogs, message box buttons) should follow the
    // interface language too. They ship with Qt and are optional.
    //
    // The file name differs by layout: a Qt installation ships both
    // "qtbase_<locale>.qm" (just the base module) and "qt_<locale>.qm" (a meta
    // catalogue that pulls in the base module), while windeployqt only copies
    // the latter. Trying both keeps a deployed build from silently falling back
    // to English.
    const auto qtTranslations = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    auto qt = new QTranslator;
    if (qt->load("qtbase_" + installedLocale + ".qm", qtTranslations) ||
        qt->load("qtbase_" + installedLocale, qtTranslations) ||
        qt->load("qt_" + installedLocale + ".qm", qtTranslations) ||
        qt->load("qt_" + installedLocale, qtTranslations)) {
        QCoreApplication::installTranslator(qt);
        qtTranslator = qt;
    } else {
        delete qt;
    }
    applyInterfaceFont();
    // Qt only delivers QEvent::LanguageChange as a side effect of installing a translator.
    // Switching back to the source language installs nothing, and a packaged build may not
    // ship Qt's own .qm files either, so the event is sent here instead of relying on that
    // side effect. Widgets rebuild from retranslate(); a duplicate event is harmless.
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &languageChange);
    return available;
}
QString localizedLabel(const QString &label) {
    if (label.isEmpty())
        return label;
    // Unlisted labels (native control names, numbered regions) pass through
    // unchanged because translate() falls back to the source text.
    const QByteArray source = label.toUtf8();
    return QCoreApplication::translate("EditHere", source.constData());
}
} // namespace h2d
