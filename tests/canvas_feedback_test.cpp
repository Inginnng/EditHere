#include "canvas.h"
#include "explosion.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
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
    static QRectF movementSource(int row) {
        return {60, 100 + row * 150.0, 140, 70};
    }
    static QRectF movementDestination(int row) {
        return {470, 100 + row * 150.0, 140, 70};
    }
    static QPointF movementMidpoint(int row, double zoom = 1) {
        return (movementSource(row).center() + movementDestination(row).center()) * (zoom / 2);
    }
    static Note movementNote(int row) {
        Note note;
        note.isPoint = false;
        note.movementSource = movementSource(row);
        note.rect = movementDestination(row).toRect();
        note.comment = "将这个内容块移到右侧，保留左侧留白。";
        return note;
    }
    static Document numberedMovements(bool annotated = true) {
        QImage image(800, 600, QImage::Format_ARGB32);
        image.fill(QColor("#edf1f5"));
        QPainter painter(&image);
        painter.setPen(QColor("#203047"));
        painter.setFont(QFont("Microsoft YaHei UI", 19, QFont::DemiBold));
        painter.drawText(QRect(40, 20, 720, 44), Qt::AlignVCenter, "内容布局 · 移动位置");
        QVector<Candidate> candidates;
        for (int row = 0; row < 3; ++row) {
            const auto source = movementSource(row).toRect();
            painter.fillRect(source, QColor("#ffffff"));
            painter.fillRect(QRect(source.x(), source.y(), 5, source.height()), QColor("#007aff"));
            painter.setFont(QFont("Microsoft YaHei UI", 11, QFont::DemiBold));
            painter.drawText(source.adjusted(16, 8, -8, -30), Qt::AlignVCenter,
                             QString("内容模块 %1").arg(row + 1));
            painter.setFont(QFont("Microsoft YaHei UI", 9));
            painter.drawText(source.adjusted(16, 36, -8, -8), Qt::AlignVCenter, "保持信息层次");
            auto target = manualTarget();
            target["method"] = "test-region";
            target["label"] = QString("内容模块 %1").arg(row + 1);
            candidates.append({source, target});
        }
        painter.end();
        auto doc = fromImage(image, "test", "移动箭头编号");
        doc.candidates = candidates;
        doc.layout = createLayout(image.size(), candidates);
        for (int row = 0; row < 3; ++row)
            for (const auto &group : doc.layout->groups)
                if (group.originalBounds == movementSource(row)) {
                    transformLayoutGroup(*doc.layout, group.id, movementDestination(row));
                    break;
                }
        if (annotated) {
            Note global;
            global.isGlobal = true;
            global.comment = "整体增加留白。";
            Note point;
            point.point = {700, 85};
            point.comment = "标题保持清晰。";
            doc.notes = {global, point, movementNote(0)};
        }
        return doc;
    }
    static QImage rendered(QWidget &widget) {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        widget.render(&image);
        return image;
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
        QApplication::setFont(QFont("Microsoft YaHei UI", 9));
#endif
        applyTheme(ThemeMode::Light);
    }
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
    void manualLayoutRegionRequestsAnnotationAfterCaching() {
        const auto doc = document();
        const auto original = createLayout(doc.image.size(), doc.candidates);
        LayoutCanvas canvas(doc.image, original);
        canvas.setDrawing(true);
        canvas.show();
        QStringList events;
        LayoutState cached = original;
        QRect annotation;
        connect(&canvas, &LayoutCanvas::changed, &canvas, [&] {
            events.append("cached");
            cached = canvas.state();
        });
        connect(&canvas, &LayoutCanvas::annotationRequested, &canvas, [&](QRect area, QPoint) {
            events.append("annotation");
            annotation = area;
            QVERIFY(cached == canvas.state());
            QVERIFY(!canvas.drawingMode());
            QVERIFY(!canvas.selected().isEmpty());
            // Losing canvas focus to the inline editor must not discard the new region.
            canvas.cancelInteraction();
            QVERIFY(cached == canvas.state());
        });
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {330, 220});
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {490, 340});
        QCOMPARE(events, QStringList({"cached", "annotation"}));
        QCOMPARE(annotation, QRect(330, 220, 160, 120));
        QVERIFY(cached != original);
        const auto saved = cached;
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {390, 275});
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {390, 275});
        QCOMPARE(canvas.state(), saved);
        QCOMPARE(events.size(), 2); // Merely selecting the component creates no second draft.
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
    void numberedMovementBadgesOpenExistingNotesAndDrafts_data() {
        QTest::addColumn<bool>("exploded");
        QTest::newRow("canvas") << false;
        QTest::newRow("explosion") << true;
    }
    void numberedMovementBadgesOpenExistingNotesAndDrafts() {
        QFETCH(bool, exploded);
        auto doc = numberedMovements();
        const auto savedNotes = doc.notes;
        const auto savedLayout = *doc.layout;
        QCOMPARE(exportLayoutChanges(savedLayout).size(), 3);
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.setMode(Canvas::Adjust);
        LayoutCanvas layout(doc.image, savedLayout);
        layout.setAnnotations(doc.notes);
        layout.setGuides(false);
        QWidget &widget = exploded ? static_cast<QWidget &>(layout) : static_cast<QWidget &>(canvas);
        widget.show();
        QSignalSpy canvasEdits(&canvas, &Canvas::editRequested);
        QSignalSpy layoutEdits(&layout, &LayoutCanvas::noteEditRequested);
        QSignalSpy canvasMoves(&canvas, &Canvas::movementAnnotationRequested);
        QSignalSpy layoutMoves(&layout, &LayoutCanvas::movementAnnotationRequested);
        auto &edits = exploded ? layoutEdits : canvasEdits;
        auto &moves = exploded ? layoutMoves : canvasMoves;
        auto editedId = [&] {
            return exploded ? edits.last().at(0).toString() : qvariant_cast<Note>(edits.last().at(0)).id;
        };
        auto setVisible = [&](bool visible) {
            if (exploded) layout.setAnnotationsVisible(visible);
            else canvas.setAnnotationsVisible(visible);
        };
        int editCount = 0, moveCount = 0;
        for (const double zoom : {.5, 1.0, 1.75}) {
            if (exploded) layout.setZoom(zoom);
            else canvas.setZoom(zoom);
            // These points miss the line's 8 px tolerance but lie inside the full 17 px badge hit area.
            for (const int offset : {12, 16}) {
                QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                                  (movementMidpoint(0, zoom) + QPointF(0, offset)).toPoint());
                QCOMPARE(edits.size(), ++editCount);
                QCOMPARE(editedId(), doc.notes[2].id);
                if (!exploded) QVERIFY(!edits.last().at(1).toBool());
                QCOMPARE(moves.size(), moveCount);
                QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                                  (movementMidpoint(1, zoom) + QPointF(0, offset)).toPoint());
                // An unannotated arrow has no badge and no invisible circular hit target.
                QCOMPARE(moves.size(), moveCount);
                QCOMPARE(edits.size(), editCount);
            }
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                              movementMidpoint(1, zoom).toPoint());
            QCOMPARE(moves.size(), ++moveCount);
            QCOMPARE(moves.last().at(0).toRectF(), movementSource(1));
            QCOMPARE(moves.last().at(1).toRectF(), movementDestination(1));
            QCOMPARE(edits.size(), editCount);
            // The existing annotation must also open when its arrow is clicked away from the badge.
            const auto linePoint = (movementSource(0).center() * .75 +
                                    movementDestination(0).center() * .25) * zoom;
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, linePoint.toPoint());
            QCOMPARE(edits.size(), ++editCount);
            QCOMPARE(editedId(), doc.notes[2].id);
            QCOMPARE(moves.size(), moveCount);
            setVisible(false);
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                              (movementMidpoint(0, zoom) + QPointF(0, 12)).toPoint());
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                              (movementMidpoint(1, zoom) + QPointF(0, 12)).toPoint());
            QCOMPARE(edits.size(), editCount);
            QCOMPARE(moves.size(), moveCount);
            setVisible(true);
            QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                              (movementMidpoint(0, zoom) + QPointF(0, 12)).toPoint());
            QCOMPARE(edits.size(), ++editCount);
            QCOMPARE(editedId(), doc.notes[2].id);
        }
        QCOMPARE(doc.notes, savedNotes);
        QCOMPARE(*doc.layout, savedLayout);
        QCOMPARE(layout.state(), savedLayout);
        // Saving an annotation adds its number and makes the badge an existing-note edit target immediately.
        doc.notes.append(movementNote(1));
        canvas.refresh();
        layout.setAnnotations(doc.notes);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                          (movementMidpoint(1, 1.75) + QPointF(0, 16)).toPoint());
        QCOMPARE(edits.size(), ++editCount);
        QCOMPARE(editedId(), doc.notes.last().id);
        QCOMPARE(moves.size(), moveCount);
        QCOMPARE(doc.notes.size(), savedNotes.size() + 1);
    }
    void nestedMovementsKeepOneArrowPerSelectedComponent_data() {
        numberedMovementBadgesOpenExistingNotesAndDrafts_data();
    }
    void nestedMovementsKeepOneArrowPerSelectedComponent() {
        QFETCH(bool, exploded);
        QImage image(900, 700, QImage::Format_ARGB32);
        image.fill(QColor("#edf1f5"));
        const QRectF parentSource(80, 80, 240, 200), parentDestination(480, 120, 240, 200);
        const QRectF childSource(80, 80, 120, 100), childDestination(450, 480, 120, 100);
        QVector<Candidate> candidates;
        auto target = manualTarget();
        target["method"] = "test-region";
        candidates.append({parentSource.toRect(), target});
        QPainter painter(&image);
        painter.setFont(QFont("Microsoft YaHei UI", 24, QFont::DemiBold));
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 2; ++column) {
                const QRect cell(80 + column * 120, 80 + row * 100, 120, 100);
                candidates.append({cell, target});
                painter.fillRect(cell, QColor(row ? "#dce9f5" : "#ffffff"));
                painter.setPen(QColor("#a9bbce"));
                painter.drawRect(cell.adjusted(0, 0, -1, -1));
                painter.setPen(QColor("#203047"));
                painter.drawText(cell, Qt::AlignCenter, QString::number(row * 2 + column + 1));
            }
        painter.end();
        auto doc = fromImage(image, "test", "嵌套组件移动");
        doc.candidates = candidates;
        doc.layout = createLayout(image.size(), candidates);
        QString parentId, childId;
        for (const auto &group : doc.layout->groups) {
            if (group.originalBounds == parentSource) parentId = group.id;
            if (group.originalBounds == childSource) childId = group.id;
        }
        QVERIFY(!parentId.isEmpty());
        QVERIFY(!childId.isEmpty());
        transformLayoutGroup(*doc.layout, parentId, parentDestination);
        auto markers = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(markers.size(), 1);
        QCOMPARE(markers[0].source, parentSource);
        QCOMPARE(markers[0].destination, parentDestination);
        QCOMPARE(markers[0].number, 0);
        QCOMPARE(markers[0].noteIndex, -1);
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.setMode(Canvas::Adjust);
        LayoutCanvas layout(doc.image, *doc.layout);
        layout.setGuides(false);
        QWidget &widget = exploded ? static_cast<QWidget &>(layout) : static_cast<QWidget &>(canvas);
        widget.show();
        QSignalSpy canvasEdits(&canvas, &Canvas::editRequested);
        QSignalSpy layoutEdits(&layout, &LayoutCanvas::noteEditRequested);
        QSignalSpy canvasMoves(&canvas, &Canvas::movementAnnotationRequested);
        QSignalSpy layoutMoves(&layout, &LayoutCanvas::movementAnnotationRequested);
        auto &edits = exploded ? layoutEdits : canvasEdits;
        auto &moves = exploded ? layoutMoves : canvasMoves;
        auto editedId = [&] {
            return exploded ? edits.last().at(0).toString() : qvariant_cast<Note>(edits.last().at(0)).id;
        };
        const auto parentMidpoint = (parentSource.center() + parentDestination.center()) / 2;
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, parentMidpoint.toPoint());
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves.last().at(0).toRectF(), parentSource);
        QCOMPARE(moves.last().at(1).toRectF(), parentDestination);
        Note parentNote;
        parentNote.isPoint = false;
        parentNote.movementSource = parentSource;
        parentNote.rect = parentDestination.toRect();
        parentNote.comment = "整体向右移动，保持表格关系。";
        doc.notes.append(parentNote);
        const auto beforeChildMove = *doc.layout;
        transformLayoutGroup(*doc.layout, childId, childDestination);
        doc.notes = remapNotes(doc.notes, beforeChildMove, *doc.layout);
        QCOMPARE(doc.notes.size(), 1);
        QCOMPARE(doc.notes[0].id, parentNote.id);
        QCOMPARE(doc.notes[0].rect, parentDestination.toRect());
        const auto parentNoteDestination = movementAnnotationDestination(doc.notes[0], *doc.layout);
        QVERIFY(parentNoteDestination.has_value());
        QCOMPARE(*parentNoteDestination, parentDestination);
        markers = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(markers.size(), 2);
        bool parentFound = false, childFound = false;
        for (const auto &marker : markers) {
            if (marker.source == parentSource) {
                parentFound = true;
                QCOMPARE(marker.destination, parentDestination);
                QCOMPARE(marker.number, 1);
                QCOMPARE(marker.noteIndex, 0);
            } else if (marker.source == childSource) {
                childFound = true;
                QCOMPARE(marker.destination, childDestination);
                QCOMPARE(marker.number, 0);
                QCOMPARE(marker.noteIndex, -1);
            } else {
                QFAIL("A child inherited from the parent move must not gain an independent trajectory.");
            }
        }
        QVERIFY(parentFound);
        QVERIFY(childFound);
        // Pixel reconstruction needs a child and two remaining rectangles, but the UI needs two arrows.
        QCOMPARE(exportLayoutChanges(*doc.layout).size(), 3);
        canvas.refresh();
        layout.setState(*doc.layout);
        layout.setAnnotations(doc.notes);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, parentMidpoint.toPoint());
        QCOMPARE(edits.size(), 1);
        QCOMPARE(editedId(), parentNote.id);
        QCOMPARE(moves.size(), 1);
        const auto childMidpoint = (childSource.center() + childDestination.center()) / 2;
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, childMidpoint.toPoint());
        QCOMPARE(moves.size(), 2);
        QCOMPARE(moves.last().at(0).toRectF(), childSource);
        QCOMPARE(moves.last().at(1).toRectF(), childDestination);
        QCOMPARE(edits.size(), 1);
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            QVERIFY(rendered(widget).save(QDir(folder).filePath(
                QString("nested-movements-%1.png").arg(exploded ? "explosion" : "canvas"))));
        }
        auto childNote = parentNote;
        childNote.id = Note().id;
        childNote.movementSource = childSource;
        childNote.rect = childDestination.toRect();
        childNote.comment = "单独下移第一个单元格。";
        doc.notes.append(childNote);
        canvas.refresh();
        layout.setAnnotations(doc.notes);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, parentMidpoint.toPoint());
        QCOMPARE(edits.size(), 2);
        QCOMPARE(editedId(), parentNote.id);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, childMidpoint.toPoint());
        QCOMPARE(edits.size(), 3);
        QCOMPARE(editedId(), childNote.id);
        QCOMPARE(moves.size(), 2);
    }
    void mergedMovementKeepsAdditionalNoteBadgesEditable_data() {
        numberedMovementBadgesOpenExistingNotesAndDrafts_data();
    }
    void mergedMovementKeepsAdditionalNoteBadgesEditable() {
        QFETCH(bool, exploded);
        auto doc = numberedMovements();
        auto additional = movementNote(0);
        additional.movementSource = QRectF(130, 100, 70, 70);
        additional.rect = QRect(540, 100, 70, 70);
        additional.comment = "同一移动中的另一处意见。";
        doc.notes.append(additional);
        QCOMPARE(movementAnnotationIndex(doc.notes[2], *doc.layout),
                 movementAnnotationIndex(additional, *doc.layout));
        const auto savedNotes = doc.notes;
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.setMode(Canvas::Adjust);
        canvas.setZoom(1.25);
        LayoutCanvas layout(doc.image, *doc.layout);
        layout.setAnnotations(doc.notes);
        layout.setZoom(1.25);
        layout.setGuides(false);
        QWidget &widget = exploded ? static_cast<QWidget &>(layout) : static_cast<QWidget &>(canvas);
        widget.show();
        QSignalSpy canvasEdits(&canvas, &Canvas::editRequested);
        QSignalSpy layoutEdits(&layout, &LayoutCanvas::noteEditRequested);
        QSignalSpy canvasMoves(&canvas, &Canvas::movementAnnotationRequested);
        QSignalSpy layoutMoves(&layout, &LayoutCanvas::movementAnnotationRequested);
        auto &edits = exploded ? layoutEdits : canvasEdits;
        auto editedId = [&] {
            return exploded ? edits.last().at(0).toString() : qvariant_cast<Note>(edits.last().at(0)).id;
        };
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier,
                          (movementMidpoint(0, 1.25) + QPointF(0, 12)).toPoint());
        QCOMPARE(edits.size(), 1);
        QCOMPARE(editedId(), doc.notes[2].id);
        // Only the first note moves onto the shared arrow. The other keeps its own corner badge.
        const auto additionalBadge = QPointF(additional.rect.topLeft()) * 1.25 + QPointF(18, 18);
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, additionalBadge.toPoint());
        QCOMPARE(edits.size(), 2);
        QCOMPARE(editedId(), additional.id);
        QVERIFY(canvasMoves.isEmpty());
        QVERIFY(layoutMoves.isEmpty());
        QCOMPARE(doc.notes, savedNotes);
    }
    void numberedMovementAppearanceAndFixedScreenSize_data() {
        QTest::addColumn<bool>("exploded");
        QTest::addColumn<bool>("annotated");
        QTest::addColumn<bool>("dark");
        for (const bool exploded : {false, true})
            for (const bool annotated : {false, true})
                for (const bool dark : {false, true}) {
                    const auto name = QString("%1-%2-%3")
                                          .arg(exploded ? "explosion" : "canvas",
                                               annotated ? "mixed-notes" : "no-notes", dark ? "dark" : "light");
                    QTest::newRow(qPrintable(name)) << exploded << annotated << dark;
                }
    }
    void numberedMovementAppearanceAndFixedScreenSize() {
        QFETCH(bool, exploded);
        QFETCH(bool, annotated);
        QFETCH(bool, dark);
        applyTheme(dark ? ThemeMode::Dark : ThemeMode::Light);
        auto doc = numberedMovements(annotated);
        const auto savedNotes = doc.notes;
        Canvas canvas;
        canvas.setDocument(&doc);
        canvas.setLayoutPreview(true);
        canvas.setMode(Canvas::Adjust);
        LayoutCanvas layout(doc.image, *doc.layout);
        layout.setAnnotations(doc.notes);
        layout.setGuides(false);
        QWidget &widget = exploded ? static_cast<QWidget &>(layout) : static_cast<QWidget &>(canvas);
        widget.show();
        for (const double zoom : {.5, 1.0, 1.75}) {
            if (exploded) {
                layout.setZoom(zoom);
                layout.setAnnotationsVisible(false);
            } else {
                canvas.setZoom(zoom);
                canvas.setAnnotationsVisible(false);
            }
            const auto hidden = rendered(widget);
            if (exploded) layout.setAnnotationsVisible(true);
            else canvas.setAnnotationsVisible(true);
            const auto visible = rendered(widget);
            for (int row = 0; row < 3; ++row) {
                const auto center = movementMidpoint(row, zoom).toPoint();
                // Only an annotated arrow has a numbered circle, with a fixed screen-space radius.
                const QRect rim(center + QPoint(-2, 10), QSize(5, 5));
                if (annotated && row == 0)
                    QVERIFY(visible.copy(rim) != hidden.copy(rim));
                else
                    QCOMPARE(visible.copy(rim), hidden.copy(rim));
                // The bare trajectory remains visible before it has any annotation.
                const QRect line(center + QPoint(-2, -2), QSize(5, 5));
                QVERIFY(visible.copy(line) != hidden.copy(line));
                // Its radius must not grow with the image zoom, and hiding must remove its pixels.
                const QRect outside(center + QPoint(-1, 19), QSize(3, 3));
                QCOMPARE(visible.copy(outside), hidden.copy(outside));
            }
            const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
            if (zoom == 1 && !folder.isEmpty()) {
                QVERIFY(QDir().mkpath(folder));
                const auto name = QString("movement-numbers-%1-%2-%3.png")
                                      .arg(exploded ? "explosion" : "canvas",
                                           annotated ? "mixed-notes" : "no-notes", dark ? "dark" : "light");
                QVERIFY(visible.save(QDir(folder).filePath(name)));
            }
        }
        QCOMPARE(doc.notes, savedNotes);
        applyTheme(ThemeMode::Light);
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
