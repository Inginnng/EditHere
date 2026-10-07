#include "controller.h"
#include "autostart.h"
#include "overlay.h"
#include "guide.h"
#include "settingsdialog.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace h2d;

class StartupFlowTests : public QObject {
    Q_OBJECT
    static AppSettings quietSettings() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.shortcuts["annotate"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        return settings;
    }
    static Editor *editorOf(Controller &controller) {
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
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
    void magnifierRemainsVisibleWithoutDelay() {
        ScreenFrame frame;
        frame.image=QImage(800,600,QImage::Format_RGB32);
        frame.image.fill(QColor(220,180,140));
        frame.logicalGeometry=QRect(0,0,800,600);
        Overlay overlay(frame);
        overlay.show();
        QMouseEvent move(QEvent::MouseMove,QPointF(200,200),QPointF(200,200),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&overlay,&move);
        const auto before=overlay.grab().toImage();
        QTest::qWait(320);
        const auto after=overlay.grab().toImage();
        QVERIFY(after.pixelColor(227,250).lightness()<100);
        QVERIFY(before.pixelColor(227,250).lightness()<100);
        QTest::mousePress(&overlay,Qt::LeftButton,Qt::NoModifier,QPoint(200,200));
        QMouseEvent drag(QEvent::MouseMove,QPointF(300,300),QPointF(300,300),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&overlay,&drag);
        const auto rendered=overlay.grab().toImage();
        QDir().mkpath("../artifacts/capture-preview");
        rendered.save("../artifacts/capture-preview/magnifier.png");
    }
    void overlappingWindowsNeverSelectBehindFront() {
        ScreenFrame frame;
        frame.image=QImage(800,600,QImage::Format_RGB32); frame.image.fill(Qt::white);
        frame.logicalGeometry=QRect(0,0,800,600); frame.windowScopeAvailable=true;
        frame.frontWindows={{QRect(100,100,200,200),manualTarget()}, {QRect(0,0,500,500),manualTarget()}};
        Overlay overlay(frame); overlay.show();
        QSignalSpy accepted(&overlay,&Overlay::accepted);
        QTest::mouseMove(&overlay,QPoint(400,400));
        QTest::mouseMove(&overlay,QPoint(150,150));
        QTest::mouseClick(&overlay,Qt::LeftButton,Qt::NoModifier,QPoint(150,150));
        // A click no longer ends the capture: it settles the region and the bar beside
        // it offers what to do next, so the picture only reaches the editor once 批注
        // is chosen.
        QCOMPARE(accepted.count(),0);
        QTest::keyClick(&overlay,Qt::Key_Return);
        QCOMPARE(accepted.count(),1);
        QCOMPARE(accepted.first().first().toRect(),QRect(100,100,200,200));
    }
    void magnifierToggleDefaultsOn() {
        Editor editor;
        auto button=editor.findChild<QPushButton *>("toggleMagnifier");
        auto canvas=editor.findChild<Canvas *>();
        QVERIFY(button && canvas); QVERIFY(button->isChecked()); QVERIFY(canvas->magnifierEnabled());
        button->click(); QVERIFY(!canvas->magnifierEnabled());
        button->click(); QVERIFY(canvas->magnifierEnabled());
        auto doc=fromImage(QImage(800,600,QImage::Format_RGB32),"file","预览");
        doc.image.fill(Qt::white); doc.png=encodePng(doc.image);
        canvas->setDocument(&doc); canvas->setMode(Canvas::Rectangle); canvas->setZoom(1);
        canvas->show(); editor.resize(1100,800); editor.show();
        QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(100,100));
        QMouseEvent move(QEvent::MouseMove,QPointF(300,250),QPointF(300,250),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(canvas,&move);
        QDir().mkpath("../artifacts/capture-preview");
        canvas->grab().save("../artifacts/capture-preview/editor-magnifier.png");
    }
    void captureArrowAdjustmentSurvivesRelease() {
        ScreenFrame frame;
        frame.image=QImage(600,400,QImage::Format_RGB32);
        frame.image.fill(Qt::white);
        frame.logicalGeometry=QRect(0,0,300,200);
        Overlay overlay(frame);
        overlay.show();
        QSignalSpy accepted(&overlay,&Overlay::accepted);
        QTest::mousePress(&overlay,Qt::LeftButton,Qt::NoModifier,QPoint(20,20));
        QMouseEvent move(QEvent::MouseMove,QPointF(80,60),QPointF(80,60),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&overlay,&move);
        QTest::keyClick(&overlay,Qt::Key_Right);
        QTest::keyClick(&overlay,Qt::Key_Down);
        QTest::mouseRelease(&overlay,Qt::LeftButton,Qt::NoModifier,QPoint(80,60));
        // Releasing keeps the region and the arrows that were pressed while it was
        // being drawn are still part of it; 批注 is what hands it on.
        QCOMPARE(accepted.count(),0);
        QTest::keyClick(&overlay,Qt::Key_Return);
        QCOMPARE(accepted.count(),1);
        QCOMPARE(accepted.first().first().toRect(),dragRect(QPoint(40,40),QPoint(161,121),frame.image.size()));
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
    void annotationShortcutOpensEmptyWindow() {
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        auto shortcut = controller.findChild<GlobalShortcut *>("annotateShortcut");
        auto trayAction = editor ? editor->findChild<QAction *>("trayAnnotate") : nullptr;
        QVERIFY(editor && shortcut && trayAction);
        QVERIFY(!editor->isVisible());
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QVERIFY(editor->isVisible());
        QVERIFY(!editor->hasDocument());
        QVERIFY(editor->findChild<QWidget *>("emptyWell")->isVisible());

        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        editor->setDocument(fromImage(picture, "file", "现有图片"));
        editor->hide();
        trayAction->trigger();
        QVERIFY(editor->isVisible());
        QVERIFY(!editor->hasDocument());
        editor->hide();
    }
    void annotationShortcutRespectsDiscardDecision_data() {
        QTest::addColumn<bool>("discard");
        QTest::newRow("cancel-keeps-edits") << false;
        QTest::newRow("discard-opens-empty") << true;
    }
    void annotationShortcutRespectsDiscardDecision() {
        QFETCH(bool, discard);
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        auto shortcut = controller.findChild<GlobalShortcut *>("annotateShortcut");
        QVERIFY(editor && shortcut);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        auto document = fromImage(picture, "file", "尚未保存的批注");
        Note note;
        note.isGlobal = true;
        note.comment = "保留这条批注";
        document.notes.append(note);
        document.dirty = true;
        editor->setDocument(document);
        bool prompted = false;
        QTimer::singleShot(0, [&] {
            auto question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(question);
            prompted = true;
            question->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
        });
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QVERIFY(prompted);
        if (discard) {
            QVERIFY(editor->isVisible());
            QVERIFY(!editor->hasDocument());
            QVERIFY(editor->findChild<QWidget *>("emptyWell")->isVisible());
        } else {
            QVERIFY(editor->hasDocument());
            QCOMPARE(editor->document().id, document.id);
            QCOMPARE(editor->document().png, document.png);
            QVERIFY(editor->document().notes == document.notes);
            QVERIFY(editor->document().dirty);
        }
        editor->hide();
    }
    void annotationShortcutPreservesActiveAgentSession() {
        QTemporaryDir directory;
        const auto input = directory.filePath("input.png");
        const auto output = directory.filePath("feedback.json");
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        QVERIFY(picture.save(input));
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        auto shortcut = controller.findChild<GlobalShortcut *>("annotateShortcut");
        QVERIFY(editor && shortcut);
        const auto response = controller.handleAgentRequest({{"command", "annotate"}, {"input", input},
                                                              {"output", output}, {"timeout", 60}});
        QVERIFY(response.value("pending").toBool());
        const auto id = editor->document().id;
        QSignalSpy finished(&controller, &Controller::agentSessionFinished);
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QVERIFY(editor->hasDocument());
        QCOMPARE(editor->document().id, id);
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("agentSession").toBool());
        QCOMPARE(finished.count(), 0);
        QVERIFY(!QFileInfo::exists(output));
        controller.cancelAgentSession(response.value("session").toString(), "cancelled", "Test cleanup");
        editor->hide();
    }
    void annotationShortcutDoesNotInterruptModalOrCapture() {
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        auto shortcut = controller.findChild<GlobalShortcut *>("annotateShortcut");
        QVERIFY(editor && shortcut);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        const auto document = fromImage(picture, "file", "正在使用的图片");
        editor->setDocument(document);
        editor->hide();
        QDialog modal(editor);
        modal.setModal(true);
        modal.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &modal);
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(!editor->isVisible());
        modal.reject();
        QTRY_VERIFY(!QApplication::activeModalWidget());

        // Trigger while the capture preparation is queued; no screen read is needed.
        controller.capture();
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(!editor->isVisible());
    }
    void annotationShortcutSettingsApplyIsTransactional_data() {
        QTest::addColumn<bool>("saveChanges");
        QTest::newRow("native-and-cancel") << false;
        QTest::newRow("save-rollback-disable") << true;
    }
    void annotationShortcutSettingsApplyIsTransactional() {
        QFETCH(bool, saveChanges);
#ifdef Q_OS_WIN
        QString startupError;
        const bool hasLoginItem = launchAtLoginEnabled(&startupError);
        if (saveChanges && (!startupError.isEmpty() || hasLoginItem))
            QSKIP("Settings save integration requires no existing login item to preserve the user's registration.");
        QTemporaryDir directory;
        const auto settingsFile = directory.filePath("settings.ini");
        const QKeySequence originalCapture("Ctrl+Alt+Shift+F17", QKeySequence::PortableText);
        const QKeySequence originalAnnotate("Ctrl+Alt+Shift+F18", QKeySequence::PortableText);
        const QKeySequence occupied("Ctrl+Alt+Shift+F19", QKeySequence::PortableText);
        const QKeySequence replacement("Ctrl+Alt+Shift+F20", QKeySequence::PortableText);
        auto settings = quietSettings();
        settings.language = LanguageMode::SimplifiedChinese;
        settings.shortcuts["capture"] = originalCapture;
        settings.shortcuts["annotate"] = originalAnnotate;
        QVERIFY(saveSettings(settings, nullptr, settingsFile));
        Controller controller(nullptr, settings, settingsFile);
        auto editor = editorOf(controller);
        auto capture = controller.findChild<GlobalShortcut *>("captureShortcut");
        auto annotate = controller.findChild<GlobalShortcut *>("annotateShortcut");
        auto trayAction = editor ? editor->findChild<QAction *>("trayAnnotate") : nullptr;
        QVERIFY(editor && capture && annotate && trayAction);
        QCOMPARE(capture->sequence(), originalCapture);
        QCOMPARE(annotate->sequence(), originalAnnotate);
        auto press = [](WORD key) {
            const WORD keys[] = {VK_CONTROL, VK_MENU, VK_SHIFT, key};
            INPUT inputs[8]{};
            for (int index = 0; index < 4; ++index) {
                inputs[index].type = INPUT_KEYBOARD;
                inputs[index].ki.wVk = keys[index];
                inputs[index + 4].type = INPUT_KEYBOARD;
                inputs[index + 4].ki.wVk = keys[3 - index];
                inputs[index + 4].ki.dwFlags = KEYEVENTF_KEYUP;
            }
            return SendInput(8, inputs, sizeof(INPUT));
        };
        QCOMPARE(press(VK_F18), UINT(8));
        QTRY_VERIFY_WITH_TIMEOUT(editor->isVisible(), 1500);
        QVERIFY(!editor->hasDocument());

        auto editBindings = [&](const QKeySequence &captureKey, const QKeySequence &annotateKey,
                                bool save, bool expectConflict = false) {
            QTimer::singleShot(0, [&] {
                auto dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QTimer::singleShot(1500, dialog, &QDialog::reject);
                QVERIFY(capture->sequence().isEmpty());
                QVERIFY(annotate->sequence().isEmpty());
                dialog->findChild<QKeySequenceEdit *>("shortcut_capture")->setKeySequence(captureKey);
                dialog->findChild<QKeySequenceEdit *>("shortcut_annotate")->setKeySequence(annotateKey);
                if (save) {
                    dialog->findChild<QPushButton *>("settingsSave")->click();
                    if (expectConflict) {
                        QVERIFY(dialog->isVisible());
                        auto error = dialog->findChild<QLabel *>("errorLabel");
                        QVERIFY(error && error->isVisible());
                        QVERIFY(!error->text().isEmpty());
                        dialog->reject();
                    }
                } else {
                    dialog->reject();
                }
            });
            controller.openSettings();
        };
        editBindings(replacement, occupied, false);
        QCOMPARE(capture->sequence(), originalCapture);
        QCOMPARE(annotate->sequence(), originalAnnotate);
        QCOMPARE(loadSettings(settingsFile).shortcuts, settings.shortcuts);
        if (!saveChanges) {
            editor->hide();
            return;
        }

        // Both registrations must be released before saving a swap.
        editBindings(originalAnnotate, originalCapture, true);
        QCOMPARE(capture->sequence(), originalAnnotate);
        QCOMPARE(annotate->sequence(), originalCapture);
        const auto saved = loadSettings(settingsFile);
        QCOMPARE(saved.shortcuts.value("capture"), originalAnnotate);
        QCOMPARE(saved.shortcuts.value("annotate"), originalCapture);
        QVERIFY(trayAction->text().contains(originalCapture.toString(QKeySequence::NativeText)));

        GlobalShortcut blocker;
        QVERIFY2(blocker.start(occupied), qPrintable(blocker.lastError()));
        editBindings(replacement, occupied, true, true);
        QCOMPARE(capture->sequence(), originalAnnotate);
        QCOMPARE(annotate->sequence(), originalCapture);
        QVERIFY(loadSettings(settingsFile) == saved);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        editor->setDocument(fromImage(picture, "file", "注册恢复后打开空窗口"));
        editor->hide();
        QCOMPARE(press(VK_F17), UINT(8));
        QTRY_VERIFY_WITH_TIMEOUT(editor->isVisible() && !editor->hasDocument(), 1500);

        editBindings(originalAnnotate, {}, true);
        QVERIFY(annotate->sequence().isEmpty());
        QVERIFY(loadSettings(settingsFile).shortcuts.value("annotate").isEmpty());
        QVERIFY(!trayAction->text().contains(originalCapture.toString(QKeySequence::NativeText)));
        GlobalShortcut released;
        QVERIFY2(released.start(originalCapture), qPrintable(released.lastError()));
        editor->hide();
#endif
    }
};
QTEST_MAIN(StartupFlowTests)
#include "startup_flow_test.moc"
