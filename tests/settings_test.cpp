#include "settings.h"
#include "aicredentials.h"
#include "aisettingspage.h"
#include "settingsdialog.h"
#include "diagnostics.h"
#include "i18n.h"
#include "ui.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QTabWidget>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLineEdit>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSettings>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <algorithm>
using namespace h2d;
// QDesktopServices::setUrlHandler only reaches slots, so the button test needs a
// real QObject to hand the URL to instead of letting a browser open.
class UrlRecorder : public QObject {
    Q_OBJECT
  public:
    QUrl url;
  public slots:
    void record(const QUrl &value) { url = value; }
};
class FakeAiCredentials final : public AiCredentials {
  public:
    QMap<QString, QString> keys;
    int loads = 0, saves = 0, removals = 0;
    bool failWrite = false, holdLoads = false;
    QMap<QString, QString> pendingLoads;
    void load(const QString &id) override {
        ++loads;
        if (holdLoads) { pendingLoads[id] = keys.value(id); return; }
        QTimer::singleShot(0, this, [this, id] { emit loaded(id, keys.value(id)); });
    }
    void finishLoad(const QString &id) { emit loaded(id, pendingLoads.take(id)); }
    void save(const QString &id, const QString &key) override {
        ++saves;
        QTimer::singleShot(0, this, [this, id, key] {
            if (failWrite) emit failed("Test credential store is unavailable");
            else { keys[id] = key; emit saved(id); }
        });
    }
    void remove(const QString &id) override {
        ++removals;
        QTimer::singleShot(0, this, [this, id] { keys.remove(id); emit removed(id); });
    }
};
class SettingsTests : public QObject {
    Q_OBJECT
  private:
    // Same convention as the UI tests: write a picture only when a folder is
    // supplied, so a normal run leaves no files behind.
    void artifact(QWidget &widget, const QString &name) {
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (folder.isEmpty())
            return;
        QDir().mkpath(folder);
        widget.grab().save(QDir(folder).filePath(name));
    }
  private slots:
    void aiSettingsPersistNonSecretsAndIgnoreBrokenProfile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        auto settings = defaultSettings();
        settings.theme = ThemeMode::Dark;
        settings.aiProviderId = "kimi-cn";
        AiConnectionSettings config;
        config.providerId = settings.aiProviderId;
        config.baseUrl = "https://api.moonshot.cn/v1";
        config.model = "vision-model-for-test";
        config.protocol = "chat-completions";
        config.timeoutSeconds = 180;
        config.extraInstructions = "按页面分组。";
        settings.aiProviders.insert(config.providerId, config);
        {
            QSettings legacy(path, QSettings::IniFormat);
            legacy.setValue("ai/providers/kimi-cn/apiKey", "must-not-survive-secret");
            legacy.sync();
        }
        QString error;
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        const auto restored = loadSettings(path);
        QCOMPARE(restored.aiProviderId, settings.aiProviderId);
        QVERIFY(restored.aiProviders.value(config.providerId) == config);
        QCOMPARE(restored.theme, ThemeMode::Dark);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto contents = file.readAll();
        QVERIFY(!contents.contains("must-not-survive-secret"));
        QVERIFY(!contents.contains("apiKey"));
        file.close();
        {
            QSettings malformed(path, QSettings::IniFormat);
            malformed.setValue("ai/providers/kimi-cn/baseUrl", "https://user:password@example.com/v1");
            malformed.sync();
        }
        const auto recovered = loadSettings(path);
        QVERIFY(recovered.aiProviders.isEmpty());
        QCOMPARE(recovered.theme, ThemeMode::Dark);
        QCOMPARE(recovered.shortcuts, settings.shortcuts);
        config.baseUrl = "http://public.example.com/v1";
        settings.aiProviders[config.providerId] = config;
        QVERIFY(!saveSettings(settings, &error, path));
        QVERIFY(!error.isEmpty());
    }
    void aiProviderSwitchKeepsIndependentDraftsAndExplicitSecretWrites() {
        FakeAiCredentials credentials;
        credentials.keys["openai"] = "saved-openai-key";
        AiSettingsPage page(nullptr, &credentials);
        page.setSettings(defaultSettings());
        QCOMPARE(credentials.loads, 0); // Opening unrelated settings pages never reads the keyring.
        auto *provider = page.findChild<QComboBox *>("aiProvider");
        auto *key = page.findChild<QLineEdit *>("aiApiKey");
        auto *base = page.findChild<QLineEdit *>("aiBaseUrl");
        auto *model = page.findChild<QLineEdit *>("aiModel");
        auto *save = page.findChild<QPushButton *>("aiSaveKey");
        auto *remove = page.findChild<QPushButton *>("aiDeleteKey");
        auto *status = page.findChild<QLabel *>("aiConnectionStatus");
        QVERIFY(provider && key && base && model && save && remove && status);
        QVERIFY(provider->findData("openai") >= 0);
        QVERIFY(provider->findData("claude") >= 0);
        QVERIFY(provider->findData("deepseek") >= 0);
        page.show();
        QTRY_COMPARE(key->text(), QString("saved-openai-key"));
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        base->setText("https://gateway.example.com/v1");
        model->setText("custom-vision-model");
        key->selectAll();
        QTest::keyClicks(key, "new-openai-key");
        provider->setCurrentIndex(provider->findData("claude"));
        QTRY_VERIFY(credentials.loads >= 2);
        QTRY_VERIFY(provider->isEnabled());
        QVERIFY(base->text() != "https://gateway.example.com/v1");
        provider->setCurrentIndex(provider->findData("openai"));
        QCOMPARE(base->text(), QString("https://gateway.example.com/v1"));
        QCOMPARE(model->text(), QString("custom-vision-model"));
        QCOMPARE(key->text(), QString("new-openai-key"));
        QCOMPARE(credentials.saves, 0);
        AppSettings result;
        page.applyTo(result);
        QCOMPARE(result.aiProviderId, QString("openai"));
        QCOMPARE(result.aiProviders.value("openai").model, QString("custom-vision-model"));
        save->click();
        QVERIFY(!provider->isEnabled());
        QTRY_COMPARE(credentials.keys.value("openai"), QString("new-openai-key"));
        QTRY_VERIFY(provider->isEnabled());
        QCOMPARE(credentials.saves, 1);
        remove->click();
        QTRY_VERIFY(key->text().isEmpty());
        QVERIFY(!credentials.keys.contains("openai"));
        credentials.failWrite = true;
        QTest::keyClicks(key, "not-saved");
        save->click();
        QTRY_VERIFY(status->text().contains("unavailable"));
        QVERIFY(status->property("error").toBool());
        QVERIFY(provider->isEnabled());
        QVERIFY(!credentials.keys.contains("openai"));
    }
    void aiCredentialReadsSerializeWithWritesAndRecoverAcrossReset() {
        FakeAiCredentials credentials;
        credentials.holdLoads = true;
        credentials.keys["openai"] = "stored-openai";
        credentials.keys["claude"] = "stored-claude";
        AiSettingsPage page(nullptr, &credentials);
        page.setSettings(defaultSettings());
        auto *provider = page.findChild<QComboBox *>("aiProvider");
        auto *key = page.findChild<QLineEdit *>("aiApiKey");
        auto *save = page.findChild<QPushButton *>("aiSaveKey");
        auto *test = page.findChild<QPushButton *>("aiTestConnection");
        auto *status = page.findChild<QLabel *>("aiConnectionStatus");
        QVERIFY(provider && key && save && test && status);
        page.show();
        QCOMPARE(credentials.loads, 1);
        QVERIFY(!provider->isEnabled());
        QVERIFY(!key->isEnabled());
        QVERIFY(!save->isEnabled());
        QVERIFY(!test->isEnabled());
        save->click();
        QCOMPARE(credentials.saves, 0);

        // Restoring other settings remains possible while the OS keyring is
        // waiting; its new provider starts reading only when the old read ends.
        auto changed = defaultSettings();
        changed.aiProviderId = "claude";
        page.setSettings(changed);
        QCOMPARE(credentials.loads, 1);
        credentials.finishLoad("openai");
        QCOMPARE(credentials.loads, 2);
        QVERIFY(!provider->isEnabled());
        credentials.finishLoad("claude");
        QVERIFY(provider->isEnabled());
        QCOMPARE(key->text(), QString("stored-claude"));
        key->selectAll();
        QTest::keyClicks(key, "replacement-claude");
        save->click();
        QTRY_COMPARE(credentials.keys.value("claude"), QString("replacement-claude"));
        // Duplicate or delayed read delivery must not replace a local draft or
        // the newly saved value, even after dirty state has been cleared.
        emit credentials.loaded("claude", "stale-claude");
        QCOMPARE(key->text(), QString("replacement-claude"));

        provider->setCurrentIndex(provider->findData("deepseek"));
        QVERIFY(!test->isEnabled());
        emit credentials.failed("Test keyring could not be unlocked");
        QVERIFY(provider->isEnabled());
        QVERIFY(save->isEnabled());
        QVERIFY(test->isEnabled());
        QVERIFY(status->text().contains("unlocked"));
    }
    void aiSettingsEntryKeepsDefaultsAndCredentialsRejectUnsafeIdentifiers() {
        auto defaults = defaultSettings();
        SettingsDialog dialog(defaults);
        QVERIFY(dialog.settings() == defaults);
        dialog.showAi();
        auto *tabs = dialog.findChild<QTabWidget *>("settingsTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsAiPage"));
        AiCredentials credentials;
        QSignalSpy failed(&credentials, &AiCredentials::failed);
        credentials.load("../invalid");
        credentials.save("openai", {});
        credentials.remove("bad/provider");
        QTRY_COMPARE(failed.size(), 3);
    }
    void aiWindowsSystemCredentialRoundtripOptIn() {
#ifndef Q_OS_WIN
        QSKIP("Windows credential backend integration check.");
#else
        if (qEnvironmentVariable("EDITHERE_TEST_SYSTEM_KEYCHAIN") != "1")
            QSKIP("Opt in with EDITHERE_TEST_SYSTEM_KEYCHAIN=1; uses only a unique synthetic test credential.");
        const auto provider = "test-0-13-0-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString syntheticKey = "EditHere 0.13.0 keychain smoke test - not an API key";
        bool deleted = false;
        const auto cleanup = qScopeGuard([&] {
            if (deleted) return;
            AiCredentials janitor;
            QSignalSpy removed(&janitor, &AiCredentials::removed);
            janitor.remove(provider);
            if (removed.isEmpty() && !removed.wait(10000))
                QTest::qFail("Could not clean up the unique synthetic keychain test item", __FILE__, __LINE__);
        });
        AiCredentials reader;
        QSignalSpy loaded(&reader, &AiCredentials::loaded);
        QSignalSpy failed(&reader, &AiCredentials::failed);
        // Closing settings destroys its credentials object. A write explicitly
        // requested before closing must still reach the Windows credential store.
        {
            AiCredentials transientWriter;
            transientWriter.save(provider, syntheticKey);
        }
        reader.load(provider);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty() || !failed.isEmpty(), 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(loaded.first().at(0).toString(), provider);
        QCOMPARE(loaded.first().at(1).toString(), syntheticKey);

        QSignalSpy removed(&reader, &AiCredentials::removed);
        reader.remove(provider);
        QTRY_VERIFY_WITH_TIMEOUT(!removed.isEmpty() || !failed.isEmpty(), 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(removed.first().first().toString(), provider);
        deleted = true;
        loaded.clear();
        reader.load(provider);
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty() || !failed.isEmpty(), 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QVERIFY(loaded.first().at(1).toString().isEmpty());
#endif
    }
    void aiSettingsCancelAndNarrowTranslatedLayout() {
        struct RestoreLanguage { ~RestoreLanguage() { installLanguage(LanguageMode::SimplifiedChinese); } } restore;
        QVERIFY(installLanguage(LanguageMode::SimplifiedChinese));
        auto settings = defaultSettings();
        settings.language = LanguageMode::SimplifiedChinese;
        FakeAiCredentials credentials;
        credentials.keys["openai"] = "saved-key-for-test";
        const auto noVerticalOverlap = [](const QList<QWidget *> &widgets) {
            for (int i = 1; i < widgets.size(); ++i) {
                if (!widgets[i - 1] || !widgets[i]) return false;
                const auto previousBottom = widgets[i - 1]->mapToGlobal(QPoint(0, widgets[i - 1]->height())).y();
                const auto nextTop = widgets[i]->mapToGlobal(QPoint(0, 0)).y();
                if (previousBottom > nextTop) return false;
            }
            return true;
        };
        const auto basicRows = [](SettingsDialog &dialog) {
            return QList<QWidget *>{dialog.findChild<QComboBox *>("aiProvider"),
                dialog.findChild<QLineEdit *>("aiApiKey"), dialog.findChild<QPushButton *>("aiSaveKey"),
                dialog.findChild<QLabel *>("aiKeyStorageHint"), dialog.findChild<QLabel *>("aiProviderNote"),
                dialog.findChild<QPushButton *>("aiProviderDocs"), dialog.findChild<QLabel *>("aiConnectionStatus"),
                dialog.findChild<QPushButton *>("aiAdvancedToggle")};
        };
        const auto advancedRows = [](SettingsDialog &dialog) {
            return QList<QWidget *>{dialog.findChild<QLineEdit *>("aiBaseUrl"), dialog.findChild<QLineEdit *>("aiModel"),
                dialog.findChild<QComboBox *>("aiProtocol"), dialog.findChild<QSpinBox *>("aiTimeout"),
                dialog.findChild<QPlainTextEdit *>("aiExtraInstructions"), dialog.findChild<QPushButton *>("aiResetPreset")};
        };
        {
            SettingsDialog dialog(settings, nullptr, &credentials);
            dialog.showAi();
            dialog.show();
            auto *key = dialog.findChild<QLineEdit *>("aiApiKey");
            auto *advanced = dialog.findChild<QPushButton *>("aiAdvancedToggle");
            auto *fields = dialog.findChild<QWidget *>("aiAdvancedFields");
            auto *scroll = dialog.findChild<QScrollArea *>("settingsAiPage");
            QVERIFY(key && advanced && fields && scroll);
            QTRY_COMPARE(key->text(), QString("saved-key-for-test"));
            QVERIFY(!fields->isVisible());
            QVERIFY(key->echoMode() == QLineEdit::Password);
            QTRY_VERIFY(noVerticalOverlap(basicRows(dialog)));
            artifact(dialog, "ai-settings-basic.png");
            advanced->click();
            QTRY_VERIFY(fields->isVisible());
            QTRY_VERIFY(noVerticalOverlap(basicRows(dialog)));
            QTRY_VERIFY(noVerticalOverlap(advancedRows(dialog)));
            QTRY_VERIFY(advanced->width() >= advanced->sizeHint().width());
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(fields->geometry().right() < scroll->widget()->width());
            artifact(dialog, "ai-settings-advanced.png");
            artifact(*scroll->widget(), "ai-settings-advanced-page.png");
            key->selectAll();
            QTest::keyClicks(key, "unsaved-draft");
            dialog.reject();
            QCOMPARE(credentials.saves, 0);
            QCOMPARE(credentials.keys.value("openai"), QString("saved-key-for-test"));
        }
        QVERIFY(installLanguage(LanguageMode::English));
        settings.language = LanguageMode::English;
        SettingsDialog dialog(settings, nullptr, &credentials);
        dialog.showAi();
        dialog.resize(500, 560);
        dialog.show();
        auto *key = dialog.findChild<QLineEdit *>("aiApiKey");
        auto *save = dialog.findChild<QPushButton *>("aiSaveKey");
        auto *advanced = dialog.findChild<QPushButton *>("aiAdvancedToggle");
        auto *scroll = dialog.findChild<QScrollArea *>("settingsAiPage");
        QVERIFY(key && save && advanced && scroll);
        QTRY_COMPARE(key->text(), QString("saved-key-for-test"));
        QVERIFY(save->text().contains("Save"));
        advanced->click();
        QTRY_VERIFY(dialog.findChild<QWidget *>("aiAdvancedFields")->isVisible());
        QTRY_VERIFY(noVerticalOverlap(basicRows(dialog)));
        QTRY_VERIFY(noVerticalOverlap(advancedRows(dialog)));
        QTRY_VERIFY(advanced->width() >= advanced->sizeHint().width());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        // The scrollable settings page keeps every field within the viewport
        // even at the supported minimum dialog width, including English labels.
        QVERIFY(scroll->widget()->width() <= scroll->viewport()->width());
        QVERIFY(key->width() >= 80);
        artifact(dialog, "ai-settings-english-narrow.png");
        artifact(*scroll->widget(), "ai-settings-english-narrow-page.png");
        auto *reset = dialog.findChild<QPushButton *>("aiResetPreset");
        QVERIFY(reset);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->ensureWidgetVisible(reset, 0, 0);
        QTRY_VERIFY(scroll->viewport()->rect().contains(QRect(reset->mapTo(scroll->viewport(), QPoint()), reset->size())));
        QVERIFY(reset->height() >= reset->sizeHint().height());
        artifact(dialog, "ai-settings-english-narrow-bottom.png");
        key->selectAll();
        QTest::keyClicks(key, "explicitly-saved");
        save->click();
        QTRY_COMPARE(credentials.keys.value("openai"), QString("explicitly-saved"));
        dialog.reject();
        QCOMPARE(credentials.keys.value("openai"), QString("explicitly-saved"));
    }
    void initTestCase() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
            QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
        }
