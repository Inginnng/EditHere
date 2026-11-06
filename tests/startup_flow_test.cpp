#include "controller.h"
#include "autostart.h"
#include "overlay.h"
#include "guide.h"
#include "settingsdialog.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
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
#include <shellapi.h>
#endif
using namespace h2d;
namespace h2d {
class StartupFlowTestAccess {
  public:
    static void deliver(Controller &controller, const UpdateChecker::Result &result) {
        controller.startupUpdateFinished(result);
    }
    static bool pending(const Controller &controller) { return controller.pendingStartupUpdate_.has_value(); }
    static void present(Controller &controller) { controller.presentStartupUpdate(); }
    static void setCapture(Controller &controller, bool value) { controller.capturing_ = value; }
    static void holdCapture(Controller &controller) {
        controller.captureIo_.prepare = [](QObject *, std::function<void()>, bool) {};
    }
    static void countCaptures(Controller &controller, int &count) {
        controller.captureIo_.prepare = [&count](QObject *, std::function<void()>, bool) { ++count; };
    }
    static void setAgentSession(Controller &controller, bool value) {
        controller.agentSessionId_ = value ? QStringLiteral("test-session") : QString();
    }
    static void receipt(Controller &controller) { controller.readInstallationReceipt(); }
    static void presentReceipt(Controller &controller) { controller.presentInstallationReceipt(); }
    static bool pendingReceipt(Controller &controller) { return !controller.pendingInstallationReceipt_.isEmpty(); }
};
}

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
    static bool annotation(Controller &controller, GlobalShortcut *shortcut, bool viaTray) {
        if (!viaTray) return QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        if (!tray) return false;
        emit tray->activated(QSystemTrayIcon::Trigger);
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        return true;
    }
    static UpdateChecker::Result offeredRelease(UpdateChecker::Status status = UpdateChecker::Available) {
        const QString name = QStringLiteral("EditHere-win-x64-setup.exe");
        const QUrl url(QStringLiteral("https://gitee.com/InnGing/EditHere/releases/download/v0.11.0/") + name);
        return {status, QStringLiteral("发现新版本 v0.11.0。"),
                QUrl("https://gitee.com/InnGing/EditHere/releases/tag/v0.11.0"), QStringLiteral("v0.11.0"),
                {name, url, 100}, {name + ".sha256", QUrl(url.toString() + ".sha256"), 64}, {}, {}, true};
    }
  private slots:
    void initTestCase() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
            QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
        }
