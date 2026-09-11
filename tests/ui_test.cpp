#include "editor.h"
#include "overlay.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
using namespace h2d;
class UiTests : public QObject {
    Q_OBJECT
  private:
    void artifact(QWidget &widget, const QString &name) {
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            QVERIFY(widget.grab().save(QDir(folder).filePath(name)));
        }
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "msyh.ttc") >= 0);
        }
#endif
        applyTheme();
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4('A'));
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4(0x4e2d));
    }
    void pointAndFrame() {
        applyTheme();
        Editor editor;
        editor.setDocument(fromImage(exampleImage(), "demo", "示例"));
        editor.resize(1240, 820);
        QTest::qWait(80);
        Canvas *canvas = editor.canvas();
        canvas->setMode(Canvas::Point);
        auto complete = [this] {
            auto dialog = QApplication::activeModalWidget();
            QVERIFY(dialog);
            auto text = dialog->findChild<QPlainTextEdit *>("commentInput");
            QVERIFY(text);
            QTest::keyClicks(text, "Refine the button spacing. ");
            text->insertPlainText("标题字重调轻，卡片间距保持一致。");
            artifact(*dialog, "note-dialog.png");
            for (auto b : dialog->findChildren<QPushButton *>())
                if (b->text() == "保存") {
                    b->click();
                    return;
                }
            QFAIL("Save button missing");
        };
        QTimer::singleShot(80, complete);
        QPoint target(qRound(200 * canvas->zoom()), qRound(100 * canvas->zoom()));
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, target);
        QCOMPARE(editor.document().notes.size(), 1);
        QVERIFY(editor.document().notes.first().comment.startsWith("Refine"));
        QVERIFY((editor.document().notes[0].point - QPoint(200, 100)).manhattanLength() <= 2);
        canvas->setMode(Canvas::Rectangle);
        QPoint start(qRound(100 * canvas->zoom()), qRound(250 * canvas->zoom()));
        QPoint end(qRound(400 * canvas->zoom()), qRound(600 * canvas->zoom()));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end);
        QTimer::singleShot(80, complete);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(editor.document().notes.size(), 2);
        QVERIFY(!editor.document().notes.last().isPoint);
        QVERIFY(editor.document().notes.last().rect.width() > 290);
        canvas->setMode(Canvas::Adjust);
        QTest::qWait(40);
        artifact(editor, "editor.png");
        auto note = editor.document().notes.last();
        QPoint handle(qRound((note.rect.x() + note.rect.width()) * canvas->zoom()),
                      qRound((note.rect.y() + note.rect.height()) * canvas->zoom()));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, handle);
        QTest::mouseMove(canvas, handle + QPoint(20, 10));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, handle + QPoint(20, 10));
        QVERIFY(editor.document().notes.last().rect.width() > note.rect.width());
        QTimer::singleShot(80, [this] {
            auto dialog = QApplication::activeModalWidget();
            QVERIFY(dialog);
            artifact(*dialog, "export-dialog.png");
            dialog->close();
        });
        editor.exportJson();
        editor.hide();
    }
    void screenCropUsesImagePixels() {
        QImage image = exampleImage();
        ScreenFrame frame{"test", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QSignalSpy accepted(&overlay, &Overlay::accepted);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {30, 70});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        artifact(overlay, "crop-overlay.png");
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().first().toRect(), QRect(60, 140, 540, 380));
        overlay.hide();
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