#endif
        applyTheme(ThemeMode::Light);
    }
    void recognizedTextExportPreferencePersistsAndDefaultsOn() {
        QVERIFY(defaultSettings().includeRecognizedText);
        QVERIFY(defaultSettings().includeWindowTitle);
        QTemporaryDir directory; const auto path = directory.filePath("settings.ini");
        auto settings = defaultSettings(); settings.includeRecognizedText = false; settings.includeWindowTitle = false;
        QString error; QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        QVERIFY(!loadSettings(path).includeRecognizedText);
        QVERIFY(!recognizedTextExportEnabled(path));
        QVERIFY(!loadSettings(path).includeWindowTitle);
        QVERIFY(!windowTitleExportEnabled(path));
        SettingsDialog dialog(settings);
        auto checkbox = dialog.findChild<QCheckBox *>("includeRecognizedText"); QVERIFY(checkbox); QVERIFY(!checkbox->isChecked());
        checkbox->setChecked(true); QVERIFY(dialog.settings().includeRecognizedText);
        auto title = dialog.findChild<QCheckBox *>("includeWindowTitle"); QVERIFY(title); QVERIFY(!title->isChecked());
        title->setChecked(true); QVERIFY(dialog.settings().includeWindowTitle);
    }
    void independentJsonTextPreferencePersistsWithoutChangingFileMode() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        QVERIFY(!defaultSettings().copyJsonTextWithImage);
        { // An existing settings file has no text-copy choice and keeps its file-mode choice.
            QSettings legacy(path, QSettings::IniFormat);
            legacy.setValue("version", 1);
            legacy.setValue("defaults/imageMode", "embedded");
            legacy.setValue("defaults/includeFeedbackPreview", true);
            legacy.sync();
        }
        auto settings = loadSettings(path);
        QCOMPARE(settings.imageExportMode, ImageExportMode::Embedded);
        QVERIFY(!settings.copyJsonTextWithImage);
        QVERIFY(settings.includeFeedbackPreview);
        settings.imageExportMode = ImageExportMode::File;
        settings.copyJsonTextWithImage = true;
        QString error;
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        const auto restored = loadSettings(path);
        QCOMPARE(restored.imageExportMode, ImageExportMode::File);
        QVERIFY(restored.copyJsonTextWithImage);
        QVERIFY(restored.includeFeedbackPreview);
        SettingsDialog dialog(restored);
        auto embed = dialog.findChild<QCheckBox *>("defaultEmbedImage");
        auto text = dialog.findChild<QCheckBox *>("defaultCopyJsonTextWithImage");
        QVERIFY(embed && text && !embed->isChecked() && text->isChecked());
        QVERIFY(!dialog.findChild<QComboBox *>("defaultImageMode"));
        QVERIFY(!dialog.findChild<QCheckBox *>("defaultFeedbackPreview"));
        embed->setChecked(true); text->setChecked(false);
        QCOMPARE(dialog.settings().imageExportMode, ImageExportMode::Embedded);
        QVERIFY(!dialog.settings().copyJsonTextWithImage);
        QVERIFY(dialog.settings().includeFeedbackPreview); // retained solely for legacy/explicit callers
        { QSettings malformed(path, QSettings::IniFormat); malformed.setValue("defaults/copyJsonTextWithImage", "invalid"); malformed.sync(); }
        QVERIFY(!loadSettings(path).copyJsonTextWithImage);
    }
    void consoleIdentityUsesTheDesktopRecognizedTextPreference() {
        const auto application = QCoreApplication::applicationName();
        const auto organization = QCoreApplication::organizationName();
        const bool testMode = QStandardPaths::isTestModeEnabled();
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("EditHere"); QCoreApplication::setOrganizationName("EditHere");
        const auto folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        const auto path = QDir(folder).filePath("settings.ini");
        QFile original(path); const bool existed = original.exists(); QByteArray bytes;
        if (original.open(QIODevice::ReadOnly)) { bytes = original.readAll(); original.close(); }
        struct Restore {
            QString path, application, organization; QByteArray bytes; bool existed, testMode;
            ~Restore() {
                if (existed) { QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write(bytes); }
                else QFile::remove(path);
                QCoreApplication::setApplicationName(application); QCoreApplication::setOrganizationName(organization);
                QStandardPaths::setTestModeEnabled(testMode);
            }
        } restore{path, application, organization, bytes, existed, testMode};
        QDir().mkpath(folder);
        { QSettings values(path, QSettings::IniFormat); values.setValue("version", 1); values.setValue("defaults/includeRecognizedText", false); values.setValue("defaults/includeWindowTitle", false); values.sync(); }
        QCoreApplication::setApplicationName("edithere-cli"); QCoreApplication::setOrganizationName({});
        QVERIFY(!recognizedTextExportEnabled());
        QVERIFY(!windowTitleExportEnabled());
        QCOMPARE(QCoreApplication::applicationName(), QString("edithere-cli"));
        QVERIFY(QCoreApplication::organizationName().isEmpty());
        { QSettings values(path, QSettings::IniFormat); values.setValue("defaults/includeRecognizedText", true); values.setValue("defaults/includeWindowTitle", true); values.sync(); }
        QVERIFY(recognizedTextExportEnabled());
        QVERIFY(windowTitleExportEnabled());
    }
    void diagnosticsAreAccessibleWithoutChangingSettings() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        diagnostics::Options options;
        options.directory = directory.path();
        QVERIFY(diagnostics::start(options));
        struct StopLog { ~StopLog() { diagnostics::stop(); } } stop;
        diagnostics::write(diagnostics::Level::Error, "test.save", "Synthetic save failure");
        auto original = defaultSettings();
        SettingsDialog dialog(original);
        auto page = dialog.findChild<QWidget *>("settingsDiagnosticsPage");
        auto tabs = dialog.findChild<QTabWidget *>("settingsTabs");
        QVERIFY(page && tabs);
        tabs->setCurrentWidget(page);
        dialog.show();
        QTest::qWait(10);
        auto exportButton = page->findChild<QPushButton *>("exportDiagnostics");
        auto folderButton = page->findChild<QPushButton *>("openDiagnosticsFolder");
        QVERIFY(exportButton && exportButton->isEnabled());
        QVERIFY(folderButton && folderButton->isEnabled());
        QCOMPARE(page->findChild<QLabel *>("diagnosticsPath")->text(), QDir::toNativeSeparators(directory.path()));
        QVERIFY(dialog.settings() == original);
        const auto reportPath = directory.filePath("user-feedback.txt");
        bool selected = false;
        QTimer chooser;
        connect(&chooser, &QTimer::timeout, &dialog, [&] {
            if (auto fileDialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); fileDialog && !selected) {
                selected = true;
                auto filename = fileDialog->findChild<QLineEdit *>("fileNameEdit");
                QVERIFY(filename);
                filename->setText(reportPath);
                QMetaObject::invokeMethod(fileDialog, "accept", Qt::DirectConnection);
            }
        });
        chooser.start(10);
        exportButton->click();
        chooser.stop();
        QVERIFY(selected);
        QFile report(reportPath);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto contents = report.readAll();
        QVERIFY(contents.contains("Synthetic save failure"));
        QVERIFY(contents.contains("Environment:"));
        QVERIFY(page->findChild<QLabel *>("diagnosticsStatus")->text().contains("日志已导出"));
        const auto artifact = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!artifact.isEmpty()) {
            QDir().mkpath(artifact);
            for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
                applyTheme(mode);
                QTest::qWait(10);
                QVERIFY(dialog.grab().save(QDir(artifact).filePath(mode == ThemeMode::Light ? "diagnostics-light.png" : "diagnostics-dark.png")));
            }
            applyTheme(ThemeMode::Light);
        }
        dialog.reject();
        QVERIFY(dialog.settings() == original);
    }
    void captureStyleColoursSurviveRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        auto settings = defaultSettings();
        settings.captureStyle.border = true;
        settings.captureStyle.borderColor = QColor(21, 73, 129, 86);
        settings.captureStyle.shadowColor = QColor(142, 31, 67, 150);
        QString error;
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        QCOMPARE(loadSettings(path).captureStyle, settings.captureStyle);

        // Clearing a remembered colour restores the automatic accent on restart.
        settings.captureStyle.shadowColor = {};
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        QCOMPARE(loadSettings(path).captureStyle, settings.captureStyle);

        QSettings source(path, QSettings::IniFormat);
        source.setValue("capture/borderColor", "invalid");
        source.setValue("capture/shadowColor", "invalid");
        source.sync();
        const auto restored = loadSettings(path);
        QCOMPARE(restored.captureStyle.borderColor, defaultSettings().captureStyle.borderColor);
        QVERIFY(!restored.captureStyle.shadowColor.isValid());
        QVERIFY(restored.captureStyle.border);
    }
    void savingSettingsPreservesRememberedCaptureStyle() {
        auto original = defaultSettings();
        original.captureStyle.cornerRadius = 19;
        original.captureStyle.shadow = false;
        original.captureStyle.shadowStrength = 77;
        original.captureStyle.border = true;
        original.captureStyle.borderColor = QColor("#aabbcc");
        SettingsDialog dialog(original);
        QCOMPARE(dialog.settings().captureStyle, original.captureStyle);
        bool applied = false;
        dialog.setApplyHandler([&](const AppSettings &next) {
            applied = true;
            if (!(next.captureStyle == original.captureStyle)) return QString("Style lost");
            return QString();
        });
        dialog.findChild<QPushButton *>("settingsSave")->click();
        QVERIFY(applied);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        dialog.findChild<QPushButton *>("settingsReset")->click();
        QCOMPARE(dialog.settings().captureStyle, defaultSettings().captureStyle);
    }
    void defaultsAndPortablePersistence() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("config/settings.ini");
        const auto defaults = defaultSettings();
        QVERIFY(validateSettings(defaults).isEmpty());
        QCOMPARE(defaults.shortcuts.value("capture"), QKeySequence("Alt+Shift+2"));
        QCOMPARE(defaults.shortcuts.value("annotate"), QKeySequence("Alt+Shift+1"));
        const auto definitions = shortcutDefinitions();
        const auto annotate = std::find_if(definitions.cbegin(), definitions.cend(),
                                          [](const auto &definition) { return definition.id == "annotate"; });
        QVERIFY(annotate != definitions.cend());
        QVERIFY(annotate->global);
        QVERIFY(!defaults.launchAtLogin);
        QVERIFY(loadSettings(path) == defaults);
        auto changed = defaults;
        changed.theme = ThemeMode::Dark;
        changed.captureOnStartup = false;
        changed.launchAtLogin = true;
        changed.fitImageOnOpen = false;
        changed.embedOriginal = false;
        changed.imageExportMode = ImageExportMode::None;
        changed.copyJsonTextWithImage = true;
        changed.includeFeedbackPreview = true;
        changed.includeRecognizedText = false;
        changed.includeWindowTitle = false;
        changed.confirmBeforeDiscard = false;
        changed.checkUpdatesOnStartup = true;
        changed.defaultTool = 2;
        changed.toolbarActions = {"saveProject", "copyJson"};
        changed.shortcuts["capture"] = QKeySequence("Ctrl+Alt+9");
        changed.shortcuts["annotate"] = QKeySequence("Ctrl+Alt+8");
        changed.shortcuts["point"] = {};
        changed.shortcuts["rectangle"] = QKeySequence("Ctrl+,");
        changed.scrollAxis = Qt::Horizontal;
        changed.scrollAutoCrop = true;
        changed.scrollUltraLong = true;
        QString error = "old error";
        QVERIFY2(saveSettings(changed, &error, path), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(loadSettings(path) == changed);
        QSettings persisted(path, QSettings::IniFormat);
        QCOMPARE(persisted.value("appearance/theme").toString(), QString("dark"));
        // "Don't ask again" is a preference, not a mood: it has to come back after a
        // restart the same way it was left.
        QVERIFY(!persisted.value("defaults/confirmBeforeDiscard").toBool());
        QVERIFY(persisted.value("defaults/launchAtLogin").toBool());
        QCOMPARE(persisted.value("shortcuts/capture").toString(), QString("Ctrl+Alt+9"));
        QCOMPARE(persisted.value("shortcuts/annotate").toString(), QString("Ctrl+Alt+8"));
        QCOMPARE(persisted.value("capture/scrollAxis").toString(), QString("horizontal"));
        QVERIFY(persisted.value("capture/scrollAutoCrop").toBool());
        QVERIFY(persisted.value("capture/scrollUltraLong").toBool());
        QVERIFY(persisted.contains("shortcuts/point"));
        QVERIFY(persisted.value("shortcuts/point").toString().isEmpty());
    }
    void invalidSettingsFallBackWithoutOverwriting() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        {
            QSettings source(path, QSettings::IniFormat);
            source.setValue("appearance/theme", "dark");
            source.setValue("shortcuts/capture", "not a key");
            source.sync();
        }
        QVERIFY(loadSettings(path) == defaultSettings());
        {
            QSettings source(path, QSettings::IniFormat);
            source.remove("shortcuts");
            source.setValue("appearance/theme", "broken");
            source.sync();
        }
        QVERIFY(loadSettings(path) == defaultSettings());
        {
            QSettings source(path, QSettings::IniFormat);
            source.setValue("appearance/theme", "light");
            source.setValue("shortcuts/point", "R");
            source.sync();
        }
        QVERIFY(loadSettings(path) == defaultSettings());
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll();
        original.close();
        auto invalid = defaultSettings();
        invalid.shortcuts["capture"] = QKeySequence("A");
        QString error;
        QVERIFY(!saveSettings(invalid, &error, path));
        QVERIFY(!error.isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
    }
    void conflictAndGlobalRules() {
        auto settings = defaultSettings();
        settings.shortcuts["point"] = settings.shortcuts["rectangle"];
        const auto conflict = validateSettings(settings);
        QVERIFY(conflict.contains("点标注"));
        QVERIFY(conflict.contains("框选标注"));
        settings.shortcuts["point"] = {};
        QVERIFY(validateSettings(settings).isEmpty());
        for (const auto &id : {"capture", "annotate"}) {
            for (const auto &invalid : {QKeySequence("A"), QKeySequence("Shift+A"),
                                        QKeySequence("Ctrl+A, Ctrl+B"), QKeySequence(Qt::Key_Control)}) {
                settings.shortcuts[id] = invalid;
                QVERIFY2(!validateSettings(settings).isEmpty(), qPrintable(invalid.toString()));
            }
            for (const auto &valid :
                 {QKeySequence("Ctrl+Alt+9"), QKeySequence("F12"), QKeySequence(Qt::Key_Print), QKeySequence()}) {
                settings.shortcuts[id] = valid;
                QVERIFY2(validateSettings(settings).isEmpty(), qPrintable(valid.toString()));
            }
        }
        settings.shortcuts["capture"] = settings.shortcuts["save"];
        QVERIFY(!validateSettings(settings).isEmpty());
        settings = defaultSettings();
        settings.shortcuts["annotate"] = settings.shortcuts["capture"];
        const auto globalConflict = validateSettings(settings);
        QVERIFY(globalConflict.contains("截图"));
        QVERIFY(globalConflict.contains("新建批注"));
        settings.shortcuts["annotate"] = settings.shortcuts["save"];
        QVERIFY(!validateSettings(settings).isEmpty());
    }
    void legacyAndMalformedNewPreferences() {
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.ini");
        QSettings source(path, QSettings::IniFormat);
        source.setValue("version", 1);
        source.setValue("appearance/theme", "dark");
        source.setValue("shortcuts/capture", "Ctrl+Alt+9");
        source.sync();
        auto expected = defaultSettings();
        expected.theme = ThemeMode::Dark;
        expected.shortcuts["capture"] = QKeySequence("Ctrl+Alt+9");
        QVERIFY(loadSettings(path) == expected);
        QCOMPARE(loadSettings(path).shortcuts.value("annotate"), QKeySequence("Alt+Shift+1"));
        QVERIFY(!loadSettings(path).launchAtLogin);
        source.setValue("defaults/launchAtLogin", "invalid");
        source.setValue("defaults/tool", "nonsense");
        source.setValue("defaults/embedOriginal", "invalid");
        source.setValue("updates/checkOnStartup", "invalid");
        source.sync();
        QVERIFY(loadSettings(path) == expected);
        expected.defaultTool = 4;
        QVERIFY(!validateSettings(expected).isEmpty());
    }
    void legacyAnnotateShortcutMigrationAndDisabling_data() {
        QTest::addColumn<QString>("conflictingId");
        QTest::newRow("existing-global-shortcut") << QString("capture");
        QTest::newRow("existing-editor-shortcut") << QString("point");
    }
    void legacyAnnotateShortcutMigrationAndDisabling() {
        QFETCH(QString, conflictingId);
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.ini");
        QSettings source(path, QSettings::IniFormat);
        source.setValue("version", 1);
        source.setValue("appearance/theme", "dark");
        source.setValue("defaults/tool", 1);
        source.setValue("shortcuts/" + conflictingId, "Alt+Shift+1");
        source.sync();
        auto expected = defaultSettings();
        expected.theme = ThemeMode::Dark;
        expected.defaultTool = 1;
        expected.shortcuts[conflictingId] = QKeySequence("Alt+Shift+1");
        expected.shortcuts["annotate"] = {};
        QVERIFY(loadSettings(path) == expected);
        QVERIFY(validateSettings(expected).isEmpty());
        QString error;
        QVERIFY2(saveSettings(expected, &error, path), qPrintable(error));
        QVERIFY(loadSettings(path) == expected);
        source.sync();
        QVERIFY(source.contains("shortcuts/annotate"));
        QVERIFY(source.value("shortcuts/annotate").toString().isEmpty());

        // An explicit empty binding stays disabled even after the old conflict is removed.
        expected.shortcuts[conflictingId] = defaultSettings().shortcuts.value(conflictingId);
        QVERIFY2(saveSettings(expected, &error, path), qPrintable(error));
        QVERIFY(loadSettings(path) == expected);
        source.sync();
        source.remove("shortcuts/annotate");
        source.sync();
        expected.shortcuts["annotate"] = QKeySequence("Alt+Shift+1");
        QVERIFY(loadSettings(path) == expected);

        // Explicitly saved conflicts still fail validation rather than using migration.
        expected.shortcuts[conflictingId] = expected.shortcuts["annotate"];
        QVERIFY(!validateSettings(expected).isEmpty());
        QVERIFY(!saveSettings(expected, &error, path));
    }
    void toolbarValidationPersistenceAndMigration() {
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.ini");
        auto settings = defaultSettings();
        QCOMPARE(settings.toolbarActions.size(), 6);
        for (const auto &id : {"saveProject", "saveImage", "exportJson", "copyJson", "copyImage"})
            QVERIFY(settings.toolbarActions.contains(id));
        QVERIFY(!settings.toolbarActions.contains("capture"));
        QVERIFY(!settings.toolbarActions.contains("fit"));
        for (const auto &id : {"copyJson", "saveImage", "hideAnnotations", "addGlobalNote"}) {
            QVERIFY(settings.shortcuts.contains(id));
            QVERIFY(settings.shortcuts.value(id).isEmpty());
        }
        settings.theme = ThemeMode::Dark;
        settings.toolbarActions = {"capture", "fit", "copyJson"};
        QString error;
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        QVERIFY(loadSettings(path) == settings);
        settings.toolbarActions.clear();
        QVERIFY2(saveSettings(settings, &error, path), qPrintable(error));
        QVERIFY(loadSettings(path) == settings);
        settings.toolbarActions = {"saveProject", "saveProject"};
        QVERIFY(!validateSettings(settings).isEmpty());
        QVERIFY(!saveSettings(settings, &error, path));
        QVERIFY(loadSettings(path).toolbarActions.isEmpty());
        settings.toolbarActions = {"copyImage", "unrecognized"};
        QVERIFY(!validateSettings(settings).isEmpty());
        QVERIFY(!saveSettings(settings, &error, path));
        // A damaged toolbar preference must not erase the user's other preferences.
        QSettings source(path, QSettings::IniFormat);
        source.setValue("toolbar/actions", QStringList{"copyImage", "unrecognized"});
        source.sync();
        auto recovered = loadSettings(path);
        QCOMPARE(recovered.theme, ThemeMode::Dark);
        QCOMPARE(recovered.toolbarActions, defaultSettings().toolbarActions);
        source.setValue("toolbar/actions", QStringList{"copyJson", "copyJson"});
        source.sync();
        QCOMPARE(loadSettings(path).toolbarActions, defaultSettings().toolbarActions);
        // Existing users without the preference get all five actions on upgrade.
        source.remove("toolbar/actions");
        source.sync();
        QCOMPARE(loadSettings(path).toolbarActions, defaultSettings().toolbarActions);
        // Renaming exportJson must preserve the former selection and Ctrl+E binding.
        const QStringList legacyActions{"saveProject", "saveImage", "copyJson", "exportJson", "copyImage"};
        source.setValue("toolbar/actions", legacyActions);
        source.setValue("shortcuts/export", "Ctrl+E");
        source.sync();
        auto migrated = loadSettings(path);
        QCOMPARE(migrated.toolbarActions, legacyActions);
        QCOMPARE(migrated.shortcuts.value("export"), QKeySequence("Ctrl+E"));
        QVERIFY2(saveSettings(migrated, &error, path), qPrintable(error));
        QVERIFY(loadSettings(path) == migrated);
    }
    void toolbarDraftAndUpdateNavigation() {
        SettingsDialog dialog(defaultSettings());
        auto tabs = dialog.findChild<QTabWidget *>("settingsTabs");
        QVERIFY(tabs);
        dialog.showToolbar();
        QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsToolbarPage"));
        for (const auto &definition : toolbarActionDefinitions()) {
            auto checkbox = dialog.findChild<QCheckBox *>("toolbar_" + definition.id);
            QVERIFY(checkbox);
            QCOMPARE(checkbox->isChecked(), defaultSettings().toolbarActions.contains(definition.id));
            checkbox->setChecked(false);
        }
        QVERIFY(dialog.settings().toolbarActions.isEmpty());
        dialog.findChild<QCheckBox *>("toolbar_copyJson")->setChecked(true);
        QCOMPARE(dialog.settings().toolbarActions, QStringList{"copyJson"});
        dialog.findChild<QCheckBox *>("toolbar_capture")->setChecked(true);
        dialog.findChild<QCheckBox *>("toolbar_fit")->setChecked(true);
        QCOMPARE(dialog.settings().toolbarActions, (QStringList{"copyJson", "capture", "fit"}));
        QVERIFY(validateSettings(dialog.settings()).isEmpty());
        dialog.showUpdates();
        QCOMPARE(tabs->currentWidget()->objectName(), QString("settingsAboutPage"));
        // The update page carries the feedback group, so users who cannot update
        // still have a way to report the problem.
        auto feedback = dialog.findChild<QLabel *>("aboutFeedback");
        QVERIFY(feedback);
        QVERIFY(feedback->text().contains("1018416966"));
        // The note sits behind a button, so the page itself stays short and the
        // long text is only built when someone asks for it.
        auto noteButton = dialog.findChild<QPushButton *>("aboutNoteButton");
        QVERIFY(noteButton && !noteButton->text().isEmpty());
        QVERIFY(!dialog.findChild<QLabel *>("authorNoteText"));
        dialog.findChild<QPushButton *>("settingsReset")->click();
        QCOMPARE(dialog.settings().toolbarActions, defaultSettings().toolbarActions);
    }
    void authorNoteOpensWithAStarLink() {
        SettingsDialog dialog(defaultSettings());
        dialog.showUpdates();
        dialog.show();
        QCoreApplication::processEvents();
        artifact(dialog, "settings-about-note.png");
        auto opener = dialog.findChild<QPushButton *>("aboutNoteButton");
        QVERIFY(opener);
        opener->click();
        auto note = dialog.findChild<QDialog *>("authorNoteDialog");
        QVERIFY(note);
        auto text = note->findChild<QLabel *>("authorNoteText");
        QVERIFY(text && text->text().contains("印迹"));
        QVERIFY(text->text().contains("GitHub"));
        auto star = note->findChild<QPushButton *>("authorNoteStar");
        QVERIFY(star && !star->text().isEmpty());
        UrlRecorder recorder;
        QDesktopServices::setUrlHandler("https", &recorder, "record");
        star->click();
        // Clear first so a failing comparison cannot leave the handler installed
        // for later tests.
        QDesktopServices::setUrlHandler("https", nullptr, nullptr);
        QCOMPARE(recorder.url.toString(), QString("https://github.com/Inginnng/EditHere"));
        QCoreApplication::processEvents();
        artifact(*note, "settings-author-note.png");
        applyTheme(ThemeMode::Dark);
        QCoreApplication::processEvents();
        artifact(*note, "settings-author-note-dark.png");
        applyTheme(ThemeMode::Light);
        QCoreApplication::processEvents();
        note->close();
    }
    void failedWriteReportsError() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY(!saveSettings(defaultSettings(), &error, directory.path()));
        QVERIFY(!error.isEmpty());
    }
    void resetAndCancelOnlyChangeDraft() {
        auto original = defaultSettings();
        original.theme = ThemeMode::Dark;
        original.captureOnStartup = false;
        original.launchAtLogin = true;
        original.fitImageOnOpen = false;
        original.embedOriginal = false;
        original.checkUpdatesOnStartup = true;
        original.defaultTool = 1;
        original.toolbarActions = {"copyImage"};
        original.shortcuts["capture"] = QKeySequence("Ctrl+Alt+9");
        original.shortcuts["annotate"] = QKeySequence("Ctrl+Alt+8");
        SettingsDialog dialog(original);
        int applyCount = 0;
        dialog.setApplyHandler([&](const AppSettings &) {
            ++applyCount;
            return QString();
        });
        QVERIFY(dialog.settings() == original);
        dialog.show();
        QTest::mouseClick(dialog.findChild<QPushButton *>("settingsReset"), Qt::LeftButton);
        QVERIFY(dialog.settings() == defaultSettings());
        QCOMPARE(applyCount, 0);
        QTest::mouseClick(dialog.findChild<QPushButton *>("settingsCancel"), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(applyCount, 0);
        QCOMPARE(original.theme, ThemeMode::Dark);
        QVERIFY(original.launchAtLogin);
    }
    void launchAtLoginFailureKeepsDraftUntilSaved() {
        SettingsDialog dialog(defaultSettings());
        auto login = dialog.findChild<QCheckBox *>("launchAtLogin");
        auto save = dialog.findChild<QPushButton *>("settingsSave");
        auto error = dialog.findChild<QLabel *>("errorLabel");
        QVERIFY(login && save && error);
        QVERIFY(!login->isChecked());
        login->setChecked(true);
        AppSettings persisted = defaultSettings();
        dialog.setApplyHandler([&](const AppSettings &draft) {
            if (draft.launchAtLogin)
                return QString("无法设置开机启动，请检查系统权限。");
            persisted = draft;
            return QString();
        });
        dialog.show();
        save->click();
        QVERIFY(dialog.isVisible());
        QVERIFY(error->isVisible());
        QVERIFY(error->text().contains("开机启动"));
        QVERIFY(login->isChecked());
        QVERIFY(!persisted.launchAtLogin);
        login->setChecked(false);
        QVERIFY(!error->isVisible());
        save->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        QVERIFY(!persisted.launchAtLogin);
    }
    void launchAtLoginNoticeDoesNotChangeDraft() {
        SettingsDialog dialog(defaultSettings());
        auto notice = dialog.findChild<QLabel *>("launchAtLoginNotice");
        QVERIFY(notice);
        QVERIFY(notice->isHidden());
        const auto message = QString("请前往系统设置批准 EditHere 登录项。");
        dialog.setLaunchAtLoginNotice(message);
        QCOMPARE(notice->text(), message);
        QVERIFY(!notice->isHidden());
        QVERIFY(dialog.settings() == defaultSettings());
        dialog.setLaunchAtLoginNotice({});
        QVERIFY(notice->isHidden());
    }
    void replayGuideClosesWithoutApplyingDraft() {
        const auto original = defaultSettings();
        SettingsDialog dialog(original);
        auto guide = dialog.findChild<QPushButton *>("restartGuide");
        auto login = dialog.findChild<QCheckBox *>("launchAtLogin");
        QVERIFY(guide && login);
        QVERIFY(guide->toolTip().contains("未保存"));
        login->setChecked(true);
        int applyCount = 0;
        dialog.setApplyHandler([&](const AppSettings &) {
            ++applyCount;
            return QString();
        });
        QSignalSpy requested(&dialog, &SettingsDialog::guideRequested);
        QSignalSpy rejected(&dialog, &QDialog::rejected);
        bool closedBeforeRequest = false;
        connect(&dialog, &SettingsDialog::guideRequested, &dialog, [&] {
            closedBeforeRequest = !dialog.isVisible() && dialog.result() == QDialog::Rejected;
        });
        QTimer::singleShot(0, guide, &QPushButton::click);
        QCOMPARE(dialog.exec(), int(QDialog::Rejected));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(closedBeforeRequest);
        QCOMPARE(applyCount, 0);
        QVERIFY(!original.launchAtLogin);
    }
    void guideStateSurvivesOldDraftAndRestoredDefaults() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("config/settings.ini");
        QVERIFY(!hasSeenGuide(path));
        auto preferences = defaultSettings();
        preferences.theme = ThemeMode::Dark;
        preferences.launchAtLogin = true;
        QString error;
        QVERIFY2(saveSettings(preferences, &error, path), qPrintable(error));
        QVERIFY(!hasSeenGuide(path));
        SettingsDialog staleDialog(loadSettings(path));
        error = "old error";
        QVERIFY2(markGuideSeen(&error, path), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(hasSeenGuide(path));
        QVERIFY(loadSettings(path) == preferences);
        staleDialog.setApplyHandler([&](const AppSettings &draft) {
            saveSettings(draft, &error, path);
            return error;
        });
        staleDialog.findChild<QPushButton *>("settingsSave")->click();
        QCOMPARE(staleDialog.result(), int(QDialog::Accepted));
        QVERIFY(hasSeenGuide(path));
        SettingsDialog resetDialog(loadSettings(path));
        resetDialog.setApplyHandler([&](const AppSettings &draft) {
            saveSettings(draft, &error, path);
            return error;
        });
        resetDialog.findChild<QPushButton *>("settingsReset")->click();
        QVERIFY(resetDialog.settings() == defaultSettings());
        QVERIFY(hasSeenGuide(path));
        resetDialog.findChild<QPushButton *>("settingsSave")->click();
        QCOMPARE(resetDialog.result(), int(QDialog::Accepted));
        QVERIFY(loadSettings(path) == defaultSettings());
        QVERIFY(hasSeenGuide(path));
        QVERIFY2(markGuideSeen(&error, path), qPrintable(error));
        QVERIFY(hasSeenGuide(path));
    }
    void guideStateCreationAndMalformedValues() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("new/settings.ini");
        QString error;
        QVERIFY2(markGuideSeen(&error, path), qPrintable(error));
        QVERIFY(hasSeenGuide(path));
        QVERIFY(loadSettings(path) == defaultSettings());
        QSettings source(path, QSettings::IniFormat);
        for (const auto &value : {"invalid", "false", "0", ""}) {
            source.setValue("onboarding/seen", value);
            source.sync();
            QVERIFY(!hasSeenGuide(path));
        }
        for (const auto &value : {"true", "1"}) {
            source.setValue("onboarding/seen", value);
            source.sync();
            QVERIFY(hasSeenGuide(path));
        }
    }
    void failedGuideWriteReportsError() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString error;
        QVERIFY(!markGuideSeen(&error, directory.path()));
        QVERIFY(!error.isEmpty());
        QFile blocker(directory.filePath("blocked"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("keep"), qint64(4));
        blocker.close();
        error.clear();
        QVERIFY(!markGuideSeen(&error, blocker.fileName() + "/settings.ini"));
        QVERIFY(!error.isEmpty());
        QVERIFY(blocker.open(QIODevice::ReadOnly));
        QCOMPARE(blocker.readAll(), QByteArray("keep"));
    }
    void saveValidatesBeforeApplyingAndKeepsErrorsVisible() {
        SettingsDialog dialog(defaultSettings());
        auto point = dialog.findChild<QKeySequenceEdit *>("shortcut_point");
        auto rectangle = dialog.findChild<QKeySequenceEdit *>("shortcut_rectangle");
        auto annotate = dialog.findChild<QKeySequenceEdit *>("shortcut_annotate");
        auto capture = dialog.findChild<QKeySequenceEdit *>("shortcut_capture");
        auto save = dialog.findChild<QPushButton *>("settingsSave");
        auto error = dialog.findChild<QLabel *>("errorLabel");
        auto theme = dialog.findChild<QComboBox *>("themeMode");
        auto login = dialog.findChild<QCheckBox *>("launchAtLogin");
        QVERIFY(point && rectangle && annotate && capture && save && error && theme && login);
        QCOMPARE(point->maximumSequenceLength(), 1);
        QVERIFY(point->isClearButtonEnabled());
        QCOMPARE(annotate->maximumSequenceLength(), 1);
        QVERIFY(annotate->isClearButtonEnabled());
        QCOMPARE(annotate->keySequence(), QKeySequence("Alt+Shift+1"));
        int applyCount = 0;
        dialog.setApplyHandler([&](const AppSettings &) {
            ++applyCount;
            return QString("快捷键已被其他应用占用。");
        });
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.show();
        point->setKeySequence(rectangle->keySequence());
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(applyCount, 0);
        QVERIFY(error->isVisible());
        QVERIFY(dialog.isVisible());
        point->clear();
        annotate->setKeySequence(capture->keySequence());
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(applyCount, 0);
        QVERIFY(error->isVisible());
        QVERIFY(error->text().contains("新建批注"));
        annotate->clear();
        login->setChecked(true);
        theme->setCurrentIndex(theme->findData(static_cast<int>(ThemeMode::Light)));
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(applyCount, 1);
        QVERIFY(error->text().contains("占用"));
        QVERIFY(error->isVisible());
        QVERIFY(dialog.isVisible());
        AppSettings applied;
        dialog.setApplyHandler([&](const AppSettings &settings) {
            ++applyCount;
            applied = settings;
            return QString();
        });
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(applyCount, 2);
        QCOMPARE(accepted.count(), 1);
        QVERIFY(!dialog.isVisible());
        QCOMPARE(applied.theme, ThemeMode::Light);
        QVERIFY(applied.launchAtLogin);
        QVERIFY(applied.shortcuts["point"].isEmpty());
        QVERIFY(applied.shortcuts["annotate"].isEmpty());
    }
};
QTEST_MAIN(SettingsTests)
#include "settings_test.moc"
