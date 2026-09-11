#include "layout.h"
#include "model.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <stdexcept>
using namespace h2d;
namespace {
Candidate candidate(QRect bounds, const QString &label) {
    auto target = manualTarget();
    target["label"] = label;
    return {bounds, target};
}
QVector<Candidate> tableRegions() {
    return {candidate({10, 10, 30, 20}, "1"), candidate({40, 10, 30, 20}, "2"),
            candidate({10, 30, 30, 20}, "3"), candidate({40, 30, 30, 20}, "4"),
            candidate({10, 10, 60, 40}, "1234")};
}
QString groupId(const LayoutState &state, const QString &label) {
    for (const auto &group : state.groups)
        if (group.label == label)
            return group.id;
    return {};
}
const LayoutPiece *pieceById(const LayoutState &state, const QString &id) {
    for (const auto &piece : state.pieces)
        if (piece.id == id)
            return &piece;
    return nullptr;
}
QStringList members(const LayoutState &state, const QString &id) {
    for (const auto &group : state.groups)
        if (group.id == id)
            return group.pieces;
    return {};
}
QImage sourceImage() {
    QImage image(160, 120, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor((x * 13) % 256, (y * 17) % 256, (x + y) % 256));
    QPainter painter(&image);
    painter.fillRect(QRect(10, 10, 30, 20), QColor("#f04050"));
    painter.fillRect(QRect(40, 10, 30, 20), QColor("#20a060"));
    painter.fillRect(QRect(10, 30, 30, 20), QColor("#3070e0"));
    painter.fillRect(QRect(40, 30, 30, 20), QColor("#f0b030"));
    return image;
}
bool rejects(const QJsonObject &json, QSize size) {
    try {
        importLayout(json, size);
        return false;
    } catch (const std::runtime_error &) {
        return true;
    }
}
QJsonObject changedPiece(QJsonObject result, int index, const QString &field, const QJsonValue &value) {
    auto pieces = result["pieces"].toArray();
    auto piece = pieces[index].toObject();
    piece[field] = value;
    pieces[index] = piece;
    result["pieces"] = pieces;
    return result;
}
QPointF oppositeCorner(QRectF r, int handle) {
    return {(handle == 0 || handle == 6) ? r.right() : r.left(),
            (handle == 0 || handle == 2) ? r.bottom() : r.top()};
}
} // namespace
class LayoutTests : public QObject {
    Q_OBJECT
  private slots:
    void initialPartitionsPreserveEveryPixel() {
        const auto original = sourceImage();
        auto regions = tableRegions();
        regions.append(candidate({25, 0, 30, 90}, "crossing"));
        regions.append(regions.first());
        auto state = createLayout(original.size(), regions);
        validateLayout(state, original.size());
        QCOMPARE(state.groups.size(), 7);
        QVERIFY(state.pieces.size() > 4);
        QSet<QString> ids;
        for (const auto &piece : state.pieces) {
            QVERIFY(!ids.contains(piece.id));
            ids.insert(piece.id);
            QCOMPARE(piece.source, piece.destination);
        }
        for (int y = 0; y < original.height(); ++y) {
            for (int x = 0; x < original.width(); ++x) {
                const QPointF center(x + 0.5, y + 0.5);
                int covering = 0;
                for (const auto &piece : state.pieces)
                    covering += piece.source.contains(center) ? 1 : 0;
                QCOMPARE(covering, 1);
            }
        }
        QCOMPARE(renderLayout(original, state).convertToFormat(QImage::Format_ARGB32), original);
        auto empty = createLayout(original.size(), {});
        QCOMPARE(empty.pieces.size(), 1);
        QCOMPARE(empty.groups.size(), 1);
        QCOMPARE(renderLayout(original, empty).convertToFormat(QImage::Format_ARGB32), original);
    }
    void movingARegionLeavesAnEmptyHole() {
        const auto original = sourceImage();
        auto state = createLayout(original.size(), tableRegions());
        const auto id = groupId(state, "1");
        QVERIFY(!id.isEmpty());
        const auto before = state.pieces;
        transformLayoutGroup(state, id, {90, 70, 30, 20});
        QCOMPARE(layoutBounds(state, id), QRectF(90, 70, 30, 20));
        validateLayout(state, original.size());
        for (const auto &piece : before) {
            const auto *after = pieceById(state, piece.id);
            QVERIFY(after);
            QCOMPARE(after->source, piece.source);
        }
        const auto rendered = renderLayout(original, state);
        QCOMPARE(rendered.pixelColor(20, 20).alpha(), 0);
        QCOMPARE(rendered.pixelColor(100, 80), QColor("#f04050"));
        QCOMPARE(rendered.pixelColor(50, 20), original.pixelColor(50, 20));
        QCOMPARE(rendered.pixelColor(5, 5), original.pixelColor(5, 5));
    }
    void transformingAParentPreservesChildRelationships() {
        auto state = createLayout({160, 120}, tableRegions());
        const auto parentId = groupId(state, "1234");
        const auto before = state;
        const auto parentMembers = members(state, parentId);
        const QRectF oldBounds(10, 10, 60, 40), newBounds(50, 50, 90, 60);
        transformLayoutGroup(state, parentId, newBounds);
        QCOMPARE(layoutBounds(state, parentId), newBounds);
        QCOMPARE(layoutBounds(state, groupId(state, "1")), QRectF(50, 50, 45, 30));
        QCOMPARE(layoutBounds(state, groupId(state, "2")), QRectF(95, 50, 45, 30));
        QCOMPARE(layoutBounds(state, groupId(state, "3")), QRectF(50, 80, 45, 30));
        QCOMPARE(layoutBounds(state, groupId(state, "4")), QRectF(95, 80, 45, 30));
        for (const auto &piece : before.pieces) {
            const auto *after = pieceById(state, piece.id);
            QVERIFY(after);
            QCOMPARE(after->source, piece.source);
            if (!parentMembers.contains(piece.id)) {
                QCOMPARE(after->destination, piece.destination);
                continue;
            }
            QCOMPARE(after->destination.topLeft(),
                     newBounds.topLeft() + (piece.destination.topLeft() - oldBounds.topLeft()) * 1.5);
            QCOMPARE(after->destination.size(), piece.destination.size() * 1.5);
        }
        validateLayout(state, {160, 120});
    }
    void choicesIncludeEveryContainingRegion() {
        QVector<Candidate> regions{candidate({10, 30, 20, 20}, "3"), candidate({10, 30, 40, 20}, "34"),
                                   candidate({10, 30, 40, 40}, "3456"), candidate({10, 10, 40, 60}, "123456"),
                                   candidate({15, 20, 10, 60}, "crossing")};
        auto state = createLayout({120, 100}, regions);
        QStringList labels;
        double previousArea = 0;
        for (const auto &choice : layoutChoices(state, {20, 40})) {
            labels.append(choice.label);
            const auto area = choice.bounds.width() * choice.bounds.height();
            QVERIFY(area >= previousArea);
            previousArea = area;
        }
        QCOMPARE(labels, QStringList({"3", "crossing", "34", "3456", "123456", "整个图片"}));
        QVERIFY(layoutChoices(state, {120, 100}).isEmpty());
        for (const auto &choice : layoutChoices(state, {30, 40}))
            QVERIFY(choice.label != "3");
        QVERIFY(!addLayoutRegion(state, {10, 30, 20, 20}, "manual 3").isEmpty());
        int sameBounds = 0;
        for (const auto &choice : layoutChoices(state, {20, 40}))
            sameBounds += choice.bounds == QRectF(10, 30, 20, 20) ? 1 : 0;
        QCOMPARE(sameBounds, 2);
    }
    void cornersKeepTheirAspectRatio_data() {
        QTest::addColumn<int>("handle");
        QTest::addColumn<QPointF>("delta");
        for (int handle : {0, 2, 4, 6})
            for (auto delta : {QPointF(13, 7), QPointF(-19, 11), QPointF(1000, 1000), QPointF(-1000, -1000),
                               QPointF(1000, -1000)}) {
                const auto name = QString("%1-%2-%3").arg(handle).arg(delta.x()).arg(delta.y()).toUtf8();
                QTest::newRow(name.constData()) << handle << delta;
            }
    }
    void cornersKeepTheirAspectRatio() {
        QFETCH(int, handle);
        QFETCH(QPointF, delta);
        const QRectF before(30, 25, 60, 40);
        const QSize canvas(160, 120);
        const auto after = resizeLayoutRect(before, delta, handle, canvas);
        QVERIFY(after.width() >= 1 - 1e-9 && after.height() >= 1 - 1e-9);
        QVERIFY(after.left() >= -1e-9 && after.top() >= -1e-9);
        QVERIFY(after.right() <= canvas.width() + 1e-9 && after.bottom() <= canvas.height() + 1e-9);
        QVERIFY(std::abs(after.width() / after.height() - 1.5) < 1e-9);
        QCOMPARE(oppositeCorner(after, handle), oppositeCorner(before, handle));
    }
    void edgesOnlyChangeTheirOwnDimension() {
        const QRectF before(30, 25, 60, 40);
        const QSize canvas(160, 120);
        QCOMPARE(resizeLayoutRect(before, {9, 7}, 1, canvas), QRectF(30, 32, 60, 33));
        QCOMPARE(resizeLayoutRect(before, {9, 7}, 3, canvas), QRectF(30, 25, 69, 40));
        QCOMPARE(resizeLayoutRect(before, {9, 7}, 5, canvas), QRectF(30, 25, 60, 47));
        QCOMPARE(resizeLayoutRect(before, {9, 7}, 7, canvas), QRectF(39, 25, 51, 40));
        for (int handle : {1, 3, 5, 7})
            for (auto delta : {QPointF(1000, 1000), QPointF(-1000, -1000)}) {
                const auto after = resizeLayoutRect(before, delta, handle, canvas);
                QVERIFY(after.width() >= 1 && after.height() >= 1);
                QVERIFY(QRectF(QPointF(0, 0), canvas).contains(after));
                if (handle == 1 || handle == 5) {
                    QCOMPARE(after.x(), before.x());
                    QCOMPARE(after.width(), before.width());
                    QCOMPARE(handle == 1 ? after.bottom() : after.top(),
                             handle == 1 ? before.bottom() : before.top());
                } else {
                    QCOMPARE(after.y(), before.y());
                    QCOMPARE(after.height(), before.height());
                    QCOMPARE(handle == 7 ? after.right() : after.left(),
                             handle == 7 ? before.right() : before.left());
                }
            }
    }
    void wheelScalePreservesRatioAndStaysOnCanvas() {
        const QRectF before(120, 90, 30, 20);
        const QSize canvas(160, 120);
        for (double factor : {0.01, 0.5, 1.25, 1000.0}) {
            const auto after = scaleLayoutRect(before, factor, canvas);
            QVERIFY(after.width() >= 1 - 1e-9 && after.height() >= 1 - 1e-9);
            QVERIFY(QRectF(QPointF(0, 0), canvas).contains(after));
            QVERIFY(std::abs(after.width() / after.height() - 1.5) < 1e-9);
        }
        const auto scaled = scaleLayoutRect({30, 30, 30, 20}, 1.25, canvas);
        QCOMPARE(scaled.center(), QPointF(45, 40));
        QCOMPARE(scaled.size(), QSizeF(37.5, 25));
    }
    void subpixelChildrenKeepSizeWhenMovedOrResized() {
        auto state = createLayout({160, 120}, tableRegions());
        transformLayoutGroup(state, groupId(state, "1234"), {159, 118.5, 1, 1.5});
        const auto childId = groupId(state, "4");
        const auto original = layoutBounds(state, childId);
        QCOMPARE(original, QRectF(159.5, 119.25, 0.5, 0.75));
        auto moved = original.translated(-10, -10);
        transformLayoutGroup(state, childId, moved);
        QCOMPARE(layoutBounds(state, childId), moved);
        QCOMPARE(constrainLayoutRect(moved, state.canvas), moved);

        const auto resized = resizeLayoutRect(moved, {-0.1, -0.15}, 0, state.canvas);
        QVERIFY(resized.width() > moved.width() && resized.width() < 1);
        QVERIFY(std::abs(resized.width() / resized.height() - 2.0 / 3.0) < 1e-9);
        transformLayoutGroup(state, childId, resized);
        QCOMPARE(layoutBounds(state, childId), resized);
        validateLayout(state, state.canvas);
        QVERIFY(importLayout(exportLayout(state), state.canvas) == state);
    }

