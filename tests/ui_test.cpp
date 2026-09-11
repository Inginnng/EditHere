#include "editor.h"
#include "explosion.h"
#include "overlay.h"
#include "ui.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>
#include <QVariantAnimation>
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
    QPushButton *toolButton(Editor &editor, const QString &tooltip) {
        for (auto button : editor.findChildren<QPushButton *>())
            if (button->toolTip() == tooltip)
                return button;
        return nullptr;
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
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto notesPanel = editor.findChild<QWidget *>("notesPanel");
        QVERIFY(imageScroll && notesPanel && notesPanel->isVisible());
        QVERIFY(editor.document().notes.isEmpty());
        const QRect originalGeometry = editor.geometry();
        const QSize originalViewport = imageScroll->viewport()->size();
        const QRect originalNotesGeometry = notesPanel->geometry();
        artifact(editor, "empty-notes-layout.png");
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
        QTest::qWait(30);
        QCOMPARE(editor.geometry(), originalGeometry);
        QCOMPARE(imageScroll->viewport()->size(), originalViewport);
        QCOMPARE(notesPanel->geometry(), originalNotesGeometry);
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
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        QVERIFY(imageScroll && explode && explode->isEnabled());
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        explode->click();
        auto canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive() && explode->isChecked());
        QVERIFY(!editor.document().dirty);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QCOMPARE(canvas->window(), &editor);
        QVERIFY(!editor.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QTest::qWait(30);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);

        auto animation = editor.findChild<QVariantAnimation *>("explosionWaveAnimation");
        auto wave = editor.findChild<QWidget *>("explosionWave");
        QVERIFY(animation && wave && wave->isVisible());
        QVERIFY(wave->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->pause();
        animation->setCurrentTime(400);
        artifact(editor, "inline-wave.png");
        animation->resume();

        const QPointF three(120, 195);
        QTest::mouseMove(canvas, canvasPoint(canvas, three));
        for (int i = 0; i < 3; ++i)
            wheel(canvas, three);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, three));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 240, 270));

        auto clear = editor.findChild<QPushButton *>("clearLayoutSelection");
        QVERIFY(clear);
        clear->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
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

        auto x = editor.findChild<QDoubleSpinBox *>("layoutX");
        QVERIFY(x && x->isEnabled());
        x->setValue(440.25);
        QVERIFY(QMetaObject::invokeMethod(x, "editingFinished", Qt::DirectConnection));
        QVERIFY(qAbs(canvas->selectionBounds().x() - 440.25) < 0.01);
        const auto moved = renderLayout(document.image, canvas->state());
        QCOMPARE(moved.pixelColor(120, 105).alpha(), 0);
        QVERIFY(moved.pixelColor(460, 105).alpha() > 0);

        const auto beforeManual = canvas->state();
        auto manual = editor.findChild<QPushButton *>("manualRegion");
        QVERIFY(manual);
        manual->click();
        QVERIFY(canvas->drawingMode());
        drag(canvas, {200, 165}, {270, 215});
        QCOMPARE(canvas->state().groups.size(), beforeManual.groups.size() + 1);
        QVERIFY(!canvas->selected().isEmpty());
        QVERIFY(!canvas->drawingMode());
        const auto afterManual = canvas->state();
        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(canvas->state() == beforeManual);
        QVERIFY(redo->isEnabled());
        redo->click();
        QVERIFY(canvas->state() == afterManual);
        QVERIFY(editor.document().layout == afterManual);

        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          canvasPoint(canvas, layoutBounds(canvas->state(), firstId).center()));
        QCOMPARE(canvas->selected(), firstId);
        animation->stop();
        wave->hide();
        artifact(editor, "inline-explosion.png");
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty())
            QVERIFY(renderLayout(document.image, canvas->state())
                        .save(QDir(folder).filePath("inline-result.png")));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(canvas->selected().isEmpty());
        manual->click();
        QVERIFY(canvas->drawingMode());
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->drawingMode());
        QVERIFY(!manual->isChecked());
        QVERIFY(editor.isVisible());
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        editor.hide();
    }
    void editorRetainsInlineLayoutUntilScreenshotCloses() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.point = {120, 105};
        note.comment = "将第一格移到右侧，批注跟随组件。";
        document.notes.append(note);
        const auto originalNotes = document.notes;
        const auto initialLayout = *document.layout;
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        QVERIFY(imageScroll && explode && explode->isEnabled());
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        explode->click();
        QPointer<LayoutCanvas> canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        auto destination = canvas->selectionBounds();
        destination.moveLeft(440);
        canvas->transformSelection(destination);
        QVERIFY(editor.document().layout.has_value());
        QCOMPARE(editor.document().notes[0].point, QPoint(500, 105));
        QCOMPARE(editor.document().notes[0].comment, note.comment);
        const auto appliedNotes = editor.document().notes;
        const auto appliedLayout = *editor.document().layout;
        const auto feedback = exportFeedback(editor.document());
        QCOMPARE(feedback.keys(), QStringList({"annotationSpace", "annotations", "changes"}));
        QCOMPARE(feedback["changes"].toArray().size(), 1);
        QCOMPARE(feedback["changes"].toArray().first().toObject()["from"].toObject(),
                 rectJson(QRect(60, 60, 120, 90)));
        QCOMPARE(feedback["changes"].toArray().first().toObject()["to"].toObject(),
                 rectJson(QRect(440, 60, 120, 90)));

        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(editor.document().layout == initialLayout);
        QVERIFY(canvas->state() == initialLayout);
        QVERIFY(editor.document().notes == originalNotes);
        QVERIFY(editor.explosionActive());
        QVERIFY(redo->isEnabled());
        redo->click();
        QVERIFY(editor.document().layout == appliedLayout);
        QVERIFY(canvas->state() == appliedLayout);
        QVERIFY(editor.document().notes == appliedNotes);

        QStringList pieceIds;
        for (const auto &piece : canvas->state().pieces)
            pieceIds.append(piece.id);
        explode->click();
        QVERIFY(!editor.explosionActive() && !explode->isChecked());
        QVERIFY(editor.canvas()->layoutPreview());
        QVERIFY(!canvas->isVisible());
        QVERIFY(editor.document().layout == appliedLayout);
        QVERIFY(editor.document().notes == appliedNotes);
        QTest::qWait(30);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        artifact(editor, "inline-result-preview.png");

        explode->click();
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QCOMPARE(editor.layoutCanvas(), canvas.data());
        QVERIFY(canvas->state() == appliedLayout);
        QStringList resumedIds;
        for (const auto &piece : canvas->state().pieces)
            resumedIds.append(piece.id);
        QCOMPARE(resumedIds, pieceIds);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);

        bool discarded = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &editor, [&] {
            if (auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto discard = prompt->button(QMessageBox::Discard)) {
                    discarded = true;
                    discard->click();
                }
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        dismiss.start(20);
        editor.close();
        dismiss.stop();
        QVERIFY(discarded);
        QVERIFY(!editor.isVisible());
        QVERIFY(!editor.hasDocument());
        QVERIFY(!editor.document().layout.has_value());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(canvas.isNull());
        QVERIFY(!editor.explosionActive());
        editor.setDocument(gridDocument());
        QVERIFY(editor.hasDocument());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(!editor.document().layout.has_value());
        editor.hide();
    }
    void explosionEditsAndAnnotationsShareResult() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        auto component = editor.findChild<QPushButton *>("componentTool");
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(explode && component && imageScroll);
        explode->click();
        auto layout = editor.layoutCanvas();
        QVERIFY(layout && editor.explosionActive() && explode->isChecked());
        layout->zoomRequested(1.0);
        QCOMPARE(layout->zoom(), 1.0);
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 110}));
        QCOMPARE(layout->selectionBounds(), QRectF(60, 60, 120, 90));
        const auto componentId = layout->selected();
        layout->transformSelection(QRectF(440, 60, 120, 90));
        const auto firstLayout = *editor.document().layout;
        const auto originalPieceCount = firstLayout.pieces.size();
        QCOMPARE(renderLayout(document.image, firstLayout).pixelColor(120, 105).alpha(), 0);

        auto completeComment = [&](const QString &comment) {
            QTimer::singleShot(2500, &editor, [] {
                if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                    dialog->reject();
            });
            QTimer::singleShot(50, &editor, [&, comment] {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                auto text = dialog->findChild<QPlainTextEdit *>("commentInput");
                QVERIFY(text);
                text->setPlainText(comment);
                for (auto button : dialog->findChildren<QPushButton *>())
                    if (button->text() == "保存") {
                        button->click();
                        return;
                    }
                QFAIL("Comment save button missing");
            });
        };

        // Point and rectangle tools annotate the edited image without disabling explosion.
        QTest::keyClick(layout, Qt::Key_P);
        auto result = editor.canvas();
        QCOMPARE(result->mode(), Canvas::Point);
        QVERIFY(result->isVisible() && result->layoutPreview());
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QVERIFY(component->isVisible() && !component->isChecked());
        QCOMPARE(result->zoom(), 1.0);
        completeComment("请加大标题字号。");
        QTest::mouseClick(result, Qt::LeftButton, Qt::NoModifier, QPoint(500, 105));
        QCOMPARE(editor.document().notes.size(), 1);
        QCOMPARE(editor.document().notes[0].point, QPoint(500, 105));
        QVERIFY(editor.document().layout == firstLayout);

        auto resumeShortcuts = [&] {
            QVERIFY(QApplication::activeModalWidget() == nullptr);
            // Qt's offscreen plugin does not reactivate the parent when hiding a dialog.
            // Native platforms must restore activation themselves, as in normal use.
            if (QGuiApplication::platformName() == "offscreen") {
                editor.activateWindow();
                result->setFocus();
            }
            QTRY_VERIFY_WITH_TIMEOUT(editor.isActiveWindow(), 1000);
            QTRY_VERIFY_WITH_TIMEOUT(result->hasFocus(), 1000);
        };
        resumeShortcuts();
        QTest::keyClick(result, Qt::Key_R);
        QCOMPARE(result->mode(), Canvas::Rectangle);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QTest::mousePress(result, Qt::LeftButton, Qt::NoModifier, QPoint(450, 70));
        QTest::mouseMove(result, QPoint(540, 125));
        completeComment("这个区域的内容需要对齐。");
        QTest::mouseRelease(result, Qt::LeftButton, Qt::NoModifier, QPoint(540, 125));
        QCOMPARE(editor.document().notes.size(), 2);
        QVERIFY(!editor.document().notes[1].isPoint);
        QCOMPARE(editor.document().notes[1].rect, QRect(450, 70, 90, 55));
        const auto firstNotes = editor.document().notes;
        resumeShortcuts();
        QTest::keyClick(result, Qt::Key_B);
        QCOMPARE(result->mode(), Canvas::Smart);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        QTest::qWait(30);
        auto notesPanel = editor.findChild<QWidget *>("notesPanel");
        QVERIFY(notesPanel && notesPanel->isVisible());
        int visibleNoteCards = 0;
        for (auto card : notesPanel->findChildren<QWidget *>("noteCard"))
            if (card->isVisible())
                ++visibleNoteCards;
        QCOMPARE(visibleNoteCards, editor.document().notes.size());
        artifact(editor, "result-annotations.png");

        // A selected annotation in the hidden result canvas must not be deleted in component mode.
        QCOMPARE(result->selected(), firstNotes.last().id);
        component->click();
        QCOMPARE(editor.layoutCanvas(), layout);
        QVERIFY(layout->isVisible() && component->isChecked());
        QTest::keyClick(layout, Qt::Key_Delete);
        QVERIFY(editor.document().notes == firstNotes);
        QVERIFY(editor.document().layout == firstLayout);

        // Continue component adjustment and carry both annotations with the same pixels.
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {450, 140}));
        QCOMPARE(layout->selected(), componentId);
        layout->transformSelection(QRectF(600, 240, 120, 90));
        const auto secondLayout = *editor.document().layout;
        const auto secondNotes = editor.document().notes;
        QCOMPARE(secondNotes[0].point, QPoint(660, 285));
        QCOMPARE(secondNotes[1].rect, QRect(610, 250, 90, 55));
        QCOMPARE(secondLayout.pieces.size(), originalPieceCount);
        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(editor.document().layout == firstLayout);
        QVERIFY(editor.document().notes == firstNotes);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        redo->click();
        QVERIFY(editor.document().layout == secondLayout);
        QVERIFY(editor.document().notes == secondNotes);
        QVERIFY(editor.explosionActive() && explode->isChecked());

        // Annotating a selected component and editing a badge never alter the cuts.
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {610, 320}));
        QCOMPARE(layout->selected(), componentId);
        auto annotate = editor.findChild<QPushButton *>("annotateComponent");
        QVERIFY(annotate && annotate->isEnabled());
        QSignalSpy geometryChanged(layout, &LayoutCanvas::changed);
        completeComment("保持这个组件的新位置。");
        annotate->click();
        QCOMPARE(editor.document().notes.size(), 3);
        QVERIFY(!editor.document().notes[2].isPoint);
        QCOMPARE(editor.document().notes[2].rect, QRect(600, 240, 120, 90));
        QVERIFY(editor.explosionActive() && explode->isChecked() && layout->isVisible());
        QVERIFY(layout->state() == secondLayout);
        QCOMPARE(layout->selected(), componentId);
        const auto beforeTextEdit = editor.document().notes;
        completeComment("标题字号改成 24 像素。");
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {660, 285}));
        QCOMPARE(editor.document().notes.size(), 3);
        QCOMPARE(editor.document().notes[0].comment, QString("标题字号改成 24 像素。"));
        QCOMPARE(editor.document().notes[0].point, QPoint(660, 285));
        QCOMPARE(geometryChanged.count(), 0);
        QVERIFY(layout->state() == secondLayout);
        const auto afterTextEdit = editor.document().notes;
        undo->click();
        QVERIFY(editor.document().notes == beforeTextEdit);
        QVERIFY(editor.document().layout == secondLayout);
        redo->click();
        QVERIFY(editor.document().notes == afterTextEdit);
        QVERIFY(editor.document().layout == secondLayout);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {610, 320}));
        QCOMPARE(layout->selected(), componentId);
        QTest::qWait(30);
        artifact(editor, "explosion-with-annotations.png");
        const auto feedback = exportFeedback(editor.document());
        QCOMPARE(feedback["annotationSpace"].toString(), QString("result"));
        QCOMPARE(feedback["annotations"].toArray().size(), 3);
        QCOMPARE(feedback["changes"].toArray().size(), 1);
        QCOMPARE(feedback["changes"].toArray()[0].toObject()["from"].toObject(),
                 rectJson(QRect(60, 60, 120, 90)));
        QCOMPARE(feedback["changes"].toArray()[0].toObject()["to"].toObject(),
                 rectJson(QRect(600, 240, 120, 90)));
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        editor.hide();
    }
    void compactFeedbackExportsSavesAndReopens() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note point;
        point.point = {120, 105};
        point.comment = "把第一格移到右边。";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {60, 150, 240, 90};
        rectangle.comment = "这一行的文字保持左对齐。";
        document.notes = {point, rectangle};
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(60);
        editor.explode();
        QPointer<LayoutCanvas> canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive());
        QVERIFY(!editor.document().dirty);

        // Editing only the temporary partition does not create user feedback.
        auto manual = editor.findChild<QPushButton *>("manualRegion");
        QVERIFY(manual);
        const int groupCount = canvas->state().groups.size();
        manual->click();
        drag(canvas, {500, 400}, {600, 450});
        QCOMPARE(canvas->state().groups.size(), groupCount + 1);
        QVERIFY(!editor.document().dirty);
        QVERIFY(exportFeedback(editor.document())["changes"].toArray().isEmpty());
        canvas->clearSelection();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        auto destination = canvas->selectionBounds();
        destination.moveLeft(440);
        canvas->transformSelection(destination);
        QVERIFY(editor.document().dirty);
        const auto expectedFeedback = exportFeedback(editor.document());
        const auto expectedEmbedded = exportFeedback(editor.document(), true);
        const auto expectedImage = renderLayout(editor.document().image, *editor.document().layout);
        QCOMPARE(expectedFeedback["annotations"].toArray().size(), 2);
        QCOMPARE(expectedFeedback["changes"].toArray().size(), 1);

        QPushButton *exportButton = nullptr;
        for (auto button : editor.findChildren<QPushButton *>())
            if (button->text() == "导出 JSON")
                exportButton = button;
        QVERIFY(exportButton);
        bool exportChecked = false;
        QTimer::singleShot(60, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            auto text = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(text && text->isVisible());
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson(text->toPlainText().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QVERIFY(parsed.isObject());
            auto embed = dialog->findChild<QCheckBox *>("embedOriginal");
            QVERIFY(embed && embed->isChecked());
            auto preview = parsed.object();
            QCOMPARE(preview.keys(), QStringList({"annotationSpace", "annotations", "changes", "image"}));
            QVERIFY(preview["image"].toString().startsWith("data:image/png;base64,"));
            QVERIFY(preview["image"] != expectedEmbedded["image"]);
            preview.remove("image");
            QCOMPARE(preview, expectedFeedback);
            QPushButton *copy = nullptr;
            for (auto button : dialog->findChildren<QPushButton *>())
                if (button->text() == "复制 JSON")
                    copy = button;
            QVERIFY(copy && copy->isEnabled());
            copy->click();
            const auto copied = QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(copied.object(), expectedEmbedded);
            const auto encoded =
                copied.object()["image"].toString().mid(QString("data:image/png;base64,").size());
            QCOMPARE(QByteArray::fromBase64(encoded.toLatin1()), document.png);
            QApplication::clipboard()->clear();
            text->setFocus();
            text->selectAll();
            QTest::keyClick(text, Qt::Key_C, Qt::ControlModifier);
            const auto keyboardCopy =
                QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(keyboardCopy.object(), expectedEmbedded);
            const auto keyboardImage =
                keyboardCopy.object()["image"].toString().mid(QString("data:image/png;base64,").size());
            QCOMPARE(QByteArray::fromBase64(keyboardImage.toLatin1()), document.png);
            text->moveCursor(QTextCursor::Start);
            artifact(*dialog, "embedded-export.png");
            embed->click();
            QVERIFY(!embed->isChecked());
            const auto compact = QJsonDocument::fromJson(text->toPlainText().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(compact.object(), expectedFeedback);
            QVERIFY(!compact.object().contains("image"));
            copy->click();
            QCOMPARE(QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object(),
                     expectedFeedback);
            artifact(*dialog, "compact-export.png");
            exportChecked = true;
            dialog->reject();
        });
        exportButton->click();
        QVERIFY(exportChecked);
        QVERIFY(editor.document().dirty);

        const QString path = dir.filePath("review.json");
        bool selectedFile = false, saveError = false;
        QTimer chooseFile;
        connect(&chooseFile, &QTimer::timeout, &editor, [&] {
            auto active = QApplication::activeModalWidget();
            if (auto dialog = qobject_cast<QFileDialog *>(active); dialog && !selectedFile) {
                selectedFile = true;
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            } else if (auto error = qobject_cast<QMessageBox *>(active)) {
                saveError = true;
                error->accept();
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        chooseFile.start(20);
        const bool saved = editor.saveProject();
        chooseFile.stop();
        QVERIFY(selectedFile && !saveError && saved);
        QVERIFY(!editor.document().dirty);
        QVERIFY(!QFileInfo::exists(dir.filePath("review.png")));
        QFile savedJson(path);
        QVERIFY(savedJson.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(savedJson.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(parsed.object().keys(), QStringList({"annotationSpace", "annotations", "changes", "image"}));
        QCOMPARE(parsed.object(), expectedEmbedded);
        const auto restored = loadDocument(path);
        QVERIFY(restored.layout.has_value());
        QCOMPARE(restored.png, document.png);
        QCOMPARE(renderLayout(restored.image, *restored.layout), expectedImage);
        QCOMPARE(restored.notes.size(), 2);
        QVERIFY(restored.notes[0].isPoint);
        QCOMPARE(restored.notes[0].point, QPoint(500, 105));
        QCOMPARE(restored.notes[0].comment, point.comment);
        QVERIFY(!restored.notes[1].isPoint);
        QCOMPARE(restored.notes[1].rect, rectangle.rect);
        QCOMPARE(restored.notes[1].comment, rectangle.comment);
        QCOMPARE(exportFeedback(restored), expectedFeedback);

        bool unexpectedPrompt = false;
        QTimer rejectUnexpectedPrompt;
        connect(&rejectUnexpectedPrompt, &QTimer::timeout, &editor, [&] {
            if (auto active = QApplication::activeModalWidget()) {
                unexpectedPrompt = true;
                active->close();
            }
        });
        rejectUnexpectedPrompt.start(20);
        editor.close();
        rejectUnexpectedPrompt.stop();
        QVERIFY(!unexpectedPrompt);
        QVERIFY(!editor.hasDocument());
        QVERIFY(!editor.document().layout.has_value());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(canvas.isNull());
        QVERIFY(!editor.isVisible());
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
        QVERIFY(!QFileInfo::exists(dir.filePath("invalid-project.png")));
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
        artifact(overlay, "crop-overlay.png");
        QCOMPARE(accepted.count(), 0);
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().first().toRect(), QRect(60, 140, 540, 380));
        QVERIFY(overlay.findChildren<QPushButton *>().isEmpty());
        QTest::keyClick(&overlay, Qt::Key_Return);
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QCOMPARE(accepted.count(), 1);
        overlay.hide();
    }
    void detectedBlockClickImmediatelyStartsAnnotation() {
        QImage image(160, 120, QImage::Format_ARGB32);
        image.fill(Qt::white);
        ScreenFrame frame{"click-test", {0, 0, 160, 120}, {}, image, false};
        Overlay overlay(frame);
        overlay.show();
        QSignalSpy accepted(&overlay, &Overlay::accepted);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        QTest::mouseMove(&overlay, {80, 60});
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QCOMPARE(accepted.count(), 0);
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().first().toRect(), QRect(0, 0, 160, 120));
        QVERIFY(overlay.findChildren<QPushButton *>().isEmpty());
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QCOMPARE(accepted.count(), 1);
        overlay.hide();

        Overlay cancelledOverlay(frame);
        cancelledOverlay.show();
        QSignalSpy cancelled(&cancelledOverlay, &Overlay::cancelled);
        QSignalSpy notAccepted(&cancelledOverlay, &Overlay::accepted);
        QTest::keyClick(&cancelledOverlay, Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(notAccepted.count(), 0);
        cancelledOverlay.hide();
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