#endif
        applyTheme(ThemeMode::Light);
    }
    void startupUpdateDefaultPreservesExplicitOptOut() {
        QTemporaryDir directory;
        const auto settingsFile = directory.filePath("settings.ini");
        QVERIFY(defaultSettings().checkUpdatesOnStartup);
        QVERIFY(loadSettings(settingsFile).checkUpdatesOnStartup);
        auto settings = quietSettings();
        QVERIFY(saveSettings(settings, nullptr, settingsFile));
        QVERIFY(!loadSettings(settingsFile).checkUpdatesOnStartup);
        Controller controller(nullptr, loadSettings(settingsFile), settingsFile);
        controller.start(false);
        QVERIFY(!controller.findChild<UpdateChecker *>("startupUpdateChecker"));
    }
    void failedInstallationReceiptIsShownOnceWithoutDiscardingWorkOrEnablingUpdates() {
        QTemporaryDir directory;
        const auto settingsFile = directory.filePath("settings.ini");
        const auto resultsPath = directory.filePath("installer-results");
        QVERIFY(QDir().mkpath(resultsPath));
        const QJsonObject receipt{{"id", "isolated-failed-installation"}, {"status", "failed"},
                                  {"version", "0.11.0"}, {"target", QCoreApplication::applicationDirPath()},
                                  {"message", "Update rolled back; recoverable files remain in the backup directory."},
                                  {"log", directory.filePath("maintenance.log")},
                                  {"journal", directory.filePath("journal.json")},
                                  {"recovery", directory.filePath("backup")}};
        QFile file(QDir(resultsPath).filePath("last.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QJsonDocument(receipt).toJson()) > 0);
        file.close();
        auto settings = quietSettings();
        QVERIFY(saveSettings(settings, nullptr, settingsFile));
        Controller controller(nullptr, settings, settingsFile);
        auto editor = editorOf(controller);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        auto document = fromImage(picture, "file", "安装提醒时保留的工作");
        document.dirty = true;
        editor->setDocument(document);
        controller.start(false, {}, true);
        QVERIFY(!QApplication::activeModalWidget());
        StartupFlowTestAccess::setCapture(controller, true);
        StartupFlowTestAccess::receipt(controller);
        QVERIFY(StartupFlowTestAccess::pendingReceipt(controller));
        QVERIFY(!QApplication::activeModalWidget());
        StartupFlowTestAccess::setCapture(controller, false);
        bool prompted = false;
        QTimer::singleShot(0, [&] {
            auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt && prompt->objectName() == "installationResultPrompt");
            QCOMPARE(prompt->text(), receipt.value("message").toString());
            QVERIFY(prompt->detailedText().contains(receipt.value("recovery").toString()));
            prompted = true;
            prompt->accept();
        });
        StartupFlowTestAccess::presentReceipt(controller);
        QVERIFY(prompted);
        QVERIFY(!StartupFlowTestAccess::pendingReceipt(controller));
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(editor->document().dirty);
        QVERIFY(!controller.findChild<UpdateChecker *>("startupUpdateChecker"));
        QVERIFY(!loadSettings(settingsFile).checkUpdatesOnStartup);
        QSettings persisted(settingsFile, QSettings::IniFormat);
        QCOMPARE(persisted.value("updates/seenInstallerResult").toString(), receipt.value("id").toString());
        StartupFlowTestAccess::receipt(controller);
        QVERIFY(!StartupFlowTestAccess::pendingReceipt(controller));
        Controller nextLaunch(nullptr, settings, settingsFile);
        StartupFlowTestAccess::receipt(nextLaunch);
        QVERIFY(!StartupFlowTestAccess::pendingReceipt(nextLaunch));
        QVERIFY(!QApplication::activeModalWidget());
        editor->hide();
    }
    void startupUpdateWaitsForFirstUserInteractionAndSchedulesOnce() {
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.checkUpdatesOnStartup = true;
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        controller.start(false, {}, true);
        QVERIFY(!controller.findChild<UpdateChecker *>("startupUpdateChecker"));
        auto shortcut = controller.findChild<GlobalShortcut *>("annotateShortcut");
        QVERIFY(shortcut);
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        auto checker = controller.findChild<UpdateChecker *>("startupUpdateChecker");
        QVERIFY(checker);
        QVERIFY(!checker->busy()); // Check runs after the startup delay, not in the user action.
        controller.activate();
        controller.start(false);
        QCOMPARE(controller.findChildren<UpdateChecker *>("startupUpdateChecker").size(), 1);
        editorOf(controller)->hide();
    }
    void startupUpdateDoesNotPromptWithoutANewerRelease_data() {
        QTest::addColumn<int>("status");
        QTest::newRow("current") << int(UpdateChecker::Current);
        QTest::newRow("failed") << int(UpdateChecker::Failed);
        QTest::newRow("newer-local") << int(UpdateChecker::NewerLocal);
    }
    void startupUpdateDoesNotPromptWithoutANewerRelease() {
        QFETCH(int, status);
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.checkUpdatesOnStartup = true;
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        StartupFlowTestAccess::deliver(controller, offeredRelease(static_cast<UpdateChecker::Status>(status)));
        QVERIFY(!StartupFlowTestAccess::pending(controller));
        QVERIFY(!QApplication::activeModalWidget());
    }
    void skippingStartupUpdatePreservesUnsavedWork_data() {
        QTest::addColumn<bool>("closeWindow");
        QTest::newRow("skip") << false;
        QTest::newRow("close") << true;
    }
    void skippingStartupUpdatePreservesUnsavedWork() {
        QFETCH(bool, closeWindow);
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.checkUpdatesOnStartup = true;
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QVERIFY(editor);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        auto document = fromImage(picture, "file", "尚未保存的批注");
        Note note;
        note.isGlobal = true;
        note.comment = "保留这条批注";
        document.notes.append(note);
        document.dirty = true;
        editor->setDocument(document);
        controller.start(false);
        bool prompted = false;
        QTimer::singleShot(0, [&] {
            auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt && prompt->objectName() == "startupUpdatePrompt");
            prompted = true;
            const auto artifacts = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
            if (!artifacts.isEmpty() && !closeWindow) {
                QDir().mkpath(artifacts);
                QVERIFY(prompt->grab().save(QDir(artifacts).filePath("startup-update-prompt.png")));
            }
            if (closeWindow)
                prompt->close();
            else
                for (auto button : prompt->buttons())
                    if (prompt->buttonRole(button) == QMessageBox::RejectRole) button->click();
        });
        StartupFlowTestAccess::deliver(controller, offeredRelease());
        QVERIFY(prompted);
        QVERIFY(!StartupFlowTestAccess::pending(controller));
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(editor->document().notes == document.notes);
        QVERIFY(editor->document().dirty);
        controller.start(false);
        StartupFlowTestAccess::present(controller);
        QCOMPARE(controller.findChildren<UpdateChecker *>("startupUpdateChecker").size(), 1);
        QVERIFY(!QApplication::activeModalWidget());
        editor->hide();
    }
    void startupUpdateDefersDuringActiveWork_data() {
        QTest::addColumn<QString>("activity");
        QTest::newRow("capture") << QStringLiteral("capture");
        QTest::newRow("agent") << QStringLiteral("agent");
        QTest::newRow("modal") << QStringLiteral("modal");
        QTest::newRow("guide") << QStringLiteral("guide");
    }
    void startupUpdateDefersDuringActiveWork() {
        QFETCH(QString, activity);
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.checkUpdatesOnStartup = true;
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QVERIFY(editor);
        QDialog modal(editor);
        if (activity == "capture") StartupFlowTestAccess::setCapture(controller, true);
        if (activity == "agent") StartupFlowTestAccess::setAgentSession(controller, true);
        if (activity == "modal") {
            modal.setModal(true);
            modal.show();
            QTRY_COMPARE(QApplication::activeModalWidget(), &modal);
        }
        if (activity == "guide") editor->showGuide();
        StartupFlowTestAccess::deliver(controller, offeredRelease());
        QVERIFY(StartupFlowTestAccess::pending(controller));
        QVERIFY(!QApplication::activeModalWidget() || QApplication::activeModalWidget() == &modal);
        if (activity == "capture") StartupFlowTestAccess::setCapture(controller, false);
        if (activity == "agent") StartupFlowTestAccess::setAgentSession(controller, false);
        if (activity == "modal") modal.reject();
        if (activity == "guide") editor->dismissGuide();
        bool prompted = false;
        QTimer::singleShot(0, [&] {
            auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt && prompt->objectName() == "startupUpdatePrompt");
            prompted = true;
            prompt->reject();
        });
        StartupFlowTestAccess::present(controller);
        QVERIFY(prompted);
        QVERIFY(!StartupFlowTestAccess::pending(controller));
        editor->hide();
    }
    void acceptingStartupUpdateUsesCheckedReleaseAndCanCancel() {
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.checkUpdatesOnStartup = true;
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QVERIFY(editor);
        QImage picture(120, 80, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        auto document = fromImage(picture, "file", "仍需保留的工作");
        document.dirty = true;
        editor->setDocument(document);
        bool offered = false;
        QTimer::singleShot(0, [&] {
            auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(prompt);
            // Inspect and cancel before the queued download starts: no network or
            // real installer is needed to verify the startup-to-settings handoff.
            QTimer::singleShot(0, [&] {
                auto dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                QVERIFY(dialog->findChild<QLabel *>("updateStatus")->text().startsWith(offeredRelease().message));
                QVERIFY(dialog->findChild<QPushButton *>("openReleases")->property("updateAvailable").toBool());
                auto updater = dialog->findChild<UpdateChecker *>();
                QVERIFY(updater && !updater->busy() && !updater->downloading());
                offered = true;
                dialog->findChild<QPushButton *>("settingsCancel")->click();
            });
            for (auto button : prompt->buttons())
                if (prompt->buttonRole(button) == QMessageBox::AcceptRole) button->click();
        });
        StartupFlowTestAccess::deliver(controller, offeredRelease());
        QVERIFY(offered);
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(editor->document().dirty);
        editor->hide();
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
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(!editor->hasDocument());
        editor->hide();
    }
    void macTrayActivationOnlyOpensTheMenu() {
#ifndef Q_OS_MACOS
        QSKIP("Cocoa reports menu tracking as a tray Trigger.");
#else
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        StartupFlowTestAccess::holdCapture(controller);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        QVERIFY(tray && editor);
        QVERIFY(QMetaObject::invokeMethod(tray, "activated", Qt::DirectConnection,
            Q_ARG(QSystemTrayIcon::ActivationReason, QSystemTrayIcon::Trigger)));
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QVERIFY(!controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        auto capture = editor->findChild<QAction *>("trayCapture");
        QVERIFY(capture);
        capture->trigger();
        QTRY_VERIFY(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
#endif
    }
    void traySingleWaitsAndDoubleOpensEmptyWithoutCapture() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa keeps its native menu; configurable clicks are unavailable.");
#else
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        StartupFlowTestAccess::holdCapture(controller);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        QVERIFY(tray && editor);
        emit tray->activated(QSystemTrayIcon::Trigger);
        QVERIFY(!controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(!editor->hasDocument());
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QVERIFY(!controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        editor->hide();
        emit tray->activated(QSystemTrayIcon::Trigger);
        QTRY_VERIFY_WITH_TIMEOUT(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool(),
                                 QApplication::doubleClickInterval() + 1000);
#endif
    }
    void traySingleCoalescesAndExplicitActionsCancelWaiting() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa keeps the native menu.");
#else
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        int captures = 0;
        StartupFlowTestAccess::countCaptures(controller, captures);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        for (int i = 0; i < 3; ++i) emit tray->activated(QSystemTrayIcon::Trigger);
        QCOMPARE(captures, 0);
        QTRY_COMPARE_WITH_TIMEOUT(captures, 1, QApplication::doubleClickInterval() + 1000);
        StartupFlowTestAccess::setCapture(controller, false);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QCOMPARE(captures, 1);
        emit tray->activated(QSystemTrayIcon::Trigger);
        emit tray->activated(QSystemTrayIcon::Context);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QCOMPARE(captures, 1);
        emit tray->activated(QSystemTrayIcon::Trigger);
        editor->findChild<QAction *>("trayAnnotate")->trigger();
        QTRY_VERIFY(editor->isVisible());
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QCOMPARE(captures, 1);
        emit tray->activated(QSystemTrayIcon::Trigger);
        auto shortcut = controller.findChild<GlobalShortcut *>("captureShortcut");
        QVERIFY(QMetaObject::invokeMethod(shortcut, "triggered", Qt::DirectConnection));
        QCOMPARE(captures, 2); // Explicit shortcut does not wait.
        StartupFlowTestAccess::setCapture(controller, false);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QCOMPARE(captures, 2);
        editor->hide();
#endif
    }
    void trayActionsCanBeSwappedAndDisabled() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa keeps the native menu.");
#else
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.traySingleClick = "annotate";
        settings.trayDoubleClick = "capture";
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        int captures = 0;
        StartupFlowTestAccess::countCaptures(controller, captures);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        emit tray->activated(QSystemTrayIcon::Trigger);
        QTRY_VERIFY_WITH_TIMEOUT(editor->isVisible(), QApplication::doubleClickInterval() + 1000);
        QCOMPARE(captures, 0);
        editor->hide();
        emit tray->activated(QSystemTrayIcon::Trigger);
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QCOMPARE(captures, 1);
        StartupFlowTestAccess::setCapture(controller, false);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QVERIFY(!editor->isVisible());
        settings.traySingleClick = settings.trayDoubleClick = "none";
        Controller disabled(nullptr, settings, directory.filePath("disabled.ini"));
        auto disabledTray = disabled.findChild<QSystemTrayIcon *>("edithereTray");
        emit disabledTray->activated(QSystemTrayIcon::Trigger);
        emit disabledTray->activated(QSystemTrayIcon::DoubleClick);
        emit disabledTray->activated(QSystemTrayIcon::Trigger);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QVERIFY(!editorOf(disabled)->isVisible());
        QVERIFY(!disabled.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
#endif
    }
    void pendingTrayClickCannotOutliveAModalDialog() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa keeps the native menu.");
#else
        QTemporaryDir directory;
        auto settings = quietSettings(); settings.traySingleClick = "annotate";
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        emit tray->activated(QSystemTrayIcon::Trigger);
        QDialog modal(editor); modal.setModal(true); modal.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &modal);
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QVERIFY(!editor->isVisible());
        modal.reject();
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QVERIFY(!editor->isVisible());
#endif
    }
    void trayShowPreservesDocumentAndShowsEmptyWithoutCapture() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa keeps the native menu.");
#else
        QTemporaryDir directory;
        auto settings = quietSettings();
        settings.trayDoubleClick = "show";
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        int captures = 0;
        StartupFlowTestAccess::countCaptures(controller, captures);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        auto editor = editorOf(controller);
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QVERIFY(editor->isVisible());
        QVERIFY(!editor->hasDocument());
        QImage picture(120, 80, QImage::Format_RGB32); picture.fill(Qt::blue);
        auto document = fromImage(picture, "file", "保留当前文档"); document.dirty = true;
        editor->setDocument(document); editor->hide();
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->document().id, document.id);
        QCOMPARE(editor->document().png, document.png);
        QVERIFY(editor->document().dirty);
        QCOMPARE(captures, 0);
        editor->hide();
