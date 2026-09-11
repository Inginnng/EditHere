#include "editor.h"
#include "explosion.h"
#include "overlay.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    Document gridDocument() {
        QImage image(800, 600, QImage::Format_ARGB32);
        image.fill(QColor("#f3f4f7"));
        QPainter painter(&image);
        const QVector<QColor> colors{QColor("#4676c9"), QColor("#4c967c"), QColor("#c67453"),
                                     QColor("#9b70be"), QColor("#557e89"), QColor("#b69848")};
        painter.setFont(QFont(qApp->font().family(), 26, QFont::DemiBold));
        QVector<Candidate> candidates;
        auto candidate = [&](QRect rect, const QString &label) {
            auto target = manualTarget();
            target["source"] = "vision";
            target["label"] = label;
            target["method"] = "test-grid";
            candidates.append({rect, target});
        };
        for (int i = 0; i < 6; ++i) {
            const QRect cell(60 + (i % 2) * 120, 60 + (i / 2) * 90, 120, 90);
            painter.fillRect(cell, colors[i]);
            painter.setPen(Qt::white);
            painter.drawText(cell, Qt::AlignCenter, QString::number(i + 1));
            candidate(cell, QString::number(i + 1));
        }
        painter.end();
        candidate({60, 150, 240, 90}, "34");
        candidate({60, 150, 240, 180}, "3456");
        candidate({60, 60, 240, 270}, "整个表格");
        auto document = fromImage(image, "demo", "组件布局回归示例");
        document.candidates = candidates;
        return document;
    }
    QPoint canvasPoint(LayoutCanvas *canvas, QPointF imagePoint) {
        return {qRound(imagePoint.x() * canvas->zoom()), qRound(imagePoint.y() * canvas->zoom())};
    }
    void wheel(LayoutCanvas *canvas, QPointF imagePoint, int delta = 120) {
        const QPoint position = canvasPoint(canvas, imagePoint);
        QWheelEvent event(QPointF(position), QPointF(canvas->mapToGlobal(position)), QPoint(),
                          QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
    }
    void drag(LayoutCanvas *canvas, QPointF start, QPointF end) {
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, start));
        QTest::mouseMove(canvas, canvasPoint(canvas, end));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, end));
    }
  private slots:
    void initTestCase() {
        // Keep the save-failure test's file chooser inspectable through Qt on every platform.
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    void explosionSelectionTransformAndManualRegion() {
        const auto document = gridDocument();
        ExplosionDialog dialog(document.image, createLayout(document.image.size(), document.candidates));
        dialog.show();
        dialog.resize(1220, 820);
        QTest::qWait(80);
        auto canvas = dialog.canvas();
        QVERIFY(canvas);
        QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowStaysOnTopHint));

        const QPointF three(120, 195);
        QTest::mouseMove(canvas, canvasPoint(canvas, three));
        for (int i = 0; i < 3; ++i)
            wheel(canvas, three);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, three));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 240, 270));

        auto clear = dialog.findChild<QPushButton *>("clearLayoutSelection");
        QVERIFY(clear);
        clear->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {120, 105}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        const QString firstId = canvas->selected();

        auto before = canvas->selectionBounds();
        const QPointF east(before.right(), before.center().y());
        drag(canvas, east, east + QPointF(40, 0));
        auto afterEdge = canvas->selectionBounds();
        QVERIFY(afterEdge.width() > before.width() + 35);
        QVERIFY(qAbs(afterEdge.height() - before.height()) < 0.01);
        QCOMPARE(canvas->selected(), firstId);

        const QPointF corner = afterEdge.bottomRight();
        drag(canvas, corner, corner + QPointF(48, 27));
        const auto afterCorner = canvas->selectionBounds();
        QVERIFY(afterCorner.width() > afterEdge.width());
        QVERIFY(qAbs(afterCorner.width() / afterCorner.height() - afterEdge.width() / afterEdge.height()) <
                0.001);
        wheel(canvas, afterCorner.center());
        const auto afterWheel = canvas->selectionBounds();
        QVERIFY(afterWheel.width() > afterCorner.width());
        QVERIFY(qAbs(afterWheel.width() / afterWheel.height() - afterCorner.width() / afterCorner.height()) <
                0.001);

        auto x = dialog.findChild<QDoubleSpinBox *>("layoutX");
        QVERIFY(x && x->isEnabled());
        x->setValue(440.25);
        QVERIFY(QMetaObject::invokeMethod(x, "editingFinished", Qt::DirectConnection));
        QVERIFY(qAbs(canvas->selectionBounds().x() - 440.25) < 0.01);
        const auto moved = renderLayout(document.image, canvas->state());
        QCOMPARE(moved.pixelColor(120, 105).alpha(), 0);
        QVERIFY(moved.pixelColor(460, 105).alpha() > 0);

        const auto beforeManual = canvas->state();
        auto manual = dialog.findChild<QPushButton *>("manualRegion");
        QVERIFY(manual);
        manual->click();
        QVERIFY(canvas->drawingMode());
        drag(canvas, {200, 165}, {270, 215});
        QCOMPARE(canvas->state().groups.size(), beforeManual.groups.size() + 1);
        QVERIFY(!canvas->selected().isEmpty());
        QVERIFY(!canvas->drawingMode());
        const auto afterManual = canvas->state();
        canvas->undo();
        QVERIFY(canvas->state() == beforeManual);
        QVERIFY(canvas->canRedo());
        canvas->redo();
        QVERIFY(canvas->state() == afterManual);

        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          canvasPoint(canvas, layoutBounds(canvas->state(), firstId).center()));
        QCOMPARE(canvas->selected(), firstId);
        QTest::qWait(30);
        artifact(dialog, "explosion-editor.png");
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty())
            QVERIFY(renderLayout(document.image, canvas->state())
                        .save(QDir(folder).filePath("explosion-result.png")));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(canvas->selected().isEmpty());
        manual->click();
        QVERIFY(canvas->drawingMode());
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->drawingMode());
        QVERIFY(!manual->isChecked());
        QVERIFY(dialog.isVisible());
        dialog.close();
    }
    void editorAppliesLayoutAndRestoresHistory() {
        auto document = gridDocument();
        Note note;
        note.point = {120, 105};
        note.comment = "将第一格移到右侧，保留原图批注坐标。";
        document.notes.append(note);
        const auto originalNotes = document.notes;
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QVERIFY(!editor.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        QVERIFY(explode);
        QTRY_VERIFY_WITH_TIMEOUT(explode->isEnabled(), 5000);
        bool applied = false;
        QTimer::singleShot(80, [&] {
            auto dialog = qobject_cast<ExplosionDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            // Always close a failed modal interaction, so a regression cannot hang the suite.
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            dialog->resize(1220, 820);
            QTest::qWait(40);
            auto canvas = dialog->canvas();
            // A deliberate manual region makes this integration check independent of detector granularity.
            auto manual = dialog->findChild<QPushButton *>("manualRegion");
            QVERIFY(manual);
            manual->click();
            drag(canvas, {60, 60}, {180, 150});
            QVERIFY(!canvas->selected().isEmpty());
            auto destination = canvas->selectionBounds();
            destination.moveLeft(440);
            canvas->transformSelection(destination);
            auto apply = dialog->findChild<QPushButton *>("applyExplosion");
            QVERIFY(apply);
            applied = true;
            apply->click();
        });
        explode->click();
        QVERIFY(applied);
        QVERIFY(editor.document().layout.has_value());
        QVERIFY(editor.document().notes == originalNotes);
        QVERIFY(editor.canvas()->layoutPreview());
        QCOMPARE(exportDocument(editor.document())["schemaVersion"].toString(), QString("2.0.0"));
        const auto appliedLayout = *editor.document().layout;

        QPushButton *undo = nullptr, *redo = nullptr;
        for (auto button : editor.findChildren<QPushButton *>()) {
            if (button->toolTip() == "撤销")
                undo = button;
            else if (button->toolTip() == "重做")
                redo = button;
        }
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(!editor.document().layout.has_value());
        QVERIFY(editor.document().notes == originalNotes);
        QVERIFY(!editor.canvas()->layoutPreview());
        QVERIFY(redo->isEnabled());
        redo->click();
        QVERIFY(editor.document().layout.has_value());
        QVERIFY(*editor.document().layout == appliedLayout);
        QVERIFY(editor.document().notes == originalNotes);
        QVERIFY(editor.canvas()->layoutPreview());
        QTest::qWait(40);
        artifact(editor, "layout-preview.png");
        auto layoutView = editor.findChild<QPushButton *>("layoutView");
        QVERIFY(layoutView && layoutView->isVisible());
        layoutView->click();
        QVERIFY(!editor.canvas()->layoutPreview());
        QVERIFY(editor.document().notes == originalNotes);
        editor.hide();
    }
    void invalidExportKeepsUnsavedWork() {
        QTemporaryDir dir;
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        document.layout->pieces[0].destination.moveLeft(900);
        document.dirty = true;
        Editor editor;
        editor.setDocument(document);
        bool rejectedExport = false;
        QTimer::singleShot(80, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            auto json = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(json);
            bool copyDisabled = false, saveDisabled = false;
            for (auto button : dialog->findChildren<QPushButton *>()) {
                if (button->text() == "复制 JSON")
                    copyDisabled = !button->isEnabled();
                if (button->text() == "保存 JSON 与图片")
                    saveDisabled = !button->isEnabled();
            }
            rejectedExport = json->toPlainText().isEmpty() && copyDisabled && saveDisabled;
            dialog->reject();
        });
        editor.exportJson();
        QVERIFY(rejectedExport);
        QVERIFY(editor.document().dirty);
        QVERIFY(editor.document().layout == document.layout);

        const QString path = dir.filePath("invalid-project.json");
        bool selectedFile = false, errorShown = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &editor, [&] {
            auto active = QApplication::activeModalWidget();
            if (auto dialog = qobject_cast<QFileDialog *>(active); dialog && !selectedFile) {
                selectedFile = true;
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            } else if (auto error = qobject_cast<QMessageBox *>(active)) {
                errorShown = true;
                error->accept();
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        driver.start(20);
        const bool saved = editor.saveProject();
        driver.stop();
        QVERIFY(selectedFile && errorShown);
        QVERIFY(!saved);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(editor.document().dirty);
        QVERIFY(editor.document().layout == document.layout);
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
