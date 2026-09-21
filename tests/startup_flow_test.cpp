#include "controller.h"
#include "overlay.h"
#include "guide.h"
#include "settingsdialog.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
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
};
QTEST_MAIN(StartupFlowTests)
#include "startup_flow_test.moc"