#endif
    }
    void windowsNativeTrayDoubleReleaseAndThirdClick() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires native Windows Qt tray backend.");
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        int captures = 0;
        StartupFlowTestAccess::countCaptures(controller, captures);
        HWND window = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM out) -> BOOL {
            wchar_t title[64]{}; GetWindowTextW(candidate, title, 64);
            if (QString::fromWCharArray(title) == "QTrayIconMessageWindow") {
                *reinterpret_cast<HWND *>(out) = candidate; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&window));
        QVERIFY(window);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        QSignalSpy activations(tray, &QSystemTrayIcon::activated);
        // Deliver shell notifications through Qt's real HWND and native WndProc,
        // not synthetic activated signals. ID 0 and WM_APP+101 are Qt 6.8.3's contract.
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(NIN_SELECT, 0));
        QCOMPARE(captures, 0);
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(WM_LBUTTONDBLCLK, 0));
        QVERIFY(editorOf(controller)->isVisible());
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(NIN_SELECT, 0));
        QCOMPARE(activations.count(), 2); // Qt consumes second release.
        QTest::qWait(QApplication::doubleClickInterval() + 30);
        QCOMPARE(captures, 0);
        editorOf(controller)->hide();
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(NIN_SELECT, 0));
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(WM_LBUTTONDBLCLK, 0));
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(NIN_SELECT, 0));
        SendMessageW(window, WM_APP + 101, 0, MAKELPARAM(NIN_SELECT, 0));
        QCOMPARE(activations.count(), 5); // Third click is independent, even immediately after double.
        QTRY_COMPARE_WITH_TIMEOUT(captures, 1, QApplication::doubleClickInterval() + 1000);
