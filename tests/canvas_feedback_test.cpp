#include "canvas.h"
#include "explosion.h"
#include "ui.h"
#include <QApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <stdexcept>
using namespace h2d;
class CanvasFeedbackTests : public QObject {
    Q_OBJECT
    static Document document() {
        QImage image(640, 480, QImage::Format_ARGB32);
        image.fill(QColor("#edf1f5"));
        auto doc = fromImage(image, "test", "画布交互");
        auto target = manualTarget();
        target["method"] = "test-region";
        target["label"] = "内容块";
        doc.candidates = {{{60, 60, 120, 80}, target}, {{30, 30, 260, 180}, target},
                          {{0, 0, 640, 480}, target}};
        return doc;
    }
    static void wheel(QWidget &widget, QPointF point, int delta = 120) {
        QWheelEvent event(point, widget.mapToGlobal(point.toPoint()), QPoint(), QPoint(0, delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&widget, &event);
    }
    static void move(QWidget &widget, QPointF point) {
        QMouseEvent event(QEvent::MouseMove, point, widget.mapToGlobal(point.toPoint()),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&widget, &event);
    }
    static QString firstGroup(const LayoutState &state) {
        for (const auto &group : state.groups)
            if (group.originalBounds == QRectF(60, 60, 120, 80))
                return group.id;
        return {};
    }
  private slots:
    void invalidLayoutRemainsInspectableAndExportStillRejectsIt() {
        auto doc = document();
        doc.layout = createLayout(doc.image.size(), doc.candidates);
        doc.layout->pieces[0].destination.moveLeft(900);
        doc.dirty = true;
        const auto damaged = *doc.layout;
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.refresh();
        canvas.show();
        QVERIFY(!canvas.grab().isNull());
        QCOMPARE(*doc.layout, damaged);
        QVERIFY(doc.dirty);
        LayoutCanvas layout(doc.image, damaged);
        layout.setState(damaged);
        layout.show();
        QVERIFY(!layout.grab().isNull());
        QCOMPARE(layout.state(), damaged);
        QVERIFY_EXCEPTION_THROWN(serializeFeedback(doc, true), std::runtime_error);
        QCOMPARE(*doc.layout, damaged);
    }
    void blankWheelZoomsAndConcreteBlockKeepsAllContainingRanges() {
        auto doc = document();
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.show();
        QSignalSpy zoom(&canvas, &Canvas::zoomRequested);
        QSignalSpy hints(&canvas, &Canvas::hintChanged);
        wheel(canvas, {520, 390});
        QCOMPARE(zoom.size(), 1);
        QCOMPARE(zoom.first().first().toDouble(), 1.12);
        wheel(canvas, {90, 90});
        wheel(canvas, {90, 90});
        QCOMPARE(zoom.size(), 1);
        QVERIFY(hints.last().first().toString().contains("3 / 3"));
        // A broad background strip must behave like empty image space as well.
        auto background = manualTarget();
        background["method"] = "color-region";
        doc.candidates.append({{0, 80, 640, 400}, background});
        canvas.refresh();
        wheel(canvas, {520, 390}, -120);
        QCOMPARE(zoom.size(), 2);
        QVERIFY(zoom.last().first().toDouble() < 1);
        // Large, explicitly detected tables are still selectable content, not background.
        auto table = manualTarget();
        table["method"] = "table-grid";
        table["label"] = "表格";
        doc.candidates = {{{0, 40, 640, 410}, table}, {{0, 0, 640, 480}, background}};
        canvas.refresh();
        wheel(canvas, {520, 390});
        QCOMPARE(zoom.size(), 2);
    }
    void rectangleCreatesCachedRegionBeforeAnnotation() {
        auto doc = document();
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setMode(Canvas::Rectangle);
        canvas.show();
        QSignalSpy regions(&canvas, &Canvas::regionRequested);
        QSignalSpy edits(&canvas, &Canvas::editRequested);
        connect(&canvas, &Canvas::regionRequested, &canvas, [&](QRect area) {
            doc.layout = createLayout(doc.image.size(), doc.candidates);
            addLayoutRegion(*doc.layout, area);
            canvas.refresh();
        });
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {330, 220});
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {490, 340});
        QCOMPARE(regions.size(), 1);
        QCOMPARE(edits.size(), 0);
        QVERIFY(doc.notes.isEmpty());
        QVERIFY(doc.layout.has_value());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {390, 275});
        QCOMPARE(edits.size(), 1);
        const auto note = qvariant_cast<Note>(edits.first().at(0));
        QVERIFY(!note.isPoint);
        QCOMPARE(note.rect, QRect(330, 220, 160, 120));
        QVERIFY(edits.first().at(1).toBool());
    }
    void badgesEditInAdjustModeAndHideWithoutDeletingNotes() {
        auto doc = document();
        Note global;
        global.isGlobal = true;
        global.comment = "整体风格";
        Note point;
        point.point = {400, 100};
        point.comment = "标题";
        doc.notes = {global, point};
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setMode(Canvas::Adjust);
        canvas.show();
        QSignalSpy edits(&canvas, &Canvas::editRequested);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point.point);
        QCOMPARE(edits.size(), 1);
        QVERIFY(!edits.first().at(1).toBool());
        move(canvas, point.point);
        const auto first = canvas.grab().toImage();
        QTest::qWait(120);
        QVERIFY(first != canvas.grab().toImage());
        canvas.setAnnotationsVisible(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point.point);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(doc.notes.size(), 2);
        canvas.setAnnotationsVisible(true);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {14, 14});
        QCOMPARE(edits.size(), 1); // The global note has no origin marker.
    }
    void arrowsUseActualChangesAndRemainEditableAcrossModes() {
        auto doc = document();
        doc.layout = createLayout(doc.image.size(), doc.candidates);
        const auto id = firstGroup(*doc.layout);
        QVERIFY(!id.isEmpty());
        const QRectF source(60, 60, 120, 80), destination(300, 240, 120, 80);
        transformLayoutGroup(*doc.layout, id, destination);
        const auto midpoint = (source.center() + destination.center()) / 2;
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.show();
        QSignalSpy moves(&canvas, &Canvas::movementAnnotationRequested);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves.first().at(0).toRectF(), source);
        QCOMPARE(moves.first().at(1).toRectF(), destination);
        move(canvas, midpoint);
        const auto first = canvas.grab().toImage();
        QTest::qWait(120);
        QVERIFY(first != canvas.grab().toImage());
        canvas.setAnnotationsVisible(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(moves.size(), 1);
        canvas.setAnnotationsVisible(true);
        transformLayoutGroup(*doc.layout, id, source);
        canvas.refresh();
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(moves.size(), 1); // Returning a component removes its change arrow.

        transformLayoutGroup(*doc.layout, id, destination);
        LayoutCanvas layout(doc.image, *doc.layout);
        layout.show();
        QSignalSpy layoutMoves(&layout, &LayoutCanvas::movementAnnotationRequested);
        QTest::mouseClick(&layout, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(layoutMoves.size(), 1);
        QCOMPARE(layoutMoves.first().at(0).toRectF(), source);
        QCOMPARE(layoutMoves.first().at(1).toRectF(), destination);
        layout.setAnnotationsVisible(false);
        QTest::mouseClick(&layout, Qt::LeftButton, Qt::NoModifier, midpoint.toPoint());
        QCOMPARE(layoutMoves.size(), 1);
    }
    void layoutBadgesRespectHiddenAndGlobalAnnotations() {
        auto doc = document();
        LayoutCanvas canvas(doc.image, createLayout(doc.image.size(), doc.candidates));
        canvas.setGuides(false);
        Note global;
        global.isGlobal = true;
        Note point;
        point.point = {420, 110};
        canvas.setAnnotations({global, point});
        canvas.show();
        QSignalSpy edits(&canvas, &LayoutCanvas::noteEditRequested);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point.point);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits.first().first().toString(), point.id);
        move(canvas, point.point);
        const auto first = canvas.grab().toImage();
        QTest::qWait(120);
        QVERIFY(first != canvas.grab().toImage());
        canvas.setAnnotationsVisible(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, point.point);
        QCOMPARE(edits.size(), 1);
        canvas.clearSelection();
        canvas.setAnnotationsVisible(true);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {14, 14});
        QCOMPARE(edits.size(), 1);
    }
    void layoutBlankWheelDoesNotResizeSelectionAndInspectorStaysCompact() {
        auto doc = document();
        LayoutCanvas canvas(doc.image, createLayout(doc.image.size(), doc.candidates));
        canvas.show();
        LayoutInspector inspector(&canvas);
        inspector.resize(336, inspector.sizeHint().height());
        inspector.show();
        QVERIFY(inspector.sizeHint().height() < 80);
        QVERIFY(!inspector.findChild<QPushButton *>("manualRegion"));
        QSignalSpy zoom(&canvas, &LayoutCanvas::zoomRequested);
        wheel(canvas, {520, 390});
        QCOMPARE(zoom.size(), 1);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, {90, 90});
        QVERIFY(!canvas.selected().isEmpty());
        QVERIFY(inspector.sizeHint().height() <= 210);
        const auto bounds = canvas.selectionBounds();
        wheel(canvas, {520, 390});
        QCOMPARE(zoom.size(), 2);
        QCOMPARE(canvas.selectionBounds(), bounds);
        wheel(canvas, {90, 90});
        QVERIFY(canvas.selectionBounds().width() > bounds.width());
    }
};
QTEST_MAIN(CanvasFeedbackTests)
#include "canvas_feedback_test.moc"
