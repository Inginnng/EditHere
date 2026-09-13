#include "editor.h"
#include "explosion.h"
#include "guide.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWheelEvent>
using namespace h2d;

class GuideTests final : public QObject {
    Q_OBJECT
    static Document documentWithFeedback() {
        QImage image(800, 600, QImage::Format_ARGB32);
        image.fill(QColor("#eef1f6"));
        auto document = fromImage(image, "file", "保留的设计稿");
        document.candidates.append({QRect(40, 40, 200, 100), manualTarget()});
        document.layout = createLayout(image.size(), document.candidates);
        Note note;
        note.isGlobal = true;
        note.comment = "保留品牌色，并增加标题留白。";
        document.notes.append(note);
        document.dirty = true;
        return document;
    }
    static QJsonObject documentState(const Document &document) {
        auto state = QJsonDocument::fromJson(serializeDocument(document, true)).object();
        state.remove("exportedAt");
        state.insert("dirty", document.dirty);
        return state;
    }
    static QPushButton *button(Editor &editor, const char *name) {
        return editor.findChild<QPushButton *>(QString::fromLatin1(name));
    }
    static void artifact(Editor &editor, const QString &name) {
        const QString directory = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (directory.isEmpty()) return;
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(editor.grab().save(QDir(directory).filePath(name)));
    }
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
        applyTheme(ThemeMode::Light);
    }
    void repeatedEntrancesReuseOverlayAndKeepDocument() {
        Editor editor;
        QSignalSpy dismissed(&editor, &Editor::guideDismissed);
        editor.showGuide();
        QVERIFY(editor.hasDocument());
        QCOMPARE(editor.document().source, QString("demo"));
        QCOMPARE(editor.document().title, QString("引导示例"));
        const QString id = editor.document().id;
        auto guide = editor.findChild<GuideOverlay *>();
        QVERIFY(guide && editor.guideActive());
        button(editor, "guideNext")->click();
        QCOMPARE(guide->currentStep(), 1);
        editor.showGuide();
        QCOMPARE(guide->currentStep(), 0);
        QCOMPARE(editor.findChildren<GuideOverlay *>().size(), 1);
        QCOMPARE(editor.document().id, id);
        QCOMPARE(dismissed.count(), 0);
        button(editor, "guideSkip")->click();
        QVERIFY(!editor.guideActive());
        QCOMPARE(dismissed.count(), 1);
        QTest::mouseClick(button(editor, "showGuide"), Qt::LeftButton);
        QVERIFY(editor.guideActive());
        QCOMPARE(guide->currentStep(), 0);
        QCOMPARE(editor.document().id, id);
        editor.dismissGuide();
        editor.dismissGuide();
        QCOMPARE(dismissed.count(), 2);
        QVERIFY(editor.hasDocument());
    }
    void navigationEscapeAndCompletionDoNotCloseDocument() {
        Editor editor;
        editor.setDocument(documentWithFeedback());
        QTest::qWait(40);
        const auto before = documentState(editor.document());
        QSignalSpy dismissed(&editor, &Editor::guideDismissed);
        QSignalSpy closed(&editor, &Editor::hiddenToTray);
        editor.canvas()->setFocus();
        editor.showGuide();
        auto guide = editor.findChild<GuideOverlay *>();
        QVERIFY(guide);
        QVERIFY(!button(editor, "guideBack")->isEnabled());
        button(editor, "guideNext")->click();
        QCOMPARE(guide->currentStep(), 1);
        button(editor, "guideBack")->click();
        QCOMPARE(guide->currentStep(), 0);
        QTest::keyClick(button(editor, "guideNext"), Qt::Key_Escape);
        QVERIFY(editor.isVisible() && editor.hasDocument() && !editor.guideActive());
        QCOMPARE(closed.count(), 0);
        QCOMPARE(dismissed.count(), 1);
        QCOMPARE(documentState(editor.document()), before);
        QVERIFY(editor.canvas()->hasFocus());
        editor.showGuide();
        for (int i = 1; i < guide->stepCount(); ++i) {
            button(editor, "guideNext")->click();
            QCOMPARE(guide->currentStep(), i);
        }
        QCOMPARE(button(editor, "guideNext")->text(), QString("完成"));
        button(editor, "guideNext")->click();
        QVERIFY(!editor.guideActive());
        QCOMPARE(dismissed.count(), 2);
        QCOMPARE(closed.count(), 0);
        QCOMPARE(documentState(editor.document()), before);
    }
    void guideCommitsDraftAndBlocksEditingAndShortcuts() {
        Editor editor;
        editor.setDocument(documentWithFeedback());
        QTest::qWait(40);
        button(editor, "addGlobalNote")->click();
        const QString id = editor.document().notes.last().id;
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + id);
        QVERIFY(input);
        input->setFocus();
        input->setPlainText("  让画面更轻盈，保留原有层级。  ");
        editor.showGuide();
        QCOMPARE(editor.document().notes.last().comment, QString("让画面更轻盈，保留原有层级。"));
        const auto before = documentState(editor.document());
        const auto mode = editor.canvas()->mode();
        const double zoom = editor.canvas()->zoom();
        QTest::mouseClick(button(editor, "addGlobalNote"), Qt::LeftButton);
        QTest::keyClick(editor.canvas(), Qt::Key_P);
        QTest::keyClick(button(editor, "guideNext"), Qt::Key_Delete);
        QTest::keyClick(button(editor, "guideNext"), Qt::Key_Z, Qt::ControlModifier);
        QWheelEvent wheel(QPointF(20, 20), QPointF(editor.canvas()->mapToGlobal(QPoint(20, 20))), {},
                          QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(editor.canvas(), &wheel);
        QCOMPARE(editor.canvas()->mode(), mode);
        QCOMPARE(editor.canvas()->zoom(), zoom);
        QCOMPARE(documentState(editor.document()), before);
        for (int i = 0; i < 9; ++i) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QVERIFY(editor.findChild<GuideOverlay *>()->isAncestorOf(QApplication::focusWidget()));
        }
        editor.dismissGuide();
        QVERIFY(input->hasFocus());
        editor.canvas()->setFocus();
        QTest::keyClick(editor.canvas(), Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(editor.document().notes.size(), 1);
    }
    void separateWindowsOwnedByEditorStayUsable() {
        Editor editor;
        editor.setDocument(documentWithFeedback());
        QTest::qWait(40);
        editor.showGuide();
        const auto before = documentState(editor.document());
        QMenu menu(&editor);
        auto action = menu.addAction("托盘操作");
        QSignalSpy triggered(action, &QAction::triggered);
        menu.popup(editor.mapToGlobal(QPoint(100, 80)));
        QTRY_VERIFY(menu.isVisible());
        QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier, menu.actionGeometry(action).center());
        QCOMPARE(triggered.count(), 1);
        QVERIFY(editor.guideActive());

        QDialog dialog(&editor);
        dialog.setModal(true);
        auto layout = new QVBoxLayout(&dialog);
        auto input = new QLineEdit(&dialog);
        auto accept = textButton("确定", true, &dialog);
        layout->addWidget(input);
        layout->addWidget(accept);
        connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
        auto shortcut = new QShortcut(QKeySequence(Qt::Key_F6), &dialog);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        QSignalSpy shortcutActivated(shortcut, &QShortcut::activated);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.show();
        dialog.activateWindow();
        input->setFocus();
        QTRY_VERIFY(input->hasFocus());
        QTest::keyClicks(input, "settings remain usable");
        QCOMPARE(input->text(), QString("settings remain usable"));
        QTest::keyClick(input, Qt::Key_F6);
        QCOMPARE(shortcutActivated.count(), 1);
        QTest::mouseClick(accept, Qt::LeftButton);
        QCOMPARE(accepted.count(), 1);
        QVERIFY(editor.guideActive());
        QTest::mouseClick(button(editor, "addGlobalNote"), Qt::LeftButton);
        QCOMPARE(documentState(editor.document()), before);
        editor.dismissGuide();
    }
    void anActiveLayoutAndHiddenNotesRemainUntouched() {
        Editor editor;
        editor.setDocument(documentWithFeedback());
        QTest::qWait(40);
        editor.explode();
        QVERIFY(editor.explosionActive());
        auto layoutCanvas = editor.layoutCanvas();
        QVERIFY(layoutCanvas);
        button(editor, "hideAnnotations")->click();
        button(editor, "collapseNotes")->click();
        const auto before = documentState(editor.document());
        const auto layout = layoutCanvas->state();
        const auto mode = editor.canvas()->mode();
        editor.showGuide();
        auto guide = editor.findChild<GuideOverlay *>();
        button(editor, "guideNext")->click();
        button(editor, "guideNext")->click();
        QCOMPARE(guide->highlightedControls(), QStringList{"collapseNotes"});
        for (int i = 3; i < guide->stepCount(); ++i) button(editor, "guideNext")->click();
        button(editor, "guideNext")->click();
        QVERIFY(editor.explosionActive());
        QCOMPARE(editor.layoutCanvas(), layoutCanvas);
        QVERIFY(layoutCanvas->state() == layout);
        QCOMPARE(editor.canvas()->mode(), mode);
        QVERIFY(!layoutCanvas->annotationsVisible());
        QVERIFY(editor.findChild<QWidget *>("detailsStack")->isHidden());
        QCOMPARE(documentState(editor.document()), before);
    }
    void themedCardsAndHighlightsStayInsideWindow_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("narrow");
        QTest::newRow("light") << false << false;
        QTest::newRow("dark") << true << false;
        QTest::newRow("light-narrow") << false << true;
        QTest::newRow("dark-narrow") << true << true;
    }
    void themedCardsAndHighlightsStayInsideWindow() {
        QFETCH(bool, dark);
        QFETCH(bool, narrow);
        applyTheme(dark ? ThemeMode::Dark : ThemeMode::Light);
        Editor editor;
        auto settings = defaultSettings();
        if (narrow) settings.toolbarActions = {"copyJson"};
        editor.setPreferences(settings);
        editor.setDocument(fromImage(exampleImage(), "demo", "引导示例"));
        editor.resize(narrow ? QSize(740, 400) : QSize(1180, 760));
        QTest::qWait(60);
        editor.showGuide();
        auto guide = editor.findChild<GuideOverlay *>();
        auto card = editor.findChild<QWidget *>("guideCard");
        QVERIFY(guide && card);
        for (int step = 0; step < guide->stepCount(); ++step) {
            QTest::qWait(160);
            QCOMPARE(guide->currentStep(), step);
            QCOMPARE(guide->geometry(), editor.rect());
            QVERIFY(editor.rect().contains(card->geometry()));
            QVERIFY(!guide->highlightRects().isEmpty());
            for (const QRect &spot : guide->highlightRects()) {
                QVERIFY(editor.rect().contains(spot));
                QVERIFY(!card->geometry().intersects(spot));
            }
            for (auto action : card->findChildren<QPushButton *>())
                QVERIFY(card->rect().contains(action->geometry()));
            if (step == 4 && narrow)
                QCOMPARE(guide->highlightedControls(), QStringList{"moreActions"});
            const QString name = QString("guide-%1-%2.png").arg(QTest::currentDataTag()).arg(step + 1);
            artifact(editor, name);
            if (step + 1 < guide->stepCount()) button(editor, "guideNext")->click();
        }
        editor.dismissGuide();
    }
};
QTEST_MAIN(GuideTests)
#include "guide_test.moc"