#else
        QSKIP("Windows-only native shell message test.");
#endif
    }
    void trayOpenUsesTheExistingFilePicker() {
#ifdef Q_OS_MACOS
        QSKIP("Cocoa uses explicit native menu actions.");
#else
        QTemporaryDir directory;
        const auto input = directory.filePath("tray-input.png");
        QImage picture(120, 80, QImage::Format_RGB32); picture.fill(Qt::blue);
        QVERIFY(picture.save(input));
        auto settings = quietSettings(); settings.trayDoubleClick = "open";
        Controller controller(nullptr, settings, directory.filePath("settings.ini"));
        int captures = 0;
        StartupFlowTestAccess::countCaptures(controller, captures);
        bool picked = false;
        QTimer chooser;
        connect(&chooser, &QTimer::timeout, &controller, [&] {
            if (auto dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                picked = true;
                dialog->selectFile(input);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        chooser.start(10);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        emit tray->activated(QSystemTrayIcon::Trigger);
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QTRY_VERIFY(editorOf(controller)->hasDocument());
        QVERIFY(picked);
        QCOMPARE(captures, 0);
        QCOMPARE(editorOf(controller)->document().image.size(), picture.size());
        editorOf(controller)->hide();
#endif
    }
    void openingFromTrayDoesNotInterruptCapture() {
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        StartupFlowTestAccess::holdCapture(controller);
        auto editor = editorOf(controller);
        auto open = editor ? editor->findChild<QAction *>("trayOpen") : nullptr;
        QVERIFY(editor && open);
        controller.capture();
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        bool dialogOpened = false;
        QTimer closeUnexpectedDialog;
        closeUnexpectedDialog.setInterval(10);
        connect(&closeUnexpectedDialog, &QTimer::timeout, [&] {
            for (auto widget : QApplication::topLevelWidgets())
                if (auto dialog = qobject_cast<QFileDialog *>(widget)) {
                    dialogOpened = true;
                    dialog->reject();
                }
        });
        closeUnexpectedDialog.start();
        open->trigger();
        QTest::qWait(50);
        QVERIFY(!dialogOpened);
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        QVERIFY(!editor->isVisible());
    }
    void annotationShortcutRespectsDiscardDecision_data() {
        QTest::addColumn<bool>("discard");
        QTest::addColumn<bool>("viaTray");
        QTest::newRow("cancel-keeps-edits") << false << false;
        QTest::newRow("discard-opens-empty") << true << false;
#ifndef Q_OS_MACOS
        QTest::newRow("tray-cancel-keeps-edits") << false << true;
        QTest::newRow("tray-discard-opens-empty") << true << true;
#endif
    }
    void annotationShortcutRespectsDiscardDecision() {
        QFETCH(bool, discard);
        QFETCH(bool, viaTray);
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
            if (viaTray) {
                // Reentrant tray notifications during the discard question must
                // neither replace work nor leave a delayed capture behind.
                auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
                emit tray->activated(QSystemTrayIcon::DoubleClick);
                emit tray->activated(QSystemTrayIcon::Trigger);
            }
            question->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
        });
        QVERIFY(annotation(controller, shortcut, viaTray));
        QVERIFY(prompted);
        if (viaTray) {
            QTest::qWait(QApplication::doubleClickInterval() + 30);
            QVERIFY(!controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        }
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
    void annotationShortcutPreservesActiveAgentSession_data() {
        QTest::addColumn<bool>("viaTray");
        QTest::newRow("shortcut") << false;
#ifndef Q_OS_MACOS
        QTest::newRow("tray") << true;
#endif
    }
    void annotationShortcutPreservesActiveAgentSession() {
        QFETCH(bool, viaTray);
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
        QVERIFY(annotation(controller, shortcut, viaTray));
        QVERIFY(editor->hasDocument());
        QCOMPARE(editor->document().id, id);
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("agentSession").toBool());
        QCOMPARE(finished.count(), 0);
        QVERIFY(!QFileInfo::exists(output));
        controller.cancelAgentSession(response.value("session").toString(), "cancelled", "Test cleanup");
        editor->hide();
    }
    void annotationShortcutDoesNotInterruptModalOrCapture_data() {
        annotationShortcutPreservesActiveAgentSession_data();
    }
    void annotationShortcutDoesNotInterruptModalOrCapture() {
        QFETCH(bool, viaTray);
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
        QVERIFY(annotation(controller, shortcut, viaTray));
        QCOMPARE(editor->document().id, document.id);
        QVERIFY(!editor->isVisible());
        modal.reject();
        QTRY_VERIFY(!QApplication::activeModalWidget());

        // Trigger while the capture preparation is queued; no screen read is needed.
        controller.capture();
        QVERIFY(controller.handleAgentRequest({{"command", "status"}}).value("capturing").toBool());
        QVERIFY(annotation(controller, shortcut, viaTray));
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
                auto single = dialog->findChild<QComboBox *>("traySingleClick");
                auto twice = dialog->findChild<QComboBox *>("trayDoubleClick");
                QVERIFY(single && twice);
                single->setCurrentIndex(single->findData("none"));
                twice->setCurrentIndex(twice->findData("show"));
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
        QCOMPARE(loadSettings(settingsFile).trayDoubleClick, QString("annotate"));
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
        QCOMPARE(saved.traySingleClick, QString("none"));
        QCOMPARE(saved.trayDoubleClick, QString("show"));
        QImage current(80, 60, QImage::Format_RGB32); current.fill(Qt::blue);
        const auto document = fromImage(current, "file", "设置保存后立即生效");
        editor->setDocument(document); editor->hide();
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        emit tray->activated(QSystemTrayIcon::DoubleClick);
        QVERIFY(editor->isVisible());
        QCOMPARE(editor->document().id, document.id);
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
