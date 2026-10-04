#include "i18n.h"
#include "model.h"
#include "settings.h"
#include "settingsdialog.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>
#include <stdexcept>
using namespace h2d;
namespace {
// Exercises a real failure path that reports its message through the file-local tr()
// helper in model.cpp. lupdate derives a context from the enclosing scope, so this is
// what catches a "h2d::Something" entry that the runtime can never look up.
QString errorFromProject(const QString &path) {
    try {
        loadDocument(path);
    } catch (const std::exception &error) {
        return QString::fromUtf8(error.what());
    }
    return {};
}
// The committed prebuilt .qm is what the release build embeds: the minimal Qt that CI
// installs has no Linguist tools, so the build falls back to this file.
QString prebuiltEnglishTranslation() {
    return QStringLiteral(QT_TESTCASE_SOURCEDIR "/app/translations/built/edithere_en.qm");
}
} // namespace
class I18nTests : public QObject {
    Q_OBJECT
  private slots:
    void simplifiedChineseIsTheSourceLanguage() {
        // Without an installed translation the user sees the source text, which is also
        // why the other tests can keep asserting Simplified Chinese strings.
        QCOMPARE(localizedLabel(QString::fromUtf8("色块区域")), QString::fromUtf8("色块区域"));
        QCOMPARE(QCoreApplication::translate("h2d", "JSON 格式不正确"), QString::fromUtf8("JSON 格式不正确"));
    }
    void languageModesResolveToLocaleIds() {
        QCOMPARE(resolveLanguage(LanguageMode::English), QString("en"));
        QCOMPARE(resolveLanguage(LanguageMode::SimplifiedChinese), QString("zh_CN"));
        // System follows the UI language: Chinese locales use Simplified Chinese, the rest
        // fall back to English.
        const auto system = resolveLanguage(LanguageMode::System);
        QVERIFY2(system == "zh_CN" || system == "en", qPrintable(system));
        QCOMPARE(languageDisplayName(LanguageMode::SimplifiedChinese), QString::fromUtf8("简体中文"));
        QCOMPARE(languageDisplayName(LanguageMode::English), QString("English"));
    }
    void switchingLanguagesReachesEveryContext() {
        QVERIFY(QFile::exists(prebuiltEnglishTranslation()));
        // Loads ":/i18n/edithere_en.qm", so this also covers the resource prefix and the
        // file name the application looks up at runtime.
        QVERIFY(installLanguage(LanguageMode::English));
        QCOMPARE(currentLanguage(), QString("en"));
        QVERIFY(!isChineseInterface());
        // QEvent::LanguageChange is posted to the top-level widgets, so widget text only
        // follows once the event loop has run.
        QTest::qWait(1);

        // "EditHere": labels that double as JSON identifiers are translated for display.
        QCOMPARE(localizedLabel(QString::fromUtf8("色块区域")), QString("Color block area"));
        QCOMPARE(QCoreApplication::translate("EditHere", "整个屏幕"), QString("Whole screen"));
        // Unknown labels fall through unchanged so identifiers and native names stay readable.
        QCOMPARE(localizedLabel(QString("action-button")), QString("action-button"));

        // "h2d": free functions in model.cpp.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto broken = directory.filePath("broken.json");
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("{ not json") > 0);
        file.close();
        QCOMPARE(errorFromProject(broken), QString("JSON format is invalid"));

        // "h2d::SettingsDialog": long-lived chrome rebuilt on QEvent::LanguageChange.
        SettingsDialog dialog(defaultSettings());
        QCOMPARE(dialog.windowTitle(), QString("EditHere Settings"));
        QCOMPARE(dialog.findChild<QPushButton *>("exportDiagnostics")->text(), QString("Export diagnostics…"));