    void subpixelHandlesNeverJumpOrInvertAtCanvasEdges() {
        const QSize canvas(160, 120);
        const QRectF full(QPointF(0, 0), canvas);
        for (const auto &before : {QRectF(0, 0, 0.4, 0.7), QRectF(159.6, 0, 0.4, 0.7),
                                   QRectF(0, 119.3, 0.4, 0.7), QRectF(159.6, 119.3, 0.4, 0.7)}) {
            for (int handle = 0; handle < 8; ++handle) {
                QCOMPARE(resizeLayoutRect(before, {}, handle, canvas), before);
                for (const auto &delta : {QPointF(1000, 1000), QPointF(-1000, -1000), QPointF(1000, -1000),
                                          QPointF(-1000, 1000)}) {
                    const auto after = resizeLayoutRect(before, delta, handle, canvas);
                    QVERIFY(after.width() > 0 && after.height() > 0);
                    QVERIFY(after.left() >= -1e-9 && after.top() >= -1e-9);
                    QVERIFY(after.right() <= full.right() + 1e-9 && after.bottom() <= full.bottom() + 1e-9);
                    QCOMPARE(constrainLayoutRect(after, canvas), after);
                    if (handle % 2 == 0) {
                        QVERIFY(std::abs(after.width() / after.height() - before.width() / before.height()) <
                                1e-9);
                        QCOMPARE(oppositeCorner(after, handle), oppositeCorner(before, handle));
                    } else if (handle == 1 || handle == 5) {
                        QCOMPARE(after.x(), before.x());
                        QCOMPARE(after.width(), before.width());
                    } else {
                        QCOMPARE(after.y(), before.y());
                        QCOMPARE(after.height(), before.height());
                    }
                }
            }
            QCOMPARE(scaleLayoutRect(before, 1, canvas), before);
            const auto grown = scaleLayoutRect(before, 1.1, canvas);
            QVERIFY(std::abs(grown.width() / before.width() - 1.1) < 1e-9);
            QVERIFY(std::abs(grown.height() / before.height() - 1.1) < 1e-9);
        }
    }

