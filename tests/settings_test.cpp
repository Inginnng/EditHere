#include "settings.h"
#include "settingsdialog.h"
#include <QComboBox>
#include <QFile>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
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
        QVERIFY(loadSettings(path) == defaults);
        auto changed = defaults;
        changed.theme = ThemeMode::Dark;
        changed.shortcuts["capture"] = QKeySequence("Ctrl+Alt+9");
        changed.shortcuts["point"] = {};
        changed.shortcuts["rectangle"] = QKeySequence("Ctrl+,");
        QString error = "old error";
        QVERIFY2(saveSettings(changed, &error, path), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(loadSettings(path) == changed);
        QSettings persisted(path, QSettings::IniFormat);
        QCOMPARE(persisted.value("appearance/theme").toString(), QString("dark"));
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
    }
    void saveValidatesBeforeApplyingAndKeepsErrorsVisible() {
        SettingsDialog dialog(defaultSettings());
        auto point = dialog.findChild<QKeySequenceEdit *>("shortcut_point");
        auto rectangle = dialog.findChild<QKeySequenceEdit *>("shortcut_rectangle");
        auto save = dialog.findChild<QPushButton *>("settingsSave");
        auto error = dialog.findChild<QLabel *>("errorLabel");
        auto theme = dialog.findChild<QComboBox *>("themeMode");
        QVERIFY(point && rectangle && save && error && theme);
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
        QVERIFY(applied.shortcuts["point"].isEmpty());
    }
};
QTEST_MAIN(SettingsTests)
#include "settings_test.moc"