        // Back to the source language: Simplified Chinese needs no application .qm at all.
        QVERIFY(installLanguage(LanguageMode::SimplifiedChinese));
        QCOMPARE(currentLanguage(), QString("zh_CN"));
        QVERIFY(isChineseInterface());
        QCOMPARE(localizedLabel(QString::fromUtf8("色块区域")), QString::fromUtf8("色块区域"));
        // Installing no application translator is exactly the case Qt does not cover on its
        // own, so installLanguage() sends QEvent::LanguageChange itself instead of relying on
        // the side effect of installing one.
        QTest::qWait(1);
        QCOMPARE(dialog.windowTitle(), QString::fromUtf8("EditHere 设置"));
        QCOMPARE(dialog.findChild<QPushButton *>("exportDiagnostics")->text(), QString::fromUtf8("导出诊断日志…"));
    }
    void brokenProjectMessagesStayOneString() {
        // Two messages used to be assembled as "prefix" + variable, which read badly in
        // Chinese ("无法%1当前用户的开机自启项" with a status phrase substituted into %1) and
        // produced stray or missing spaces in English. A single message with a %1
        // placeholder is what the catalogue and this test pin down.
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::white);
        auto document = fromImage(image, "file", "拼接文案");
        const auto healthy = QJsonDocument::fromJson(serializeDocument(document, true)).object();
        auto capture = healthy["capture"].toObject();
        QVERIFY(capture.contains("title"));
        capture["renamed"] = capture.take("title"); // Same key count, one key gone.
        auto broken = healthy;
        broken["capture"] = capture;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("missing-field.EDITHERE");
        saveBytes(path, QJsonDocument(broken).toJson());

        QVERIFY(installLanguage(LanguageMode::English));
        QTest::qWait(1);
        QCOMPARE(errorFromProject(path), QString("The project is missing the field: title"));

        QVERIFY(installLanguage(LanguageMode::SimplifiedChinese));
        QTest::qWait(1);
        QCOMPARE(errorFromProject(path), QString::fromUtf8("项目缺少字段：title"));
    }
    void qtOwnStringsFollowTheInterfaceLanguage() {
        // Qt's own strings -- file dialog buttons, message box defaults -- come from a
        // separate catalogue that is deployed next to the application rather than embedded,
        // and its file name depends on how it was deployed: a Qt installation ships
        // "qtbase_<locale>.qm", while windeployqt renames exactly that file to
        // "qt_<locale>.qm". installLanguage() has to accept both, so this checks the layout
        // the application really runs in instead of the one it is compiled against.
        const auto translations = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        if (QDir(translations).entryList({"qtbase_zh_CN.qm", "qt_zh_CN.qm"}, QDir::Files).isEmpty())
            QSKIP(qPrintable(QString("No Qt Chinese catalogue is deployed in %1.").arg(translations)));

        QVERIFY(installLanguage(LanguageMode::SimplifiedChinese));
        // QPlatformTheme is the context Qt's own dialogs use for the standard button labels.
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Save"), QString::fromUtf8("保存"));
        QCOMPARE(QCoreApplication::translate("QDialogButtonBox", "OK"), QString::fromUtf8("确定"));

        // English is Qt's own source language, so switching to it must drop the catalogue
        // again rather than leave translated buttons behind.
        QVERIFY(installLanguage(LanguageMode::English));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Save"), QString("Save"));
    }
    void oneFeatureKeepsOneEnglishName() {
        // README.en.md presents this feature as "Explode" (大爆炸) in three places, and the
        // code agrees (explodeButton, glyph("explode"), the E shortcut, Editor::explode).
        // The interface has to agree too. Pinning it here means a rename on either side --
        // catalogue or documentation -- shows up as a failing test instead of only being
        // noticed by someone reading both.
        QVERIFY(installLanguage(LanguageMode::English));
        QCOMPARE(QCoreApplication::translate("h2d::Editor", "大爆炸"), QString("Explode"));
        QCOMPARE(QCoreApplication::translate("h2d", "切换大爆炸"), QString("Toggle Explode"));
        QCOMPARE(QCoreApplication::translate("h2d::LayoutCanvas", "大爆炸组件画布"),
                 QString("Explode component canvas"));
        QCOMPARE(QCoreApplication::translate("h2d::GuideOverlay", "用大爆炸调整布局"),
                 QString("Adjust the layout with Explode"));
    }
};
QTEST_MAIN(I18nTests)
#include "i18n_test.moc"