    void manualCutAfterMoveAndScalePreservesExistingGroups() {
        const auto original = sourceImage();
        auto state = createLayout(original.size(), tableRegions());
        const auto cellId = groupId(state, "1"), parentId = groupId(state, "1234");
        transformLayoutGroup(state, cellId, {80, 60, 60, 40});
        const auto beforeCut = renderLayout(original, state);
        const auto countBefore = members(state, cellId).size();
        const auto manualId = addLayoutRegion(state, {90, 70, 20, 20}, "精细切片");
        QVERIFY(!manualId.isEmpty());
        QCOMPARE(layoutBounds(state, manualId), QRectF(90, 70, 20, 20));
        QCOMPARE(layoutBounds(state, cellId), QRectF(80, 60, 60, 40));
        QVERIFY(members(state, cellId).size() > countBefore);
        QCOMPARE(renderLayout(original, state), beforeCut);
        const auto parentMembers = members(state, parentId), manualMembers = members(state, manualId);
        bool foundMappedPart = false;
        for (const auto &id : members(state, cellId)) {
            QVERIFY(parentMembers.contains(id));
            const auto *piece = pieceById(state, id);
            QVERIFY(piece);
            if (manualMembers.contains(id)) {
                QCOMPARE(piece->source, QRectF(15, 15, 10, 10));
                QCOMPARE(piece->destination, QRectF(90, 70, 20, 20));
                foundMappedPart = true;
            }
        }
        QVERIFY(foundMappedPart);
        transformLayoutGroup(state, manualId, {5, 80, 20, 20});
        auto rendered = renderLayout(original, state);
        QCOMPARE(rendered.pixelColor(15, 90), QColor("#f04050"));
        QCOMPARE(rendered.pixelColor(100, 80).alpha(), 0);
        validateLayout(state, original.size());
    }
    void layoutJsonRoundTripRejectsCorruption() {
        auto state = createLayout({160, 120}, tableRegions());
        transformLayoutGroup(state, groupId(state, "1"), {85.5, 60.25, 60.5, 40.25});
        QVERIFY(!addLayoutRegion(state, {90.25, 70.5, 20.5, 15.25}, "fractional").isEmpty());
        const auto valid = exportLayout(state);
        const auto decoded = QJsonDocument::fromJson(QJsonDocument(valid).toJson()).object();
        QVERIFY(importLayout(decoded, state.canvas) == state);
        auto pieces = valid["pieces"].toArray();
        QVERIFY(pieces.size() >= 2);
        QVERIFY(rejects(changedPiece(valid, 1, "source", pieces[0].toObject()["source"]), state.canvas));
        QVERIFY(rejects(changedPiece(valid, 1, "id", pieces[0].toObject()["id"]), state.canvas));
        auto destination = pieces[0].toObject()["destination"].toObject();
        destination["x2"] = state.canvas.width() + 1;
        QVERIFY(rejects(changedPiece(valid, 0, "destination", destination), state.canvas));
        auto source = pieces[0].toObject()["source"].toObject();
        source["x1"] = -1;
        QVERIFY(rejects(changedPiece(valid, 0, "source", source), state.canvas));
        source = pieces[0].toObject()["source"].toObject();
        source["x2"] = source["x1"];
        QVERIFY(rejects(changedPiece(valid, 0, "source", source), state.canvas));
        source = pieces[0].toObject()["source"].toObject();
        source["x1"] = "0";
        QVERIFY(rejects(changedPiece(valid, 0, "source", source), state.canvas));
        for (auto references : {QJsonArray{"nonexistent"},
                                QJsonArray{pieces[0].toObject()["id"], pieces[0].toObject()["id"]}}) {
            auto bad = valid;
            auto groups = bad["groups"].toArray();
            auto group = groups[0].toObject();
            group["pieceIds"] = references;
            groups[0] = group;
            bad["groups"] = groups;
            QVERIFY(rejects(bad, state.canvas));
        }
        auto bad = valid;
        pieces.removeLast();
        bad["pieces"] = pieces;
        QVERIFY(rejects(bad, state.canvas));
        bad = valid;
        bad["width"] = 160.5;
        QVERIFY(rejects(bad, state.canvas));
        bad = valid;
        bad["unexpected"] = true;
        QVERIFY(rejects(bad, state.canvas));
    }
    void minimalChangesOnlyKeepActualTransforms() {
        auto state = createLayout({160, 120}, tableRegions());
        QVERIFY(exportLayoutChanges(state).isEmpty());
        const auto cell = groupId(state, "1");
        transformLayoutGroup(state, cell, {90, 70, 30, 20});
        auto changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[0].toObject(),
                 (QJsonObject{{"from", rectJson({10, 10, 30, 20})}, {"to", rectJson({90, 70, 30, 20})}}));
        transformLayoutGroup(state, cell, {10, 10, 30, 20});
        QVERIFY(exportLayoutChanges(state).isEmpty());
        QCOMPARE(renderLayout(sourceImage(), state).convertToFormat(QImage::Format_ARGB32), sourceImage());
    }
    void minimalChangesMergeAParentAndKeepNestedEditsSeparate() {
        auto state = createLayout({160, 120}, tableRegions());
        const auto parent = groupId(state, "1234");
        transformLayoutGroup(state, parent, {50, 50, 90, 60});
        auto changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[0].toObject(),
                 (QJsonObject{{"from", rectJson({10, 10, 60, 40})}, {"to", rectJson({50, 50, 90, 60})}}));
        auto restored = importLayoutChanges(changes, state.canvas);
        QCOMPARE(renderLayout(sourceImage(), restored), renderLayout(sourceImage(), state));

        transformLayoutGroup(state, groupId(state, "1"), {10, 70, 45, 30});
        changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(),
                 3); // A differently moved child and the two rectangles of the L-shaped remainder.
        restored = importLayoutChanges(changes, state.canvas);
        QCOMPARE(renderLayout(sourceImage(), restored), renderLayout(sourceImage(), state));
        double area = 0;
        for (const auto &value : changes) {
            const auto from = value.toObject()["from"].toObject();
            area += (from["x2"].toDouble() - from["x1"].toDouble()) *
                    (from["y2"].toDouble() - from["y1"].toDouble());
        }
        QCOMPARE(area, 2400.0);
    }
    void minimalChangesPreserveLayerOrderAndSwaps() {
        auto state = createLayout({160, 120}, tableRegions());
        transformLayoutGroup(state, groupId(state, "1"), {80, 10, 30, 20});
        transformLayoutGroup(state, groupId(state, "3"), {90, 10, 30, 20});
        transformLayoutGroup(state, groupId(state, "2"), {110, 10, 30, 20});
        auto changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(), 3); // Merging adjacent 1 and 2 would cover component 3 incorrectly.
        auto restored = importLayoutChanges(changes, state.canvas);
        QCOMPARE(renderLayout(sourceImage(), restored), renderLayout(sourceImage(), state));
        QCOMPARE(renderLayout(sourceImage(), restored).pixelColor(100, 20), QColor("#3070e0"));

        state = createLayout({160, 120}, tableRegions());
        transformLayoutGroup(state, groupId(state, "1"), {40, 10, 30, 20});
        transformLayoutGroup(state, groupId(state, "2"), {10, 10, 30, 20});
        restored = importLayoutChanges(exportLayoutChanges(state), state.canvas);
        QCOMPARE(renderLayout(sourceImage(), restored), renderLayout(sourceImage(), state));
        QCOMPARE(renderLayout(sourceImage(), restored).pixelColor(20, 20), QColor("#20a060"));
        QCOMPARE(renderLayout(sourceImage(), restored).pixelColor(50, 20), QColor("#f04050"));

        // Returning a region to its source removes its record even after it was brought to the front.
        transformLayoutGroup(state, groupId(state, "2"), {40, 10, 30, 20});
        changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(), 1);
        restored = importLayoutChanges(changes, state.canvas);
        QCOMPARE(renderLayout(sourceImage(), restored), renderLayout(sourceImage(), state));
    }
    void minimalChangesCollapseDenseDetectorPartitions() {
        QVector<Candidate> candidates;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 20; ++x)
                candidates.append(candidate({10 + x * 4, 10 + y * 4, 4, 4}, QString("%1-%2").arg(x).arg(y)));
        candidates.append(candidate({10, 10, 80, 40}, "table"));
        auto state = createLayout({200, 150}, candidates);
        QVERIFY(state.pieces.size() >= 200);
        transformLayoutGroup(state, groupId(state, "table"), {100, 80, 80, 40});
        auto changes = exportLayoutChanges(state);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[0].toObject()["from"], QJsonValue(rectJson({10, 10, 80, 40})));
    }
    void minimalFeedbackRoundTripsAndRejectsCorruption() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto document = fromImage(sourceImage(), "demo", "精简反馈");
        document.layout = createLayout(document.image.size(), tableRegions());
        transformLayoutGroup(*document.layout, groupId(*document.layout, "1234"), {50, 50, 90, 60});
        Note point;
        point.point = {20, 20};
        point.comment = "加大标题";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {10, 10, 60, 40};
        rectangle.comment = "表格右移";
        document.notes = {point, rectangle};
        const auto feedback = exportFeedback(document);
        QCOMPARE(feedback.keys(), QStringList({"annotationSpace", "annotations", "changes"}));
        QCOMPARE(feedback["annotations"].toArray()[0].toObject(),
                 (QJsonObject{{"point", QJsonObject{{"x", 20}, {"y", 20}}}, {"text", "加大标题"}}));
        QCOMPARE(feedback["annotations"].toArray()[1].toObject(),
                 (QJsonObject{{"rectangle", rectJson({10, 10, 60, 40})}, {"text", "表格右移"}}));
        const auto compact = serializeFeedback(document);
        QVERIFY(compact.size() < 350);
        const auto path = directory.filePath("feedback-minimal.json");
        saveBytes(path, compact);
        QVERIFY_EXCEPTION_THROWN(loadDocument(path),
                                 std::runtime_error); // The matching source image is required.
        saveBytes(directory.filePath("feedback-minimal.png"), document.png);
        const auto restored = loadDocument(path);
        QVERIFY(restored.layout.has_value());
        QCOMPARE(exportFeedback(restored), feedback);
        QCOMPARE(renderLayout(restored.image, *restored.layout),
                 renderLayout(document.image, *document.layout));
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            auto legacy = feedback;
            legacy.remove("annotationSpace");
            saveBytes(QDir(folder).filePath("feedback-minimal.json"),
                      QJsonDocument(legacy).toJson(QJsonDocument::Compact));
            saveBytes(QDir(folder).filePath("feedback-minimal.png"), document.png);
        }
        auto invalid = feedback;
        invalid["tool"] = "unexpected";
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        invalid = feedback;
        auto changes = feedback["changes"].toArray();
        changes.append(changes.first());
        invalid["changes"] = changes;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        invalid = feedback;
        auto annotations = feedback["annotations"].toArray();
        auto badPoint = annotations[0].toObject();
        badPoint["rectangle"] = rectJson({10, 10, 60, 40});
        annotations[0] = badPoint;
        invalid["annotations"] = annotations;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        invalid = feedback;
        changes = feedback["changes"].toArray();
        auto change = changes[0].toObject();
        change["to"] = rectJson({159, 119, 100, 100});
        changes[0] = change;
        invalid["changes"] = changes;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        invalid = feedback;
        change["to"] = change["from"];
        changes[0] = change;
        invalid["changes"] = changes;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
    }
    void embeddedFeedbackNeedsNoSidecarAndPreservesOriginal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto document = fromImage(sourceImage(), "demo", "内嵌原图反馈");
        document.layout = createLayout(document.image.size(), tableRegions());
        transformLayoutGroup(*document.layout, groupId(*document.layout, "1"), {90, 70, 30, 20});
        Note point;
        point.point = {100, 80};
        point.comment = "编辑后添加的批注";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {90, 70, 30, 20};
        rectangle.comment = "这里加大文字";
        document.notes = {point, rectangle};
        const auto feedback = exportFeedback(document, true);
        QCOMPARE(feedback.keys(), QStringList({"annotationSpace", "annotations", "changes", "image"}));
        QCOMPARE(feedback["annotationSpace"], QJsonValue("result"));
        QCOMPARE(feedback["changes"].toArray().size(), 1);
        QCOMPARE(feedback["image"],
                 QJsonValue("data:image/png;base64," + QString::fromLatin1(document.png.toBase64())));
        const auto path = directory.filePath("embedded.json");
        saveBytes(path, serializeFeedback(document, true));
        QVERIFY(!QFileInfo::exists(directory.filePath("embedded.png")));
        const auto restored = loadDocument(path);
        QCOMPARE(restored.png, document.png);
        QCOMPARE(restored.image, document.image);
        QCOMPARE(exportFeedback(restored, true), feedback);
        QVERIFY(restored.layout.has_value());
        QCOMPARE(renderLayout(restored.image, *restored.layout),
                 renderLayout(document.image, *document.layout));

        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            saveBytes(QDir(folder).filePath("feedback-v0.7.json"), serializeFeedback(document, true));
            saveBytes(QDir(folder).filePath("feedback-v0.7-no-image.json"), serializeFeedback(document));
        }
        for (const auto &invalidImage :
             {QJsonValue(true), QJsonValue("data:image/jpeg;base64,AA=="),
              QJsonValue("data:image/png;base64,!!!"), QJsonValue("data:image/png;base64,QQ=="),
              QJsonValue("data:image/png;base64,")}) {
            auto invalid = feedback;
            invalid["image"] = invalidImage;
            saveBytes(path, QJsonDocument(invalid).toJson(QJsonDocument::Compact));
            QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        }
        auto invalid = feedback;
        invalid["annotationSpace"] = "original";
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        invalid = feedback;
        invalid["extra"] = true;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, document.image), std::runtime_error);
        auto badPng = document.png;
        for (int i = 16; i < 24; ++i)
            badPng[i] = char(0xff); // Reject absurd dimensions before PNG decoding allocates pixels.
        invalid = feedback;
        invalid["image"] = "data:image/png;base64," + QString::fromLatin1(badPng.toBase64());
        saveBytes(path, QJsonDocument(invalid).toJson(QJsonDocument::Compact));
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        QImage wrongImage = document.image;
        wrongImage.setPixelColor(0, 0, Qt::magenta);
        QVERIFY_EXCEPTION_THROWN(loadFeedback(feedback, wrongImage), std::runtime_error);
    }
    void legacyAnnotationsMigrateAndFullProjectRoundTrips() {
        auto document = fromImage(sourceImage(), "demo", "旧格式迁移");
        document.layout = createLayout(document.image.size(), tableRegions());
        transformLayoutGroup(*document.layout, groupId(*document.layout, "1"), {90, 70, 30, 20});
        Note point;
        point.point = {20, 20};
        point.comment = "旧格式原图坐标";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {10, 10, 30, 20};
        rectangle.comment = "旧矩形";
        document.notes = {point, rectangle};
        auto legacy = exportFeedback(document);
        legacy.remove("annotationSpace");
        auto restored = loadFeedback(legacy, document.image);
        QCOMPARE(restored.notes[0].point, QPoint(100, 80));
        QCOMPARE(restored.notes[1].rect, QRect(90, 70, 30, 20));

        // Old v2 contains original coordinates; current documents contain result coordinates.
        const auto full = exportDocument(restored, true);
        QCOMPARE(full["annotations"].toArray()[0].toObject()["point"],
                 QJsonValue(QJsonObject{{"x", 20}, {"y", 20}}));
        QTemporaryDir directory;
        const auto path = directory.filePath("old-v2.json");
        saveBytes(path, QJsonDocument(full).toJson());
        const auto fullRestored = loadDocument(path);
        QCOMPARE(fullRestored.notes, restored.notes);
        QCOMPARE(fullRestored.image, document.image);
        QCOMPARE(exportFeedback(fullRestored), exportFeedback(restored));

        Note inHole;
        inHole.point = {20, 20};
        inHole.comment = "调整后空白处也可以批注";
        restored.notes = {inHole};
        QVERIFY_EXCEPTION_THROWN(exportDocument(restored, true), std::runtime_error);
        QCOMPARE(loadFeedback(exportFeedback(restored, true), {}).notes[0].point, inHole.point);
    }
    void annotationsFollowVisibleContentAcrossMovesAndManualCuts() {
        auto before = createLayout({160, 120}, tableRegions());
        auto moved = before;
        const auto cell = groupId(moved, "1");
        transformLayoutGroup(moved, cell, {90, 70, 30, 20});
        Note point;
        point.point = {20, 20};
        point.comment = "已有批注";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {10, 10, 30, 20};
        rectangle.comment = "整块";
        auto notes = remapNotes({point, rectangle}, before, moved);
        QCOMPARE(notes[0].point, QPoint(100, 80));
        QCOMPARE(notes[1].rect, QRect(90, 70, 30, 20));
        Note afterEditing;
        afterEditing.point = {100, 80};
        afterEditing.comment = "移动后添加";
        notes.append(afterEditing);
        auto movedAgain = moved;
        transformLayoutGroup(movedAgain, cell, {80, 60, 60, 40});
        notes = remapNotes(notes, moved, movedAgain);
        QCOMPARE(notes[0].point, QPoint(100, 80));
        QCOMPARE(notes[1].rect, QRect(80, 60, 60, 40));
        QCOMPARE(notes[2].point, QPoint(100, 80));

        auto cut = movedAgain;
        const auto cutId = addLayoutRegion(cut, {90, 70, 20, 20}, "手动切块");
        QVERIFY(!cutId.isEmpty());
        QCOMPARE(remapNotes(notes, movedAgain, cut), notes);
        auto shifted = cut;
        transformLayoutGroup(shifted, cutId, {10, 80, 20, 20});
        const auto remapped = remapNotes(notes, cut, shifted);
        QCOMPARE(remapped[0].point, QPoint(20, 90));
        QCOMPARE(remapped[2].point, QPoint(20, 90));
        QCOMPARE(remapped[1].rect, notes[1].rect); // The whole rectangle no longer shares one transform.

        auto overlap = before;
        transformLayoutGroup(overlap, cell, {40, 10, 30, 20});
        point.point = {50, 20};
        auto lifted = overlap;
        transformLayoutGroup(lifted, cell, {90, 70, 30, 20});
        QCOMPARE(remapNotes({point}, overlap, lifted)[0].point, QPoint(100, 80));
        point.point = {20, 20}; // A hole has no component to follow.
        QCOMPARE(remapNotes({point}, overlap, lifted)[0].point, QPoint(20, 20));

        auto parentMoved = before;
        transformLayoutGroup(parentMoved, groupId(parentMoved, "1234"), {50, 50, 90, 60});
        rectangle.rect = {10, 10, 60, 40};
        QCOMPARE(remapNotes({rectangle}, before, parentMoved)[0].rect, QRect(50, 50, 90, 60));
        auto partlyMoved = before;
        transformLayoutGroup(partlyMoved, cell, {90, 70, 30, 20});
        QCOMPARE(remapNotes({rectangle}, before, partlyMoved)[0].rect, rectangle.rect);
    }
    void fractionalComponentAnnotationsFollowWithoutQuantizationDrift() {
        auto before = createLayout({160, 120}, tableRegions());
        const auto cell = groupId(before, "1");
        transformLayoutGroup(before, cell, {80.25, 60.5, 60.5, 40.25});
        QVERIFY(!addLayoutRegion(before, {90.25, 70.5, 20.5, 15.25}, "内部细分").isEmpty());
        Note frame;
        frame.isPoint = false;
        frame.rect = layoutBounds(before, cell).toAlignedRect();
        frame.comment = "浮点组件的整数批注框";
        auto after = before;
        transformLayoutGroup(after, cell, {75.5, 52.25, 72.75, 48.5});
        auto notes = remapNotes({frame}, before, after);
        QCOMPARE(notes[0].rect, layoutBounds(after, cell).toAlignedRect());

        auto next = after;
        transformLayoutGroup(next, cell, {90.125, 70.875, 31.375, 20.625});
        notes = remapNotes(notes, after, next);
        QCOMPARE(notes[0].rect, layoutBounds(next, cell).toAlignedRect());

        Note arbitrary = frame;
        arbitrary.rect.adjust(-1, -1, 1, 1); // More than pixel quantization; includes distinct background.
        QCOMPARE(remapNotes({arbitrary}, before, after)[0].rect, arbitrary.rect);
        arbitrary.rect = {70, 50, 80, 55};
        QCOMPARE(remapNotes({arbitrary}, before, after)[0].rect, arbitrary.rect);

        // An outer group enclosing different transforms must not capture a note.
        auto partlyMoved = before;
        const auto cut = groupId(partlyMoved, "内部细分");
        transformLayoutGroup(partlyMoved, cut, {10, 80, 20.5, 15.25});
        QCOMPARE(remapNotes({frame}, before, partlyMoved)[0].rect, frame.rect);
    }
    void annotationsUseRenderedLayerOrderAfterAComponentReturnsToSource() {
        auto state = createLayout({160, 120}, tableRegions());
        const auto one = groupId(state, "1"), two = groupId(state, "2");
        transformLayoutGroup(state, two, {90, 70, 30, 20});
        transformLayoutGroup(state, one, {40, 10, 30, 20});
        transformLayoutGroup(state, two, {40, 10, 30, 20}); // Appended last, but now an unchanged background.
        QCOMPARE(renderLayout(sourceImage(), state).pixelColor(50, 20), QColor("#f04050"));
        Note point;
        point.point = {50, 20};
        point.comment = "顶层红色";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {40, 10, 30, 20};
        rectangle.comment = "顶层整块";
        auto after = state;
        transformLayoutGroup(after, one, {90, 70, 30, 20});
        const auto notes = remapNotes({point, rectangle}, state, after);
        QCOMPARE(notes[0].point, QPoint(100, 80));
        QCOMPARE(notes[1].rect, QRect(90, 70, 30, 20));
    }
    void documentRoundTripAndSchemaFixture() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto document = fromImage(sourceImage(), "demo", "大爆炸布局回归");
        document.layout = createLayout(document.image.size(), tableRegions());
        transformLayoutGroup(*document.layout, groupId(*document.layout, "1"), {85, 60, 60, 40});
        QVERIFY(!addLayoutRegion(*document.layout, {90, 70, 20, 20}, "手动切分").isEmpty());
        Note note;
        note.isPoint = false;
        note.rect = {85, 60, 60, 40};
        note.comment = "将红色组件右移，并保持角点缩放比例。";
        document.notes.append(note);
        const auto exported = exportDocument(document, true);
        QCOMPARE(exported["schemaVersion"].toString(), QString("2.0.0"));
        QVERIFY(exported["layout"].isObject());
        const auto path = directory.filePath("feedback-v2.json");
        saveBytes(path, QJsonDocument(exported).toJson());
        const auto restored = loadDocument(path);
        QVERIFY(restored.layout.has_value());
        QVERIFY(*restored.layout == *document.layout);
        QCOMPARE(restored.notes, document.notes);
        QCOMPARE(restored.png, document.png);
        QCOMPARE(renderLayout(restored.image, *restored.layout),
                 renderLayout(document.image, *document.layout));
        QVERIFY(exportDocument(restored, false)["capture"].toObject()["pngBase64"].isNull());
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            saveBytes(QDir(folder).filePath("feedback-v2.json"), QJsonDocument(exported).toJson());
        }
        auto broken = exported;
        auto layout = broken["layout"].toObject();
        auto groups = layout["groups"].toArray();
        auto group = groups[0].toObject();
        group["pieceIds"] = QJsonArray{"missing-piece"};
        groups[0] = group;
        layout["groups"] = groups;
        broken["layout"] = layout;
        saveBytes(path, QJsonDocument(broken).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        broken = exported;
        broken.remove("layout");
        saveBytes(path, QJsonDocument(broken).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        broken = exported;
        layout = broken["layout"].toObject();
        layout["unexpected"] = true;
        broken["layout"] = layout;
        saveBytes(path, QJsonDocument(broken).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        broken = exported;
        broken["schemaVersion"] = "1.0.0";
        saveBytes(path, QJsonDocument(broken).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(LayoutTests)
#include "layout_test.moc"
