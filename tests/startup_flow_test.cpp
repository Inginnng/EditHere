#include "controller.h"
#include "guide.h"
#include "settingsdialog.h"
#include "ui.h"
#include <QApplication>
#include <QCheckBox>
#include <QFontDatabase>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace h2d;

class StartupFlowTests : public QObject {
    Q_OBJECT
    static AppSettings quietSettings() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        return settings;
    }
    static Editor *editorOf(Controller &controller) {
        auto tray = controller.findChild<QSystemTrayIcon *>("helpDesignTray");
        return tray && tray->contextMenu() ? qobject_cast<Editor *>(tray->contextMenu()->parentWidget()) : nullptr;
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
            QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
        }
#endif
        applyTheme(ThemeMode::Light);
    }
    void firstManualStartShowsGuideInsteadOfCapture() {
        QTemporaryDir directory;
        const auto stateFile = directory.filePath("settings.ini");
        auto settings = quietSettings();
        settings.captureOnStartup = true;
        Controller controller(nullptr, settings, stateFile);
        auto editor = editorOf(controller);
        QVERIFY(editor);
        controller.start(false, {}, false, !hasSeenGuide(stateFile));
        QTRY_VERIFY(editor->isVisible() && editor->guideActive());
        QVERIFY(editor->hasDocument());
        QVERIFY(!hasSeenGuide(stateFile));
        QVERIFY(editor->document().notes.isEmpty());
        QVERIFY(!editor->document().dirty);
        editor->dismissGuide();
        QVERIFY(hasSeenGuide(stateFile));
        QVERIFY(!editor->guideActive());
        QVERIFY(editor->hasDocument());
        editor->hide();
    }
    void loginLaunchDefersGuideUntilManualActivation() {
        QTemporaryDir directory;
        const auto stateFile = directory.filePath("settings.ini");
        auto settings = quietSettings();
        settings.captureOnStartup = true;
        Controller controller(nullptr, settings, stateFile);
        auto editor = editorOf(controller);
        QVERIFY(editor);
        controller.start(false, {}, true, true);
        QTest::qWait(400); // A capture would have created its native overlay by now.
        QVERIFY(!editor->isVisible());
        QVERIFY(!editor->hasDocument());
        QVERIFY(!editor->guideActive());
        for (auto widget : QApplication::topLevelWidgets())
            QVERIFY(qobject_cast<Overlay *>(widget) == nullptr);
        QVERIFY(!hasSeenGuide(stateFile));
        controller.activate();
        QTRY_VERIFY(editor->isVisible() && editor->guideActive());
        editor->dismissGuide();
        QVERIFY(hasSeenGuide(stateFile));
        editor->hide();
        controller.activate();
        QVERIFY(editor->isVisible());
        QVERIFY(!editor->guideActive());
        editor->hide();
    }
    void seenGuideDoesNotReappearOnLaterManualStartup() {
        QTemporaryDir directory;
        const auto stateFile = directory.filePath("settings.ini");
        QVERIFY(markGuideSeen(nullptr, stateFile));
        Controller controller(nullptr, quietSettings(), stateFile);
        auto editor = editorOf(controller);
        QVERIFY(editor);
        controller.start(false, {}, false, !hasSeenGuide(stateFile));
        QVERIFY(!editor->isVisible());
        QVERIFY(!editor->hasDocument());
        QVERIFY(!editor->guideActive());
        controller.showGuide();
        QTRY_VERIFY(editor->guideActive());
        editor->dismissGuide();
        QVERIFY(hasSeenGuide(stateFile));
        editor->hide();
    }
    void openingFirstImageKeepsItDuringGuide() {
        QTemporaryDir directory;
        const auto stateFile = directory.filePath("settings.ini");
        const auto imageFile = directory.filePath("original.png");
        QImage original(240, 160, QImage::Format_ARGB32);
        original.fill(QColor("#4081c4"));
        QVERIFY(original.save(imageFile));
        Controller controller(nullptr, quietSettings(), stateFile);
        auto editor = editorOf(controller);
        controller.start(false, imageFile, false, true);
        QTRY_VERIFY(editor->guideActive());
        QCOMPARE(editor->document().image.convertToFormat(original.format()), original);
        editor->dismissGuide();
        QCOMPARE(editor->document().image.convertToFormat(original.format()), original);
        editor->hide();
    }
    void settingsGuideEntryKeepsDocumentAndDiscardsDraft() {
        QTemporaryDir directory;
        const auto stateFile = directory.filePath("settings.ini");
        auto settings = quietSettings();
        QVERIFY(saveSettings(settings, nullptr, stateFile));
        Controller controller(nullptr, settings, stateFile);
        auto editor = editorOf(controller);
        QImage original(640, 480, QImage::Format_ARGB32);
        original.fill(Qt::white);
        auto document = fromImage(original, "test", "用户正在编辑的截图");
        Note note;
        note.isGlobal = true;
        note.comment = "界面更轻盈，色彩更克制。";
        document.notes.append(note);
        document.dirty = true;
        editor->setDocument(document);
        controller.showGuide();
        editor->findChild<QPushButton *>("guideNext")->click();
        QCOMPARE(editor->findChild<GuideOverlay *>()->currentStep(), 1);
        bool entryClicked = false;
        QTimer::singleShot(30, [&] {
            auto dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto draft = dialog->findChild<QCheckBox *>("captureOnStartup");
            auto guide = dialog->findChild<QPushButton *>("restartGuide");
            QVERIFY(draft && guide);
            draft->setChecked(!settings.captureOnStartup);
            connect(dialog, &SettingsDialog::guideRequested, dialog, [&] { entryClicked = true; });
            QTimer::singleShot(800, dialog, &QDialog::reject);
            QTest::mouseClick(guide, Qt::LeftButton);
        });
        controller.openSettings();
        QVERIFY(entryClicked);
        QTRY_VERIFY(editor->guideActive());
        QCOMPARE(editor->findChild<GuideOverlay *>()->currentStep(), 0);
        QCOMPARE(editor->document().id, document.id);
        QCOMPARE(editor->document().png, document.png);
        QVERIFY(editor->document().notes == document.notes);
        QVERIFY(editor->document().dirty);
        QCOMPARE(loadSettings(stateFile).captureOnStartup, settings.captureOnStartup);
        editor->dismissGuide();
        QVERIFY(hasSeenGuide(stateFile));
        editor->hide();
    }
};
QTEST_MAIN(StartupFlowTests)
#include "startup_flow_test.moc"
