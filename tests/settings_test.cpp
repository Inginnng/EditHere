#include "settings.h"
#include "settingsdialog.h"
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace h2d;
class SettingsTests : public QObject {
    Q_OBJECT
  private slots:
    void defaultsAndPortablePersistence() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("config/settings.ini");
        const auto defaults = defaultSettings();
        QVERIFY(validateSettings(defaults).isEmpty());
        QVERIFY(!defaults.launchAtLogin);
        QVERIFY(loadSettings(path) == defaults);
        auto changed = defaults;
        changed.theme = ThemeMode::Dark;
        changed.captureOnStartup = false;
        changed.launchAtLogin = true;
        changed.fitImageOnOpen = false;
        changed.embedOriginal = false;
        changed.checkUpdatesOnStartup = true;
        changed.defaultTool = 2;
        changed.toolbarActions = {"saveProject", "copyJson"};
        changed.shortcuts["capture"] = QKeySequence("Ctrl+Alt+9");
        changed.shortcuts["point"] = {};
        changed.shortcuts["rectangle"] = QKeySequence("Ctrl+,");
        QString error = "old error";
        QVERIFY2(saveSettings(changed, &error, path), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(loadSettings(path) == changed);
        QSettings persisted(path, QSettings::IniFormat);
        QCOMPARE(persisted.value("appearance/theme").toString(), QString("dark"));
        QVERIFY(persisted.value("defaults/launchAtLogin").toBool());
        QCOMPARE(persisted.value("shortcuts/capture").toString(), QString("Ctrl+Alt+9"));
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
        for (const auto &invalid : {QKeySequence("A"), QKeySequence("Shift+A"),
                                    QKeySequence("Ctrl+A, Ctrl+B"), QKeySequence(Qt::Key_Control)}) {
            settings.shortcuts["capture"] = invalid;
            QVERIFY2(!validateSettings(settings).isEmpty(), qPrintable(invalid.toString()));
        }
        for (const auto &valid :
             {QKeySequence("Ctrl+Alt+9"), QKeySequence("F12"), QKeySequence(Qt::Key_Print), QKeySequence()}) {
            settings.shortcuts["capture"] = valid;
            QVERIFY2(validateSettings(settings).isEmpty(), qPrintable(valid.toString()));
        }
        settings.shortcuts["capture"] = settings.shortcuts["save"];
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
    void toolbarValidationPersistenceAndMigration() {
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.ini");
        auto settings = defaultSettings();
        QCOMPARE(settings.toolbarActions.size(), 5);
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
        dialog.findChild<QPushButton *>("settingsReset")->click();
        QCOMPARE(dialog.settings().toolbarActions, defaultSettings().toolbarActions);
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
        const auto message = QString("请前往系统设置批准 HelpDesign 登录项。");
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
        auto save = dialog.findChild<QPushButton *>("settingsSave");
        auto error = dialog.findChild<QLabel *>("errorLabel");
        auto theme = dialog.findChild<QComboBox *>("themeMode");
        auto login = dialog.findChild<QCheckBox *>("launchAtLogin");
        QVERIFY(point && rectangle && save && error && theme && login);
        QCOMPARE(point->maximumSequenceLength(), 1);
        QVERIFY(point->isClearButtonEnabled());
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
    }
};
QTEST_MAIN(SettingsTests)
#include "settings_test.moc"
