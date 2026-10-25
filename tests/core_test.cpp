#include "feedback_test_helpers.h"
#include "detector.h"
#include "model.h"
#include "videoproject.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
using namespace h2d;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void childFeedbackMatchesSharedContractAndNormalizationIsIdempotent() {
        QFile fixture(QFINDTESTDATA("fixtures/child-feedback-contract.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto cases = QJsonDocument::fromJson(fixture.readAll()).object()["cases"].toArray();
        QVERIFY(!cases.isEmpty());
        auto rectangle = [](QJsonValue value) {
            const auto object = value.toObject();
            return QRectF(QPointF(object["x1"].toDouble(), object["y1"].toDouble()),
                          QPointF(object["x2"].toDouble(), object["y2"].toDouble()));
        };
        for (const auto &value : cases) {
            const auto test = value.toObject();
            const auto source = test["document"].toObject();
            QImage image(source["width"].toInt(), source["height"].toInt(), QImage::Format_ARGB32);
            image.fill(Qt::white);
            auto document = fromImage(image, "demo", test["name"].toString());
            QVector<LayoutSequence> sequences;
            for (const auto &movement : source["movements"].toArray())
                sequences.append({rectangle(movement.toObject()["source"]),
                                  {rectangle(movement.toObject()["destination"])}});
            document.layout = importLayoutSequences(sequences, image.size());
            for (const auto &entry : source["notes"].toArray()) {
                const auto object = entry.toObject();
                const auto bounds = rectangle(object["source"]);
                Note note;
                note.isPoint = bounds.width() == 0 && bounds.height() == 0;
                note.point = bounds.topLeft().toPoint(); note.rect = bounds.toRect();
                note.comment = object["text"].toString();
                if (object.contains("movementSource")) note.movementSource = rectangle(object["movementSource"]);
                if (object.contains("parent")) note.parent = ParentLink{
                    rectangle(object["parent"].toObject()["source"]), ParentState::Child};
                document.notes.append(note);
            }
            const auto before = document.notes;
            const auto actual = exportFeedback(document)["objects"].toArray();
            QCOMPARE(actual, test["expected"].toObject()["objects"].toArray());
            QCOMPARE(formatFeedbackJson(QJsonObject{{"objects", actual}}),
                     test["expected"].toObject()["serializedObjects"].toString().toUtf8());
            QCOMPARE(document.notes, before); // Export only projects the model.
            normalizeNoteParents(document);
            const auto normalized = document.notes;
            for (int iteration = 0; iteration < 3; ++iteration) {
                normalizeNoteParents(document);
                QCOMPARE(document.notes, normalized);
                QCOMPARE(exportFeedback(document)["objects"].toArray(), actual);
            }
        }
    }
    void parentLinksResolveOnlyUniqueActiveMovementSources() {
        auto document = fromImage(exampleImage(), "demo", "parent identity");
        document.layout = createLayout(document.image.size(), {});
        const QRectF source(100.4, 100.4, 200, 100), destination(200, 140, 200, 100);
        document.layout->movements = QVector<LayoutMovement>{{{}, source, destination}};
        Note child;
        child.isPoint = false; child.rect = {220, 150, 20, 20}; child.comment = "Child";
        child.parent = ParentLink{QRectF(100, 100, 200, 100), ParentState::Unknown};
        document.notes = {child};
        normalizeNoteParents(document);
        QVERIFY(document.notes[0].parent);
        QCOMPARE(document.notes[0].parent->source, source); // legacy integer bounds
        QVERIFY(document.notes[0].parent->state == ParentState::Unknown);
        document.notes[0].parent->source = QRectF(100.2, 100.2, 200, 100);
        normalizeNoteParents(document);
        QVERIFY(!document.notes[0].parent); // fractional orphan is not a near match
        document.notes[0].parent = ParentLink{source};
        document.layout->movements->append({{}, QRectF(100.3, 100.3, 200, 100), destination});
        normalizeNoteParents(document);
        QVERIFY(!document.notes[0].parent); // two sources round to the same pixels
        document.layout->movements->removeLast();
        document.notes[0].parent = ParentLink{source};
        document.layout->movements->first().destination = source;
        normalizeNoteParents(document);
        QVERIFY(!document.notes[0].parent); // returned home
    }
    void childGroupingAndPendingOwnershipSurviveProjectReload() {
        auto document = fromImage(exampleImage(), "demo", "stable ownership");
        const QRectF firstSource(100, 100, 120, 80), secondSource(350, 100, 120, 80);
        document.layout = importLayoutSequences(
            {{firstSource, {firstSource.translated(30, 100)}},
             {secondSource, {secondSource.translated(30, 100)}}}, document.image.size());
        auto parent = [](const QString &id, QRectF source) {
            Note note; note.id = id; note.isPoint = false;
            note.movementSource = source; note.rect = source.translated(30, 100).toRect();
            return note;
        };
        auto child = [](const QString &id, QRectF source, int offset) {
            Note note; note.id = id; note.isPoint = false;
            note.rect = QRect((source.topLeft() + QPointF(40 + offset, 110)).toPoint(), QSize(10, 10));
            note.comment = id; note.parent = ParentLink{source};
            return note;
        };
        Note standalone; standalone.id = "standalone"; standalone.point = {10, 10};
        standalone.comment = "Keep my relative position";
        auto pending = child("pending", secondSource, 40);
        pending.parent->state = ParentState::Unknown;
        document.notes = {parent("first", firstSource), standalone, child("second-a", secondSource, 0),
                          parent("second", secondSource), child("first-a", firstSource, 0), pending,
                          child("second-b", secondSource, 20), child("first-b", firstSource, 20)};
        auto expected = QVector<Note>{document.notes[0], document.notes[4], document.notes[7],
                                      document.notes[1], document.notes[3], document.notes[2],
                                      document.notes[6], document.notes[5]};
        for (int iteration = 0; iteration < 5; ++iteration) {
            groupChildNotes(document.notes);
            QCOMPARE(document.notes, expected);
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("ownership.edithere");
        saveBytes(path, serializeDocument(document, true));
        const auto restored = loadDocument(path);
        QCOMPARE(restored.notes, expected);
        QVERIFY(restored.layout == document.layout);
        QVERIFY(restored.notes.last().parent);
        QVERIFY(restored.notes.last().parent->state == ParentState::Unknown);
        QVERIFY_EXCEPTION_THROWN(serializeFeedback(restored), std::runtime_error);
        auto confirmed = restored;
        confirmed.notes.last().parent->state = ParentState::Child;
        normalizeNoteParents(confirmed);
        const auto feedback = exportFeedback(confirmed, true);
        QCOMPARE(feedback["objects"].toArray().size(), 3); // two components and the standalone point
        QCOMPARE(feedback["objects"].toArray()[0].toObject()["children"].toArray().size(), 2);
        QCOMPARE(feedback["objects"].toArray()[2].toObject()["children"].toArray().size(), 3);
    }
    void emptyParentCardsDoNotChangeFeedbackAndProjectionRespectsTheLimit() {
        auto document = fromImage(exampleImage(), "demo", "empty parent card");
        document.layout = createLayout(document.image.size(), {});
        const auto group = addLayoutRegion(*document.layout, {100, 100, 200, 100});
        transformLayoutGroup(*document.layout, group, {200, 140, 200, 100});
        Note parent;
        parent.isPoint = false; parent.rect = {200, 140, 200, 100};
        parent.movementSource = QRectF(100, 100, 200, 100);
        Note first;
        first.isPoint = false; first.rect = {220, 150, 20, 20}; first.comment = "First";
        first.parent = ParentLink{*parent.movementSource};
        Note second = first;
        second.id = uniqueId(); second.rect.translate(30, 0); second.comment = "Second";
        document.notes = {parent, first, second};
        const auto expected = serializeFeedback(document);
        document.notes.removeFirst();
        QCOMPARE(serializeFeedback(document), expected);
        const auto before = document.notes;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("children.edithere");
        saveBytes(path, serializeDocument(document, true));
        const auto restored = loadDocument(path);
        QCOMPARE(serializeFeedback(restored), expected);
        QCOMPARE(document.notes, before);
        while (document.notes.size() < MaxNotes) {
            Note note; note.point = {10, 10}; note.comment = "Unrelated";
            document.notes.append(note);
        }
        const auto full = document.notes;
        QVERIFY_EXCEPTION_THROWN(serializeFeedback(document), std::runtime_error);
        QCOMPARE(document.notes, full);
        QVERIFY_THROWS_NO_EXCEPTION(serializeDocument(document, true));
    }
    void unresolvedParentsBlockEveryFrameButRemainEditable() {
        auto document = fromImage(exampleImage(), "demo", "unresolved parent");
        document.layout = createLayout(document.image.size(), {});
        const auto group = addLayoutRegion(*document.layout, {100, 100, 200, 100});
        transformLayoutGroup(*document.layout, group, {200, 140, 200, 100});
        Note child;
        child.isPoint = false; child.rect = {380, 150, 60, 40}; child.comment = "Unresolved";
        child.parent = ParentLink{QRectF(100, 100, 200, 100), ParentState::Unknown};
        document.notes = {child};
        QVERIFY_EXCEPTION_THROWN(serializeFeedback(document), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(buildFeedbackBundle(document), std::runtime_error);
        QVERIFY_THROWS_NO_EXCEPTION(serializeDocument(document, true));
        VideoProject project;
        project.source = "missing.mp4"; project.durationMs = 10000;
        auto other = fromImage(exampleImage(), "demo", "other frame");
        Note point; point.point = {10, 10}; point.comment = "Other frame"; other.notes = {point};
        project.frames = {{1000, other}, {2000, document}};
        QVERIFY_EXCEPTION_THROWN(buildVideoFeedbackBundle(project), std::runtime_error);
        QVERIFY_THROWS_NO_EXCEPTION(serializeVideoProject(project));
        project.frames[1].document.notes[0].parent->state = ParentState::Child;
        FeedbackExportOptions options; options.mode = ImageExportMode::Embedded;
        const auto feedback = buildVideoFeedbackBundle(project, options);
        const auto root = QJsonDocument::fromJson(feedback.json).object();
        QVERIFY(root.contains("exportedAt"));
        QCOMPARE(root["objects"].toArray().size(), 3); // point, movement, child
        for (const auto &value : root["objects"].toArray()) {
            QVERIFY(!value.toObject().contains("children"));
            QVERIFY(value.toObject().contains("frameId"));
        }
        QTemporaryDir directory;
        const auto path = directory.filePath("video.json");
        saveBytes(path, feedback.json);
        const auto restored = loadVideoProject(path);
        QCOMPARE(videoAnnotationCount(restored), 3);
        const auto reexported = QJsonDocument::fromJson(buildVideoFeedbackBundle(restored, options).json).object();
        QCOMPARE(reexported["objects"], root["objects"]);
    }
    void previousFeedbackVersionsRejectChildAndInternalFields() {
        auto document = fromImage(exampleImage(), "demo", "version boundary");
        Note note; note.isPoint = false; note.rect = {10, 10, 50, 50}; note.comment = "Region";
        document.notes = {note};
        const auto feedback = exportFeedback(document, true);
        for (const auto &version : {QString("feedback-minimal-3"), QString(FeedbackSchemaVersion)}) {
            for (const auto &field : {QString("parent"), QString("moved"), QString("children")}) {
                auto forged = feedback;
                forged["schemaVersion"] = version;
                forged["exportedAt"] = timestamp();
                auto object = forged["objects"].toArray()[0].toObject();
                if (field == "children") {
                    if (version == FeedbackSchemaVersion) continue;
                    object[field] = QJsonArray{object};
                } else object[field] = field == "moved" ? QJsonValue(true) : QJsonValue(object["source"]);
                forged["objects"] = QJsonArray{object};
                QVERIFY_EXCEPTION_THROWN(loadFeedback(forged, {}), std::runtime_error);
            }
        }
        auto previous = feedback;
        previous["schemaVersion"] = "feedback-minimal-3";
        QVERIFY_EXCEPTION_THROWN(loadFeedback(previous, {}), std::runtime_error);
        previous["exportedAt"] = timestamp();
        QVERIFY_THROWS_NO_EXCEPTION(loadFeedback(previous, {}));
    }
    void feedbackV3TypesAndMovementResultsRoundTrip() {
        const auto image = exampleImage();
        const auto source = QJsonObject{{"x1", 100}, {"y1", 100}, {"x2", 300}, {"y2", 200}};
        const auto target = QJsonObject{{"x1", 200}, {"y1", 140}, {"x2", 400}, {"y2", 240}};
        const auto png = encodePng(image);
        QJsonObject feedback{
            {"$schema", FeedbackSchemaId}, {"schemaVersion", FeedbackSchemaVersion},
            {"tool", QJsonObject{{"name", "EditHere"}, {"version", EDITHERE_VERSION}}},
            {"exportedAt", "2026-10-17T10:00:00+08:00"}, {"annotationSpace", "result"},
            {"image", "data:image/png;base64," + QString::fromLatin1(png.toBase64())},
            {"objects", QJsonArray{
                QJsonObject{{"id", "region"}, {"kind", "region"}, {"source", source},
                            {"movements", QJsonArray{QJsonObject{{"to", target}}}},
                            {"result", QJsonObject{{"rect", target}, {"dx", 100}, {"dy", 40}, {"scaleX", 1}, {"scaleY", 1}}},
                            {"annotations", QJsonArray{QJsonObject{{"number", 3}, {"text", "移动区域"}}}}},
                QJsonObject{{"id", "point"}, {"kind", "point"}, {"point", QJsonObject{{"x", 240}, {"y", 180}}},
                            {"movements", QJsonArray{}}, {"annotations", QJsonArray{QJsonObject{{"number", 1}, {"text", "点意见"}}}}},
                QJsonObject{{"id", "global"}, {"kind", "global"}, {"movements", QJsonArray{}},
                            {"annotations", QJsonArray{QJsonObject{{"number", 2}, {"text", "全局意见"}}}}},
            }},
        };
        const auto restored = loadFeedback(feedback, {});
        QCOMPARE(restored.notes.size(), 3);
        const auto exported = exportFeedback(restored, true);
        QCOMPARE(exported["schemaVersion"].toString(), QString(FeedbackSchemaVersion));
        const auto objects = exported["objects"].toArray();
        QCOMPARE(objects.size(), 3);
        QJsonObject byKind;
        for (const auto &value : objects) byKind.insert(value.toObject()["kind"].toString(), value);
        QCOMPARE(byKind["point"].toObject()["point"].toObject()["x"].toInt(), 240);
        QCOMPARE(byKind["point"].toObject()["id"].toString(), QString("point"));
        QCOMPARE(byKind["region"].toObject()["id"].toString(), QString("region"));
        QCOMPARE(byKind["global"].toObject()["id"].toString(), QString("global"));
        QCOMPARE(byKind["global"].toObject().contains("source"), false);
        const auto region = byKind["region"].toObject();
        QCOMPARE(region["movement"].toObject()["rect"].toObject(), target);
        QCOMPARE(region["movement"].toObject()["dx"].toDouble(), 100.0);
        QVERIFY(!region["movement"].toObject().contains("scaleX"));
        QVERIFY(!region["movement"].toObject().contains("scaleY"));
        auto forged = feedback; auto forgedObjects = forged["objects"].toArray();
        auto forgedRegion = forgedObjects[0].toObject(); auto result = forgedRegion["result"].toObject(); result["dx"] = 99; forgedRegion["result"] = result; forgedObjects[0] = forgedRegion; forged["objects"] = forgedObjects;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(forged, {}), std::runtime_error);
    }
    void feedbackV3KeepsMultiAnnotationObjectsAndAllocatesUniqueIds() {
        auto doc = fromImage(exampleImage(), "demo", "typed objects");
        Note point;
        point.point = {20, 20}; point.comment = "point";
        Note region;
        region.isPoint = false; region.rect = {30, 30, 80, 40}; region.comment = "region";
        Note global;
        global.isGlobal = true; global.comment = "global";
        doc.notes = {point, region, global};
        auto feedback = exportFeedback(doc, true);
        auto objects = feedback["objects"].toArray();
        QVERIFY(!feedback.contains("$schema"));
        for (const auto &value : objects) QVERIFY(!value.toObject().contains("movements"));
        auto older = feedback;
        older.insert("$schema", FeedbackSchemaId);
        QJsonArray olderObjects;
        for (const auto &value : objects) {
            auto object = value.toObject(); object.insert("movements", QJsonArray{}); olderObjects.append(object);
        }
        older.insert("objects", olderObjects);
        QCOMPARE(stableFeedback(exportFeedback(loadFeedback(older, {}), true)), stableFeedback(feedback));
        auto invalid = feedback; auto invalidObjects = objects;
        auto invalidObject = invalidObjects[0].toObject(); invalidObject.insert("movements", QJsonValue::Null);
        invalidObjects[0] = invalidObject; invalid.insert("objects", invalidObjects);
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, {}), std::runtime_error);
        for (qsizetype i = 0; i < objects.size(); ++i) {
            auto object = objects[i].toObject();
            auto annotations = object["annotations"].toArray();
            annotations.append(QJsonObject{{"number", int(i) + 4}, {"text", "another annotation"}});
            object["annotations"] = annotations;
            objects[i] = object;
        }
        feedback["objects"] = objects;
        auto restored = loadFeedback(feedback, {});
        QCOMPARE(restored.notes.size(), 6);
        restored.notes.prepend(point); // Fresh notes must not reuse an imported object's ID.
        const auto exported = exportFeedback(restored, true);
        const auto exportedObjects = exported["objects"].toArray();
        QCOMPARE(exportedObjects.size(), 4);
        for (qsizetype i = 1; i < exportedObjects.size(); ++i)
            QCOMPARE(exportedObjects[i].toObject()["annotations"].toArray().size(), 2);
        QVERIFY_THROWS_NO_EXCEPTION(loadFeedback(exported, {}));
        restored.notes.last().isGlobal = false;
        restored.notes.last().point = {60, 60}; // Splitting a target also needs a fresh ID.
        const auto split = exportFeedback(restored, true);
        QCOMPARE(split["objects"].toArray().size(), 5);
        QVERIFY_THROWS_NO_EXCEPTION(loadFeedback(split, {}));
    }
    void feedbackV4NestsChildAnnotationsUnderTheirMovedComponent() {
        const auto image = exampleImage();
        auto layout = createLayout(image.size(), {});
        const QString groupId = addLayoutRegion(layout, QRectF(100, 100, 200, 100));
        QVERIFY(!groupId.isEmpty());
        transformLayoutGroup(layout, groupId, QRectF(200, 140, 200, 100));
        QRectF groupSource;
        for (const auto &group : layout.groups)
            if (group.id == groupId) groupSource = group.originalBounds;
        QCOMPARE(groupSource.toRect(), QRect(100, 100, 200, 100));
        const QRectF groupTarget(200, 140, 200, 100);

        // A box entirely inside the moved component is unambiguous: it becomes a
        // child without asking, whether or not the explosion view is open.
        auto classified = classifyParent(QRect(220, 150, 60, 40), layout);
        QVERIFY(classified);
        QVERIFY(classified->state == ParentState::Child);
        QCOMPARE(classified->source.toRect(), QRect(100, 100, 200, 100));
        // A partial overlap stays undetermined until the user answers.
        QVERIFY(classifyParent(QRect(380, 150, 60, 40), layout)->state == ParentState::Unknown);
        QVERIFY(!classifyParent(QRect(10, 10, 20, 20), layout));

        auto doc = fromImage(image, "demo", "children");
        doc.layout = layout;
        Note moved;
        moved.isPoint = false; moved.movementSource = groupSource; moved.rect = groupTarget.toRect(); moved.comment = "整体右移";
        Note child;
        child.isPoint = false; child.rect = QRect(220, 150, 60, 40); child.comment = "按钮要更圆";
        child.parent = ParentLink{groupSource, ParentState::Child};
        doc.notes = {moved, child};
        const auto feedback = exportFeedback(doc, true);
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 1);
        const auto parent = objects[0].toObject();
        QCOMPARE(parent["kind"].toString(), QString("region"));
        QCOMPARE(parent["children"].toArray().size(), 1);
        const auto nested = parent["children"].toArray()[0].toObject();
        QCOMPARE(nested["kind"].toString(), QString("region"));
        const auto expectedChildSource = QJsonObject{{"x1", 220}, {"y1", 150}, {"x2", 280}, {"y2", 190}};
        QCOMPARE(nested["source"].toObject(), expectedChildSource);
        QCOMPARE(nested["annotations"].toArray()[0].toObject()["text"].toString(), QString("按钮要更圆"));
        QVERIFY(!nested["id"].toString().isEmpty());
        QVERIFY(nested["id"].toString() != parent["id"].toString());
        // The nested array reads as the tail of the parent object, after its own
        // id, source, movement and annotations.
        const auto serialized = QString::fromUtf8(formatFeedbackJson(feedback, true));
        QVERIFY(serialized.indexOf("\"children\"") > serialized.indexOf("\"movement\""));
        QVERIFY(serialized.indexOf("\"children\"") > serialized.indexOf("\"annotations\""));

        // Reading the file back restores the ownership instead of duplicating it.
        const auto restored = loadFeedback(feedback, {});
        QCOMPARE(restored.notes.size(), 2);
        bool foundChild = false;
        for (const auto &note : restored.notes) {
            if (note.comment != "按钮要更圆") continue;
            foundChild = true;
            QVERIFY(note.parent);
            QVERIFY(note.parent->state == ParentState::Child);
            QCOMPARE(note.parent->source.toRect(), QRect(100, 100, 200, 100));
            QCOMPARE(note.rect, QRect(220, 150, 60, 40));
        }
        QVERIFY(foundChild);
        QCOMPARE(stableFeedback(exportFeedback(restored, true)), stableFeedback(feedback));
        // Child notes line up directly below their component even when they were
        // created after unrelated notes, so list order and JSON order agree.
        auto interleaved = restored;
        Note noise;
        noise.point = {40, 40};
        noise.comment = "无关意见";
        interleaved.notes = {moved, noise, child};
        groupChildNotes(interleaved.notes);
        QCOMPARE(interleaved.notes.size(), 3);
        QCOMPARE(interleaved.notes[0].comment, QString("整体右移"));
        QCOMPARE(interleaved.notes[1].comment, QString("按钮要更圆"));
        QCOMPARE(interleaved.notes[2].comment, QString("无关意见"));
        // Layout bounds can carry sub-pixel fractions while the stored parent
        // rectangle is rounded; the ownership still has to line up.
        auto fractional = restored;
        Note subpixel;
        subpixel.isPoint = false;
        subpixel.movementSource = QRectF(100.4, 100.4, 200, 100);
        subpixel.rect = groupTarget.toRect();
        subpixel.comment = "整体右移";
        fractional.notes = {subpixel, noise, child};
        groupChildNotes(fractional.notes);
        QCOMPARE(fractional.notes.size(), 3);
        QCOMPARE(fractional.notes[1].comment, QString("按钮要更圆"));

        // Only a boxed region can own children, and children stay regions.
        const auto source = nested["source"];
        const auto globalOnly = QJsonObject{{"kind", "global"},
            {"annotations", QJsonArray{QJsonObject{{"number", 1}, {"text", "全局"}}}}};
        QVector<QJsonObject> forgedObjects{
            QJsonObject{{"kind", "global"}, {"annotations", globalOnly["annotations"]},
                        {"children", QJsonArray{nested}}},
            QJsonObject{{"kind", "region"}, {"source", source}, {"annotations", QJsonArray{}},
                        {"children", QJsonArray{globalOnly}}},
            QJsonObject{{"kind", "region"}, {"source", source}, {"annotations", QJsonArray{}},
                        {"children", QJsonArray{}}},
        };
        for (const auto &forgedObject : forgedObjects) {
            auto forged = feedback;
            forged["objects"] = QJsonArray{forgedObject};
            QVERIFY_EXCEPTION_THROWN(loadFeedback(forged, {}), std::runtime_error);
        }
    }
    void deletingAMovementReleasesItsChildAnnotations() {
        const auto image = exampleImage();
        auto layout = createLayout(image.size(), {});
        const QString groupId = addLayoutRegion(layout, QRectF(100, 100, 200, 100));
        transformLayoutGroup(layout, groupId, QRectF(200, 140, 200, 100));
        QRectF groupSource;
        for (const auto &group : layout.groups)
            if (group.id == groupId) groupSource = group.originalBounds;
        auto doc = fromImage(image, "demo", "delete parent");
        doc.layout = layout;
        Note moved;
        moved.id = "moved";
        moved.isPoint = false;
        moved.movementSource = groupSource;
        moved.rect = QRect(200, 140, 200, 100);
        moved.comment = "整体右移";
        Note child;
        child.id = "child";
        child.isPoint = false;
        child.rect = QRect(220, 150, 60, 40);
        child.comment = "按钮要更圆";
        child.parent = ParentLink{groupSource, ParentState::Child};
        doc.notes = {moved, child};
        QCOMPARE(exportFeedback(doc, true)["objects"].toArray()[0].toObject()["children"].toArray().size(), 1);

        removeNote(doc, "moved");
        // Reverting the component sends the annotation home and drops the ownership,
        // so neither the sidebar nor the export may still claim a parent.
        QCOMPARE(doc.notes.size(), 1);
        QVERIFY(!doc.notes[0].parent);
        QCOMPARE(doc.notes[0].rect, QRect(120, 110, 60, 40));
        const auto objects = exportFeedback(doc, true)["objects"].toArray();
        QCOMPARE(objects.size(), 1);
        QVERIFY(!objects[0].toObject().contains("children"));
    }
    void captureContextAndAgentPageRoundTripWithoutGuessingScreenshotIdentity() {
        auto doc = fromImage(exampleImage(), "screen", "Screenshot");
        doc.capture.insert("windowTitle", "Game - user document");
        doc.capture.insert("appName", "Game.exe"); doc.capture.insert("page", "游戏主页面"); doc.capture.insert("pageSource", "agent");
        const auto feedback = exportFeedback(doc, true);
        QCOMPARE(feedback["capture"].toObject(), doc.capture);
        const auto restored = loadFeedback(feedback, {}); QCOMPARE(restored.capture, doc.capture);
        QCOMPARE(restored.source, doc.source); QCOMPARE(restored.createdAt, doc.createdAt);
        FeedbackExportOptions privateOptions; privateOptions.mode = ImageExportMode::Embedded; privateOptions.includeWindowTitle = false;
        const auto privateFeedback = exportFeedback(doc, privateOptions);
        auto expected = doc.capture; expected.remove("windowTitle");
        QCOMPARE(privateFeedback["capture"].toObject(), expected);
        QCOMPARE(privateFeedback["capture"].toObject()["appName"].toString(), QString("Game.exe"));
        QCOMPARE(doc.capture["windowTitle"].toString(), QString("Game - user document")); // Export filtering does not erase editable data.
        QTemporaryDir directory;
        const auto path = directory.filePath("capture.edithere"); saveBytes(path, serializeDocument(doc, true));
        QCOMPARE(loadDocument(path).capture, doc.capture);
        const auto project = QJsonDocument::fromJson(serializeDocument(doc, true)).object();
        QCOMPARE(project["capture"].toObject()["createdAt"], feedback["capture"].toObject()["capturedAt"]);
        QVERIFY(!project["capture"].toObject().contains("capturedAt"));
        auto old = feedback; old.remove("capture");
        const auto importedOld = loadFeedback(old, {}); QVERIFY(importedOld.capture.isEmpty());
        QVERIFY(!exportFeedback(importedOld).contains("capture"));
        auto screenshot = fromImage(exampleImage(), "file", "main-screen.png");
        QCOMPARE(screenshot.capture["fileName"].toString(), QString("main-screen.png"));
        QVERIFY(!screenshot.capture.contains("page")); // Filenames never become guessed page identities.
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            saveBytes(QDir(folder).filePath("feedback-capture.json"), serializeFeedback(doc, true));
            saveBytes(QDir(folder).filePath("feedback-capture-private.json"), QJsonDocument(privateFeedback).toJson());
            saveBytes(QDir(folder).filePath("project-capture.json"), serializeDocument(doc, true));
        }
    }
    void captureContextRejectsInvalidMetadataAndPairedPageFields() {
        const auto doc = fromImage(exampleImage(), "screen", "Capture validation");
        const QJsonObject valid{{"source", "screen"}, {"capturedAt", doc.createdAt}, {"page", "游戏主页面"}, {"pageSource", "agent"}};
        QVector<QJsonObject> invalid;
        for (const auto &field : {QString("page"), QString("pageSource"), QString("source"), QString("capturedAt")}) {
            auto missing = valid; missing.remove(field); invalid.append(missing);
        }
        for (const auto &entry : QVector<QPair<QString, QJsonValue>>{
                 {"source", "unknown"}, {"capturedAt", "2026-02-30T00:00:00Z"}, {"capturedAt", "2026-10-17T00:00:00"},
                 {"fileName", "../private.png"}, {"fileName", "C:\\private.png"}, {"fileName", ".."},
                 {"windowTitle", 1}, {"appName", QString(1001, 'a')}, {"page", " \t\n"},
                 {"page", QString(501, 'p')}, {"pageSource", "guessed"}, {"unknown", true}}) {
            auto broken = valid; broken[entry.first] = entry.second; invalid.append(broken);
        }
        auto nul = valid; nul["page"] = QString("Page") + QChar::Null; invalid.append(nul);
        for (const auto &capture : invalid) {
            QVERIFY_EXCEPTION_THROWN(validateCaptureContext(capture), std::runtime_error);
            auto feedback = exportFeedback(doc, true); feedback["capture"] = capture;
            QVERIFY_EXCEPTION_THROWN(loadFeedback(feedback, {}), std::runtime_error);
        }
        for (const auto &source : {"screen", "file", "clipboard", "demo", "scroll", "history", "pin", "drop", "video"}) {
            auto capture = valid; capture["source"] = source; QVERIFY_THROWS_NO_EXCEPTION(validateCaptureContext(capture));
        }
        auto bounds = valid; bounds["page"] = QString(500, 'p'); bounds["windowTitle"] = QString(1000, 'w'); bounds["appName"] = QString(1000, 'a');
        QVERIFY_THROWS_NO_EXCEPTION(validateCaptureContext(bounds));
    }
    void imageBundleModesUseRealBytesAndPreserveCoordinates() {
        auto doc = fromImage(exampleImage(), "file", "Image bundle");
        Note note; note.point = {40, 50}; note.comment = "Change the heading"; doc.notes = {note};
        QTemporaryDir directory;
        const auto path = directory.filePath("feedback.json");
        for (const auto mode : {ImageExportMode::File, ImageExportMode::Embedded, ImageExportMode::None}) {
            FeedbackExportOptions options; options.mode = mode; options.imageFile = "feedback-file.png";
            const auto bundle = buildFeedbackBundle(doc, options);
            const auto root = QJsonDocument::fromJson(bundle.json).object();
            QVERIFY(!root.contains("exportedAt"));
            QVERIFY(bundle.json.contains("\"width\":" + QByteArray::number(doc.image.width()) + ",\"height\":"));
            const auto readable = formatFeedbackJson(root, true);
            QVERIFY(readable.contains("\"width\": " + QByteArray::number(doc.image.width()) + ",\n        \"height\": "));
            QCOMPARE(QJsonDocument::fromJson(readable).object(), root);
            const auto image = root["image"].toObject();
            QCOMPARE(image["width"].toInt(), doc.image.width()); QCOMPARE(image["height"].toInt(), doc.image.height());
            QCOMPARE(image["mimeType"].toString(), QString("image/png")); QVERIFY(image["devicePixelRatio"].isNull());
            QCOMPARE(image["sha256"].toString(), QString::fromLatin1(QCryptographicHash::hash(doc.png, QCryptographicHash::Sha256).toHex()));
            if (mode == ImageExportMode::File) {
                QCOMPARE(image["mode"].toString(), QString("file"));
                QCOMPARE(bundle.files.value("feedback-file.png"), doc.png);
                QVERIFY(!image.contains("dataUrl"));
                saveBytes(directory.filePath("feedback-file.png"), bundle.files.value("feedback-file.png"));
                saveBytes(path, bundle.json);
                QCOMPARE(loadDocument(path).notes[0].point, note.point);
                // Same dimensions with different pixels must fail by hash.
                auto different = doc.image; different.setPixelColor(0, 0, Qt::magenta);
                saveBytes(directory.filePath("feedback-file.png"), encodePng(different));
                QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
                saveBytes(directory.filePath("feedback-file.png"), doc.png);
            } else if (mode == ImageExportMode::Embedded) {
                QCOMPARE(image["mode"].toString(), QString("embedded")); QVERIFY(bundle.files.isEmpty());
                QVERIFY(!image.contains("file"));
                QCOMPARE(QByteArray::fromBase64(image["dataUrl"].toString().section(',', 1).toLatin1()), doc.png);
                QCOMPARE(loadFeedback(root, {}).notes[0].point, note.point);
            } else {
                QCOMPARE(image["mode"].toString(), QString("none")); QVERIFY(bundle.files.isEmpty());
                QVERIFY(!image.contains("file") && !image.contains("dataUrl"));
                QVERIFY_EXCEPTION_THROWN(loadFeedback(root, {}), std::runtime_error);
                QCOMPARE(loadFeedback(root, doc.image, doc.png).notes[0].point, note.point);
            }
            const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
            if (!folder.isEmpty()) {
                QVERIFY(QDir().mkpath(folder));
                const QString name = mode == ImageExportMode::File ? "feedback-file.json" : mode == ImageExportMode::Embedded ? "feedback-embedded.json" : "feedback-none.json";
                saveBytes(QDir(folder).filePath(name), bundle.json);
                for (auto it = bundle.files.cbegin(); it != bundle.files.cend(); ++it) saveBytes(QDir(folder).filePath(it.key()), it.value());
            }
        }
        FeedbackExportOptions preview; preview.mode = ImageExportMode::None; preview.preview = true;
        const auto bundle = buildFeedbackBundle(doc, preview);
        QVERIFY(bundle.imageFile.isEmpty()); QCOMPARE(bundle.previewFile, QString("feedback.annotated.jpg"));
        QCOMPARE(bundle.files.size(), 1);
        const auto rendered = QImage::fromData(bundle.files.value(bundle.previewFile), "JPEG");
        QVERIFY(!rendered.isNull()); QVERIFY(rendered.width() >= doc.image.width());
        QVERIFY(!bundle.json.contains("data:image/"));
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            saveBytes(QDir(folder).filePath("feedback-preview.json"), bundle.json);
            saveBytes(QDir(folder).filePath(bundle.previewFile), bundle.files.value(bundle.previewFile));
        }
    }
    void imageBundleRejectsMetadataTamperingAndUnsafeNames() {
        const auto doc = fromImage(exampleImage(), "file", "Image metadata");
        auto root = exportFeedback(doc, true);
        auto image = root["image"].toObject();
        for (const auto &field : {QString("sha256"), QString("width"), QString("mimeType")}) {
            auto broken = image;
            if (field == "sha256") broken[field] = QString(64, '0');
            else if (field == "width") broken[field] = doc.image.width() + 1;
            else broken[field] = "image/jpeg";
            auto changed = root; changed["image"] = broken;
            QVERIFY_EXCEPTION_THROWN(loadFeedback(changed, {}), std::runtime_error);
        }
        for (const auto &name : {QString("../private.png"), QString("C:/private.png"), QString("nul.png"), QString(".hidden.png")}) {
            FeedbackExportOptions options; options.imageFile = name;
            QVERIFY_EXCEPTION_THROWN(buildFeedbackBundle(doc, options), std::runtime_error);
        }
        FeedbackExportOptions options; options.preview = true; options.previewFile = "../private.annotated.jpg";
        QVERIFY_EXCEPTION_THROWN(buildFeedbackBundle(doc, options), std::runtime_error);
        auto legacy = root;
        legacy["image"] = "data:image/png;base64," + QString::fromLatin1(doc.png.toBase64());
        QCOMPARE(loadFeedback(legacy, {}).image, doc.image);
        legacy.remove("image");
        QCOMPARE(loadFeedback(legacy, doc.image).image, doc.image);

        // A promised preview is part of the bundle, even when the original is
        // embedded. Missing or mislabeled previews cannot silently disappear.
        QTemporaryDir directory;
        FeedbackExportOptions withPreview; withPreview.mode = ImageExportMode::Embedded; withPreview.preview = true;
        const auto bundle = buildFeedbackBundle(doc, withPreview);
        const auto path = directory.filePath("preview.json"); saveBytes(path, bundle.json);
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        saveBytes(directory.filePath(bundle.previewFile), doc.png);
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        saveBytes(directory.filePath(bundle.previewFile), bundle.files.value(bundle.previewFile));
        QCOMPARE(loadDocument(path).image, doc.image);
    }
    void recognizedScreenshotTextRoundTripsAndCanBeOmitted() {
        auto doc = fromImage(exampleImage(), "file", "OCR metadata");
        Note note; note.isPoint = false; note.rect = {20, 20, 300, 80}; note.comment = "Shorten this label";
        note.recognizedText = {{"text", "Export JSON"}, {"engine", "windows-ocr"}, {"language", "en-US"}, {"confidence", 0.8}};
        doc.notes = {note};
        const auto feedback = exportFeedback(doc, true);
        const auto object = feedback["objects"].toArray()[0].toObject();
        QCOMPARE(object["ocrtext"].toString(), QString("Export JSON"));
        QVERIFY(!object.contains("recognizedText"));
        QCOMPARE(loadFeedback(feedback, {}).notes[0].recognizedText["text"].toString(), QString("Export JSON"));
        QVERIFY(!exportFeedback(doc, false, false, false)["objects"].toArray()[0].toObject().contains("ocrtext"));
        auto older = feedback; auto olderObject = object;
        olderObject.remove("ocrtext"); olderObject.insert("recognizedText", note.recognizedText);
        older.insert("objects", QJsonArray{olderObject});
        QCOMPARE(loadFeedback(older, {}).notes[0].recognizedText, note.recognizedText);
        QCOMPARE(stableFeedback(exportFeedback(loadFeedback(older, {}), true)), stableFeedback(feedback));
        for (const auto &text : {QJsonValue(""), QJsonValue(" \n\t"), QJsonValue(QString(501, 'x')),
                                QJsonValue(7), QJsonValue(QJsonValue::Null), QJsonValue(QJsonObject{})}) {
            auto invalid = feedback; auto target = object; target.insert("ocrtext", text);
            invalid.insert("objects", QJsonArray{target});
            QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, {}), std::runtime_error);
        }
        QTemporaryDir directory; const auto path = directory.filePath("ocr.edithere");
        saveBytes(path, serializeDocument(doc, true));
        QCOMPARE(loadDocument(path).notes[0].recognizedText, note.recognizedText);
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            saveBytes(QDir(folder).filePath("feedback-ocr.json"), serializeFeedback(doc, true));
            saveBytes(QDir(folder).filePath("feedback-ocr-disabled.json"), serializeFeedback(doc, true, false, false));
            saveBytes(QDir(folder).filePath("project-ocr.json"), serializeDocument(doc, true));
        }
        doc.notes[0].recognizedText = {{"text", ""}, {"edited", true}};
        saveBytes(path, serializeDocument(doc, true));
        QCOMPARE(loadDocument(path).notes[0].recognizedText, doc.notes[0].recognizedText);
        QVERIFY(!exportFeedback(doc)["objects"].toArray()[0].toObject().contains("ocrtext"));
        if (!folder.isEmpty()) saveBytes(QDir(folder).filePath("project-ocr-deleted.json"), serializeDocument(doc, true));
        auto legacy = feedback; legacy.remove("$schema"); legacy.remove("schemaVersion"); legacy.remove("tool"); legacy.remove("exportedAt");
        QVERIFY_EXCEPTION_THROWN(loadFeedback(legacy, {}), std::runtime_error);
        auto malformed = feedback; auto objects = malformed["objects"].toArray(); auto ambiguous = objects[0].toObject();
        ambiguous["recognizedText"] = note.recognizedText; objects[0] = ambiguous; malformed["objects"] = objects;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(malformed, {}), std::runtime_error);
        auto global = feedback; auto globalObject = object;
        globalObject.remove("source"); globalObject.insert("kind", "global"); global.insert("objects", QJsonArray{globalObject});
        QVERIFY_EXCEPTION_THROWN(loadFeedback(global, {}), std::runtime_error);
        doc.notes[0].isGlobal = true;
        QVERIFY_EXCEPTION_THROWN(validateDocument(doc), std::runtime_error);
    }
    void recognitionUsesOriginalMovementAndVisibleRegionPixels() {
        QImage image(500, 300, QImage::Format_ARGB32); image.fill(Qt::white);
        QPainter painter(&image); painter.fillRect(QRect(20, 30, 100, 60), Qt::red); painter.end();
        auto doc = fromImage(image, "file", "OCR crop");
        doc.candidates = {{{20, 30, 100, 60}, manualTarget()}};
        doc.layout = createLayout(image.size(), doc.candidates);
        QString group;
        for (const auto &item : doc.layout->groups) if (item.originalBounds == QRectF(20, 30, 100, 60)) group = item.id;
        QVERIFY(!group.isEmpty()); transformLayoutGroup(*doc.layout, group, {220, 30, 100, 60});
        Note movement; movement.isPoint = false; movement.movementSource = QRectF(20, 30, 100, 60); movement.rect = {220, 30, 100, 60};
        const auto original = noteRecognitionImage(doc, movement);
        QCOMPARE(original.size(), QSize(100, 60)); QCOMPARE(original.pixelColor(40, 20), QColor(Qt::red));
        Note region; region.isPoint = false; region.rect = {220, 30, 100, 60};
        QCOMPARE(noteRecognitionImage(doc, region).convertToFormat(QImage::Format_ARGB32), original);
        Note point; point.point = {250, 50};
        QCOMPARE(noteRecognitionImage(doc, point).convertToFormat(QImage::Format_ARGB32), original);
        point.point = {490, 290};
        const auto nearby = noteRecognitionImage(doc, point); QVERIFY(!nearby.isNull()); QVERIFY(nearby.width() <= 240 && nearby.height() <= 80);
        point.isGlobal = true; QVERIFY(noteRecognitionImage(doc, point).isNull());
        // Cropping directly must match the displayed layout, including scaled
        // pieces whose edges fall between pixels.
        transformLayoutGroup(*doc.layout, group, {219.5, 27.25, 137.5, 81.75});
        region.rect = {210, 20, 155, 100};
        QCOMPARE(noteRecognitionImage(doc, region), renderLayout(doc.image, *doc.layout).copy(region.rect));
    }
    void feedbackMetadataAndUnversionedObjectsRoundTrip() {
        auto doc = fromImage(exampleImage(), "file", "metadata");
        Note note;
        note.point = {20, 30};
        note.comment = "Keep the exported contract self describing";
        doc.notes.append(note);
        const auto feedback = exportFeedback(doc, true);
        QCOMPARE(feedback["schemaVersion"].toString(), QString(FeedbackSchemaVersion));
        QVERIFY(!feedback.contains("$schema"));
        QCOMPARE(feedback["tool"].toObject(),
                 (QJsonObject{{"name", "EditHere"}, {"version", EDITHERE_VERSION}}));
        QVERIFY(!feedback.contains("exportedAt"));
        QTemporaryDir directory;
        const auto path = directory.filePath("feedback.json");
        saveBytes(path, QJsonDocument(feedback).toJson());
        Document imported;
        QVERIFY_THROWS_NO_EXCEPTION(imported = loadDocument(path));
        QCOMPARE(imported.image, doc.image);
        QCOMPARE(imported.notes[0].comment, note.comment);

        auto external = feedback;
        external.remove("image");
        saveBytes(directory.filePath("feedback.png"), doc.png);
        saveBytes(path, QJsonDocument(external).toJson());
        QVERIFY_THROWS_NO_EXCEPTION(imported = loadDocument(path));
        QCOMPARE(imported.notes[0].point, note.point);

        auto legacy = feedback;
        legacy.remove("capture");
        legacy["image"] = feedback["image"].toObject()["dataUrl"];
        legacy = unversionedLegacyFeedback(legacy);
        saveBytes(path, QJsonDocument(legacy).toJson());
        QVERIFY_THROWS_NO_EXCEPTION(imported = loadDocument(path));
        QCOMPARE(imported.notes[0].comment, note.comment);
        QVERIFY_THROWS_NO_EXCEPTION(imported = loadFeedback(legacy, {}));
        QCOMPARE(imported.notes[0].point, note.point);

        auto offset = feedback;
        offset["exportedAt"] = "2026-10-16T13:14:15.123+08:00";
        QVERIFY_THROWS_NO_EXCEPTION(imported = loadFeedback(offset, {}));
        QCOMPARE(imported.notes[0].comment, note.comment);
    }
    void feedbackRejectsInvalidMetadata_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("unknown feedback version") << QString("schemaVersion") << QJsonValue("feedback-minimal-99");
        QTest::newRow("null version") << QString("schemaVersion") << QJsonValue(QJsonValue::Null);
        QTest::newRow("wrong schema id") << QString("$schema") << QJsonValue("urn:other:2");
        QTest::newRow("missing tool") << QString("tool") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("tool string") << QString("tool") << QJsonValue("EditHere");
        QTest::newRow("wrong tool") << QString("tool")
            << QJsonValue(QJsonObject{{"name", "Other"}, {"version", "1.0"}});
        QTest::newRow("missing tool version") << QString("tool")
            << QJsonValue(QJsonObject{{"name", "EditHere"}});
        QTest::newRow("blank tool version") << QString("tool")
            << QJsonValue(QJsonObject{{"name", "EditHere"}, {"version", " "}});
        QTest::newRow("oversized tool version") << QString("tool")
            << QJsonValue(QJsonObject{{"name", "EditHere"}, {"version", QString(101, 'a')}});
        QTest::newRow("null legacy date") << QString("exportedAt") << QJsonValue(QJsonValue::Null);
        QTest::newRow("invalid date") << QString("exportedAt") << QJsonValue("not-a-date");
        QTest::newRow("date without timezone") << QString("exportedAt") << QJsonValue("2026-10-16T13:14:15.123");
        QTest::newRow("missing annotation space") << QString("annotationSpace") << QJsonValue(QJsonValue::Undefined);
    }
    void feedbackRejectsInvalidMetadata() {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        auto feedback = exportFeedback(fromImage(exampleImage(), "file", "metadata"), true);
        feedback.insert(field, value);
        QVERIFY_EXCEPTION_THROWN(loadFeedback(feedback, {}), std::runtime_error);
        QTemporaryDir directory;
        const auto path = directory.filePath("feedback.json");
        saveBytes(path, QJsonDocument(feedback).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
    }
    void numberedFeedbackPreservesSidebarOrderAndReadsEarlierFormats() {
        auto doc = fromImage(exampleImage(), "file", "numbered feedback");
        doc.layout = createLayout(doc.image.size(), {});
        const QRectF source(80, 80, 60, 40), destination(240, 80, 60, 40);
        const auto group = addLayoutRegion(*doc.layout, source);
        transformLayoutGroup(*doc.layout, group, destination);
        Note global; global.isGlobal = true; global.comment = "overall";
        Note point; point.point = {20, 30}; point.comment = "point";
        Note emptyMovement; emptyMovement.isPoint = false;
        emptyMovement.movementSource = source; emptyMovement.rect = destination.toRect();
        Note movement = emptyMovement; movement.id = uniqueId(); movement.comment = "movement";
        Note rectangle; rectangle.isPoint = false; rectangle.rect = {400, 80, 40, 30};
        rectangle.comment = "rectangle";
        doc.notes = {global, point, emptyMovement, movement, rectangle};
        const auto feedback = exportFeedback(doc, true);
        const auto objects = feedback["objects"].toArray();
        QSet<QString> ids;
        QSet<int> numbers;
        for (qsizetype index = 0; index < objects.size(); ++index) {
            const auto object = objects[index].toObject();
            QCOMPARE(object["id"].toString(), "o" + QString::number(index + 1));
            QVERIFY(!ids.contains(object["id"].toString()));
            ids.insert(object["id"].toString());
            for (const auto &value : object["annotations"].toArray()) {
                const auto annotation = value.toObject();
                const int number = annotation["number"].toInt();
                QVERIFY(number >= 1 && number <= doc.notes.size());
                QCOMPARE(annotation["text"].toString(), doc.notes[number - 1].comment);
                QVERIFY(!numbers.contains(number));
                numbers.insert(number);
            }
        }
        QCOMPARE(numbers, (QSet<int>{1, 2, 4, 5})); // An omitted empty movement leaves a valid gap.
        QCOMPARE(objects.first().toObject()["annotations"].toArray().first().toObject()["number"].toInt(), 1);
        auto shuffled = feedback;
        QJsonArray reversed;
        for (qsizetype index = objects.size(); index > 0; --index)
            reversed.append(objects[index - 1]);
        shuffled["objects"] = reversed;
        const auto restored = loadFeedback(shuffled, {});
        QCOMPARE(restored.notes.size(), 4);
        QCOMPARE(restored.notes[0].comment, global.comment);
        QCOMPARE(restored.notes[1].comment, point.comment);
        QCOMPARE(restored.notes[2].comment, movement.comment);
        QCOMPARE(restored.notes[3].comment, rectangle.comment);
        QCOMPARE(restored.notes[2].movementSource, movement.movementSource);
        QCOMPARE(restored.notes[2].rect, movement.rect);

        // J01 already emitted version 2 with string notes and no object IDs.
        auto earlier = feedback;
        QJsonArray earlierObjects;
        for (const auto &value : objects) {
            auto object = value.toObject();
            object.remove("id");
            QJsonArray texts;
            for (const auto &annotation : object["annotations"].toArray())
                texts.append(annotation.toObject()["text"]);
            object["annotations"] = texts;
            earlierObjects.append(object);
        }
        earlier["objects"] = earlierObjects;
        QCOMPARE(loadFeedback(earlier, {}).notes.size(), 4);
        earlier["image"] = earlier["image"].toObject()["dataUrl"];
        earlier.remove("capture");
        earlier = unversionedLegacyFeedback(earlier);
        QCOMPARE(loadFeedback(earlier, {}).notes.size(), 4);

        // Mixed notes allocate old strings around explicit numbers without collisions.
        qsizetype movementObjectIndex = 0;
        for (qsizetype i = 0; i < reversed.size(); ++i)
            if (reversed[i].toObject().contains("movement")) movementObjectIndex = i;
        auto mixedObject = reversed[movementObjectIndex].toObject();
        mixedObject["id"] = QString(256, 'a');
        mixedObject["annotations"] = QJsonArray{movement.comment};
        reversed[movementObjectIndex] = mixedObject;
        shuffled["objects"] = reversed;
        const auto mixed = loadFeedback(shuffled, {});
        QCOMPARE(mixed.notes[2].comment, movement.comment);
        auto boundaryObject = reversed.first().toObject();
        auto boundaryAnnotation = boundaryObject["annotations"].toArray().first().toObject();
        boundaryAnnotation["number"] = MaxNotes;
        boundaryObject["annotations"] = QJsonArray{boundaryAnnotation};
        reversed[0] = boundaryObject;
        shuffled["objects"] = reversed;
        QCOMPARE(loadFeedback(shuffled, {}).notes.last().comment, rectangle.comment);
    }
    void numberedFeedbackRejectsInvalidIdentifiers_data() {
        QTest::addColumn<QString>("corruption");
        for (const auto *name : {"duplicate-id", "blank-id", "oversized-id", "nonstring-id",
                                "duplicate-number", "zero-number", "negative-number", "fractional-number",
                                "oversized-number", "string-number", "missing-number", "missing-text",
                                "nonstring-text", "extra-annotation-field"})
            QTest::newRow(name) << QString(name);
    }
    void numberedFeedbackRejectsInvalidIdentifiers() {
        QFETCH(QString, corruption);
        auto doc = fromImage(exampleImage(), "file", "invalid identifiers");
        Note first; first.point = {20, 30}; first.comment = "first";
        Note second; second.point = {40, 50}; second.comment = "second";
        doc.notes = {first, second};
        auto feedback = exportFeedback(doc, true);
        auto objects = feedback["objects"].toArray();
        auto object = objects.first().toObject();
        auto annotations = object["annotations"].toArray();
        auto annotation = annotations.first().toObject();
        if (corruption == "duplicate-id") object["id"] = objects.last().toObject()["id"];
        if (corruption == "blank-id") object["id"] = " \t ";
        if (corruption == "oversized-id") object["id"] = QString(257, 'a');
        if (corruption == "nonstring-id") object["id"] = 3;
        if (corruption == "duplicate-number") annotation["number"] = 2;
        if (corruption == "zero-number") annotation["number"] = 0;
        if (corruption == "negative-number") annotation["number"] = -1;
        if (corruption == "fractional-number") annotation["number"] = 1.5;
        if (corruption == "oversized-number") annotation["number"] = MaxNotes + 1;
        if (corruption == "string-number") annotation["number"] = "1";
        if (corruption == "missing-number") annotation.remove("number");
        if (corruption == "missing-text") annotation.remove("text");
        if (corruption == "nonstring-text") annotation["text"] = false;
        if (corruption == "extra-annotation-field") annotation["unknown"] = true;
        annotations[0] = annotation;
        object["annotations"] = annotations;
        objects[0] = object;
        feedback["objects"] = objects;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(feedback, {}), std::runtime_error);
    }
    void previewLayoutIsIndependentOfSourceResolution() {
        auto makeDocument = [](int factor) {
            QImage image(960 * factor, 540 * factor, QImage::Format_RGB32);
            image.fill(Qt::white);
            auto doc = fromImage(image, "file", "proportional preview");
            Note note;
            note.isPoint = false;
            note.rect = {120 * factor, 100 * factor, 200 * factor, 150 * factor};
            note.comment = "Consistent annotation layout";
            doc.notes.append(note);
            return doc;
        };
        const auto small = makeDocument(1), large = makeDocument(4);
        const auto first = previewImage(small), second = previewImage(large);
        QCOMPARE(first.size(), second.size());
        // Coordinates are displayed as original pixels, so compare the image
        // area, title and comment body independently of the coordinate label.
        QCOMPARE(first.copy(0, 0, 984, first.height()), second.copy(0, 0, 984, second.height()));
        QCOMPARE(first.copy(1020, 122, 324, 60), second.copy(1020, 122, 324, 60));
        QCOMPARE(large.notes[0].rect, QRect(480, 400, 800, 600));
    }
    void compressedFeedbackKeepsCoordinatesAndProjectPixels() {
        QImage image(640,480,QImage::Format_RGB32);
        quint32 seed=42;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            seed=seed*1664525u+1013904223u;
            image.setPixelColor(x,y,QColor((seed>>24)&255,(seed>>16)&255,(seed>>8)&255));
        }
        auto doc=fromImage(image,"file","compression");
        const auto original=doc.png;
        Note note; note.isPoint=true; note.point={230,170}; note.comment="测试"; doc.notes.append(note);
        const auto uncompressed=serializeFeedback(doc,true);
        const auto compressed=serializeFeedback(doc,true,true);
        QVERIFY(compressed.size()<uncompressed.size());
        const auto restored=loadFeedback(QJsonDocument::fromJson(compressed).object(),{});
        QCOMPARE(restored.image.size(),image.size()); QCOMPARE(restored.notes[0].point,note.point);
        QCOMPARE(doc.png,original);
        QVERIFY(!serializeDocument(restored,true).isEmpty());
        qInfo("Compression sample: %lld -> %lld bytes",qint64(uncompressed.size()),qint64(compressed.size()));
    }
    void globalFeedbackAndPortableProject() {
        auto doc = fromImage(exampleImage(), "demo", "风格意见");
        Note global;
        global.isGlobal = true;
        global.comment = "整体采用克制、通透的杂志风格";
        global.point = {-100, -100}; // A global note intentionally has no valid image position.
        doc.notes = {global};
        const auto feedback = exportFeedback(doc, true);
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 1);
        QCOMPARE(objects[0].toObject()["kind"].toString(), QString("global"));
        QVERIFY(!objects[0].toObject().contains("source"));
        QCOMPARE(objects[0].toObject()["annotations"].toArray()[0].toObject()["text"].toString(), global.comment);
        const auto imported = loadFeedback(feedback, {});
        QVERIFY(imported.notes[0].isGlobal);
        QCOMPARE(imported.notes[0].comment, global.comment);
        QVERIFY(!imported.notes[0].movementSource);
        QTemporaryDir directory;
        const auto path = directory.filePath("风格设计.EDITHERE");
        saveBytes(path, serializeDocument(doc, true));
        const auto restored = loadDocument(path);
        QCOMPARE(restored.image, doc.image);
        QCOMPARE(restored.notes[0].id, global.id);
        QVERIFY(restored.notes[0].isGlobal);
        QCOMPARE(restored.notes[0].comment, global.comment);
        QVERIFY(!restored.layout);
        const auto project = QJsonDocument::fromJson(serializeDocument(restored, true)).object();
        QCOMPARE(project["schemaVersion"].toString(), QString("3.0.0"));
        QCOMPARE(project["annotationSpace"].toString(), QString("result"));
        QVERIFY(project["annotations"].toArray()[0].toObject()["point"].isNull());
        QVERIFY(project["annotations"].toArray()[0].toObject()["rectangle"].isNull());
        auto invalid = project;
        auto annotations = invalid["annotations"].toArray();
        auto annotation = annotations[0].toObject();
        annotation["point"] = QJsonObject{{"x", 1}, {"y", 1}};
        annotations[0] = annotation;
        invalid["annotations"] = annotations;
        saveBytes(path, QJsonDocument(invalid).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
    }
    void movementNumbersMatchNotesAndFeedback() {
        QImage image(960, 220, QImage::Format_ARGB32);
        image.fill(Qt::white);
        auto doc = fromImage(image, "demo", "箭头编号");
        doc.layout = createLayout(image.size(), {});
        const auto first = addLayoutRegion(*doc.layout, {20, 20, 40, 30});
        const auto second = addLayoutRegion(*doc.layout, {20, 100, 40, 30});
        const auto third = addLayoutRegion(*doc.layout, {20, 160, 40, 30});
        transformLayoutGroup(*doc.layout, first, {200, 20, 40, 30});
        transformLayoutGroup(*doc.layout, second, {200, 100, 40, 30});
        transformLayoutGroup(*doc.layout, third, {220, 160, 40, 30});
        const auto plain = movementMarkers(*doc.layout, {});
        QCOMPARE(plain.size(), 3);
        for (int i = 0; i < plain.size(); ++i) {
            QCOMPARE(plain[i].number, i + 1);
            QCOMPARE(plain[i].noteIndex, -1);
        }
        // A movement without a note still gets a sequential orphan number so the
        // sidebar can display it; the canvas preview omits orphan badges.
        QCOMPARE(previewImage(doc).size(), QSize(1368, 268));
        Note global;
        global.isGlobal = true;
        global.comment = "整体留白";
        Note point;
        point.point = {300, 180};
        point.comment = "按钮";
        Note movement;
        movement.isPoint = false;
        movement.movementSource = QRectF(20, 100, 40, 30);
        movement.rect = {200, 100, 40, 30};
        movement.comment = "这个位置";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {260, 130, 40, 30};
        rectangle.comment = "保留这块";
        doc.notes = {global, point, movement, rectangle};
        const auto originalNotes = doc.notes;
        const auto feedback = exportFeedback(doc);
        const auto markers = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(markers.size(), 3);
        QCOMPARE(markers[0].number, 5);
        QCOMPARE(markers[0].noteIndex, -1);
        QCOMPARE(markers[1].number, 3);
        QCOMPARE(markers[1].noteIndex, 2);
        QCOMPARE(markers[2].number, 6);
        QCOMPARE(markers[2].noteIndex, -1);
        const auto preview = previewImage(doc);
        const auto annotatedAnchor = movementMarkerAnchor(markers[1], 1, image.size()).toPoint() + QPoint(24, 24);
        QCOMPARE(preview.pixelColor(annotatedAnchor + QPoint(-9, 0)), QColor("#007aff"));
        // The linked note's badge moves to the arrow; its old corner has no duplicate circle.
        QCOMPARE(preview.pixelColor(movement.rect.topLeft() + QPoint(17, 17)), QColor(Qt::white));
        const auto bareAnchor = movementMarkerAnchor(markers[0], 1, image.size()).toPoint() + QPoint(24, 24);
        QCOMPARE(preview.pixelColor(bareAnchor + QPoint(-9, 10)), QColor(Qt::white));
        int bluePixels = 0;
        for (int y = -14; y <= 14; ++y)
            for (int x = -14; x <= 14; ++x) {
                const auto color = preview.pixelColor(bareAnchor + QPoint(x, y));
                bluePixels += color.red() < 40 && color.green() < 160 && color.blue() > 230;
            }
        QCOMPARE(bluePixels, 0);
        QCOMPARE(doc.notes, originalNotes);
        QCOMPARE(stableFeedback(exportFeedback(doc)), stableFeedback(feedback));
        QCOMPARE(feedback.keys(), (QStringList{"annotationSpace", "capture", "image", "objects", "schemaVersion", "tool"}));
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 6);
        // The movement-linked object keeps the source rect, the destination,
        // and the linked note's text together in one entry.
        bool foundMovementObject = false;
        for (const auto &value : objects) {
            const auto obj = value.toObject();
            if (obj["source"].toObject() ==
                (QJsonObject{{"x1", 20.}, {"y1", 100.}, {"x2", 60.}, {"y2", 130.}})) {
                foundMovementObject = true;
                QVERIFY(obj.contains("movement"));
                QCOMPARE(obj["movement"].toObject()["rect"].toObject(),
                         (QJsonObject{{"x1", 200.}, {"y1", 100.}, {"x2", 240.}, {"y2", 130.}}));
                QCOMPARE(obj["annotations"].toArray().size(), 1);
                QCOMPARE(obj["annotations"].toArray()[0].toObject()["text"].toString(), movement.comment);
            }
        }
        QVERIFY(foundMovementObject);
        const auto project = QJsonDocument::fromJson(serializeDocument(doc, true)).object();
        QCOMPARE(project.keys(), (QStringList{"annotationSpace", "annotations", "capture", "exportedAt",
                                              "layout", "schemaVersion", "tool"}));
        QCOMPARE(project["annotations"].toArray().size(), 4);
        // Deleting a note releases its number without leaving a stale link on the arrow.
        doc.notes.removeAt(2);
        const auto afterDelete = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(afterDelete.size(), 3);
        for (int i = 0; i < afterDelete.size(); ++i) {
            QCOMPARE(afterDelete[i].number, i + 4);
            QCOMPARE(afterDelete[i].noteIndex, -1);
        }
    }
    void separatelyMovedRegionsKeepTheirOwnTrajectories() {
        auto layout = createLayout({240, 160}, {});
        const auto left = addLayoutRegion(layout, {20, 20, 20, 40});
        const auto right = addLayoutRegion(layout, {40, 20, 20, 40});
        const auto original = layout;
        transformLayoutGroup(layout, left, {100, 60, 20, 40});
        transformLayoutGroup(layout, right, {120, 60, 20, 40});
        Note leftNote;
        leftNote.isPoint = false;
        leftNote.movementSource = QRectF(20, 20, 20, 40);
        leftNote.rect = {100, 60, 20, 40};
        leftNote.comment = "左半部分";
        Note rightNote;
        rightNote.isPoint = false;
        rightNote.movementSource = QRectF(40, 20, 20, 40);
        rightNote.rect = {120, 60, 20, 40};
        rightNote.comment = "右半部分";
        const auto markers = movementMarkers(layout, {leftNote, rightNote});
        // Identical translations may share a compact pixel change, but these were
        // two separately selected components, each with its own annotation target.
        QCOMPARE(exportLayoutChanges(layout).size(), 1);
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers[0].source, *leftNote.movementSource);
        QCOMPARE(markers[0].destination, QRectF(leftNote.rect));
        QCOMPARE(markers[0].number, 1);
        QCOMPARE(markers[0].noteIndex, 0);
        QCOMPARE(markers[1].source, *rightNote.movementSource);
        QCOMPARE(markers[1].destination, QRectF(rightNote.rect));
        QCOMPARE(markers[1].number, 2);
        QCOMPARE(markers[1].noteIndex, 1);
        QCOMPARE(movementAnnotationIndex(rightNote, layout), 0);
        const auto remaining = movementMarkers(layout, {rightNote});
        QCOMPARE(remaining.size(), 2);
        QCOMPARE(remaining[0].noteIndex, -1);
        QCOMPARE(remaining[0].number, 2);
        QCOMPARE(remaining[1].noteIndex, 0);
        QCOMPARE(remaining[1].number, 1);
        QVERIFY(movementMarkers(original, {leftNote, rightNote}).isEmpty());
        transformLayoutGroup(layout, left, {20, 20, 20, 40});
        transformLayoutGroup(layout, right, {40, 20, 20, 40});
        QVERIFY(movementMarkers(layout, {leftNote, rightNote}).isEmpty());
        // A centered resize uses a corner trajectory so it can still be selected.
        transformLayoutGroup(layout, left, {15, 10, 30, 60});
        QVERIFY(!exportLayoutChanges(layout).isEmpty());
        const auto resizeMarkers = movementMarkers(layout, {leftNote});
        QCOMPARE(resizeMarkers.size(), 1);
        QCOMPARE(resizeMarkers[0].noteIndex, 0);
        QVERIFY(movementMarkerLine(resizeMarkers[0]).length() > 0);
    }
    void nestedMovementNotesFollowTheirOwnFrames() {
        QImage image(800, 600, QImage::Format_ARGB32);
        image.fill(Qt::white);
        auto doc = fromImage(image, "demo", "嵌套移动");
        doc.layout = createLayout(image.size(), {});
        const QRectF parentSource(40, 40, 200, 160), childSource(40, 40, 100, 80);
        const QRectF parentDestination(350, 280, 300, 240), childDestination(600, 30, 150, 120);
        const auto parent = addLayoutRegion(*doc.layout, parentSource);
        const auto child = addLayoutRegion(*doc.layout, childSource);
        transformLayoutGroup(*doc.layout, parent, parentDestination);
        const auto parentOnly = *doc.layout;
        auto markers = movementMarkers(*doc.layout, {});
        QCOMPARE(markers.size(), 1);
        QCOMPARE(markers[0].source, parentSource);
        QCOMPARE(markers[0].destination, parentDestination);
        QCOMPARE(markers[0].number, 1);
        Note parentNote;
        parentNote.isPoint = false;
        parentNote.movementSource = parentSource;
        parentNote.rect = parentDestination.toRect();
        parentNote.comment = "整体移到这里";
        doc.notes = {parentNote};
        transformLayoutGroup(*doc.layout, child, childDestination);
        doc.notes = remapNotes(doc.notes, parentOnly, *doc.layout);
        QCOMPARE(doc.notes[0].rect, parentDestination.toRect());
        QCOMPARE(movementAnnotationDestination(doc.notes[0], *doc.layout).value(), parentDestination);
        markers = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers[0].destination, parentDestination);
        QCOMPARE(markers[0].noteIndex, 0);
        QCOMPARE(markers[1].source, childSource);
        QCOMPARE(markers[1].destination, childDestination);
        QCOMPARE(markers[1].noteIndex, -1);
        QCOMPARE(markers[1].number, 2);
        Note childNote;
        childNote.isPoint = false;
        childNote.movementSource = childSource;
        childNote.rect = childDestination.toRect();
        childNote.comment = "这个小块另放";
        // Insert the child first: containment must not steal the parent's badge.
        doc.notes.prepend(childNote);
        markers = movementMarkers(*doc.layout, doc.notes);
        QCOMPARE(markers[0].noteIndex, 1);
        QCOMPARE(markers[0].number, 2);
        QCOMPARE(markers[1].noteIndex, 0);
        QCOMPARE(markers[1].number, 1);
        // Compact reconstruction still describes disjoint pixels, and keeps both texts.
        const auto feedback = exportFeedback(doc, true);
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 2);
        // The parent object carries its own movement and annotation together,
        // preserving the link the legacy format lost when it split them.
        bool foundParentObject = false;
        for (const auto &value : objects) {
            const auto obj = value.toObject();
            if (obj["source"].toObject() ==
                (QJsonObject{{"x1", 40.}, {"y1", 40.}, {"x2", 240.}, {"y2", 200.}})) {
                foundParentObject = true;
                QVERIFY(obj.contains("movement"));
                QCOMPARE(obj["movement"].toObject()["rect"].toObject(),
                         (QJsonObject{{"x1", 350.}, {"y1", 280.}, {"x2", 650.}, {"y2", 520.}}));
                QCOMPARE(obj["annotations"].toArray()[0].toObject()["text"].toString(), parentNote.comment);
            }
        }
        QVERIFY(foundParentObject);
        const auto feedbackDoc = loadFeedback(feedback, {});
        QCOMPARE(renderLayout(feedbackDoc.image, *feedbackDoc.layout), renderLayout(doc.image, *doc.layout));
        QTemporaryDir directory;
        const auto path = directory.filePath("nested.edithere");
        saveBytes(path, serializeDocument(doc, true));
        const auto restored = loadDocument(path);
        QCOMPARE(*restored.layout, *doc.layout);
        const auto restoredMarkers = movementMarkers(*restored.layout, restored.notes);
        QCOMPARE(restoredMarkers.size(), 2);
        for (int i = 0; i < markers.size(); ++i) {
            QCOMPARE(restoredMarkers[i].source, markers[i].source);
            QCOMPARE(restoredMarkers[i].destination, markers[i].destination);
            QCOMPARE(restoredMarkers[i].number, markers[i].number);
            QCOMPARE(restoredMarkers[i].noteIndex, markers[i].noteIndex);
        }
        // Undoing the child edit restores the parent's single arrow and annotation frame.
        QCOMPARE(movementMarkers(parentOnly, {parentNote}).size(), 1);
        QCOMPARE(remapNotes(doc.notes, *doc.layout, parentOnly)[1].rect, parentNote.rect);
    }
    void shortMovementBadgeLeavesTheArrowVisible() {
        MovementMarker marker{QRectF(20, 140, 20, 10), QRectF(22, 140, 20, 10), 1, -1};
        const auto anchor = movementMarkerAnchor(marker, 1, {240, 160});
        const auto midpoint = (marker.source.center() + marker.destination.center()) * 0.5;
        QVERIFY(QLineF(anchor, midpoint).length() >= 20);
        QVERIFY(anchor.x() >= 14 && anchor.x() <= 226);
        QVERIFY(anchor.y() >= 14 && anchor.y() <= 146);
        QVERIFY(anchor.y() < midpoint.y());
        marker.destination.translate(100, -80);
        QCOMPARE(movementMarkerAnchor(marker, 1, {240, 160}),
                 (marker.source.center() + marker.destination.center()) * 0.5);
    }
    void geometry() {
        QCOMPARE(dragRect({220, 80}, {-5, 15}, {200, 100}), QRect(0, 15, 200, 65));
        QCOMPARE(moveRect({20, 20, 60, 40}, {300, -50}, {100, 100}), QRect(40, 0, 60, 40));
        QCOMPARE(moveRect({20, 20, 60, 40}, {-100, -100}, {100, 100}, 4), QRect(20, 20, 1, 1));
    }
    void allContainingRegions() {
        auto target = manualTarget();
        QVector<Candidate> candidates{{{0, 0, 100, 100}, target},
                                      {{10, 10, 10, 10}, target},
                                      {{15, 0, 35, 30}, target},
                                      {{5, 5, 45, 45}, target}};
        CandidatePicker picker;
        picker.update(candidates, {16, 16});
        QCOMPARE(picker.count(), 4);
        QCOMPARE(picker.current()->bounds, QRect(10, 10, 10, 10));
        picker.step(1);
        QCOMPARE(picker.current()->bounds, QRect(15, 0, 35, 30));
        picker.update(candidates, {17, 17});
        QCOMPARE(picker.level(), 2);
        picker.step(1);
        QCOMPARE(picker.current()->bounds, QRect(5, 5, 45, 45));
        picker.step(1);
        QCOMPARE(picker.current()->bounds, QRect(0, 0, 100, 100));
        picker.step(1);
        QCOMPARE(picker.level(), 4);
        for (int i = 0; i < 10; ++i)
            picker.step(-1);
        QCOMPARE(picker.level(), 1);
        picker.step(0);
        QCOMPARE(picker.level(), 1);
        picker.update(candidates, {100, 100});
        QVERIFY(!picker.current());
        picker.step(1);
        QVERIFY(!picker.current());
    }
    void tableRegionSelection() {
        const auto target = manualTarget();
        const QRect cell1(10, 10, 20, 20), row12(10, 10, 40, 20), cell3(10, 30, 20, 20),
            row34(10, 30, 40, 20), lower3456(10, 30, 40, 40), entire123456(10, 10, 40, 60);
        QVector<Candidate> candidates{{lower3456, target},    {row12, target}, {cell1, target},
                                      {entire123456, target}, {cell3, target}, {row34, target}};
        CandidatePicker picker;
        picker.update(candidates, {15, 15});
        QCOMPARE(picker.count(), 3);
        QCOMPARE(picker.current()->bounds, cell1);
        picker.step(1);
        QCOMPARE(picker.current()->bounds, row12);
        picker.step(1);
        QCOMPARE(picker.current()->bounds, entire123456);
        // Without the level being remembered, moving to another cell takes that cell:
        // the smallest block under the pointer, which is what a hovering look has
        // always offered.
        picker.forget();
        picker.update(candidates, {15, 35});
        QCOMPARE(picker.count(), 4);
        QCOMPARE(picker.current()->bounds, cell3);
        for (const auto &expected : {row34, lower3456, entire123456}) {
            picker.step(1);
            QCOMPARE(picker.current()->bounds, expected);
        }
        picker.update(candidates, {16, 35});
        QCOMPARE(picker.current()->bounds, entire123456);
        for (const auto &expected : {lower3456, row34, cell3}) {
            picker.step(-1);
            QCOMPARE(picker.current()->bounds, expected);
        }
    }
    void theLevelPickedWithTheWheelIsKept() {
        const auto target = manualTarget();
        const QRect cell1(10, 10, 20, 20), row12(10, 10, 40, 20), cell3(10, 30, 20, 20),
            row34(10, 30, 40, 20), lower3456(10, 30, 40, 40), entire123456(10, 10, 40, 60);
        QVector<Candidate> candidates{{lower3456, target},    {row12, target}, {cell1, target},
                                      {entire123456, target}, {cell3, target}, {row34, target}};
        CandidatePicker picker;
        picker.update(candidates, {15, 15});
        QCOMPARE(picker.current()->bounds, cell1);
        // The wheel asks for the row rather than the cell, and that is a wish about
        // how coarse a block is wanted rather than about one block: it has to be the
        // level still on offer after the pointer has moved somewhere else.
        picker.step(1);
        QCOMPARE(picker.current()->bounds, row12);
        picker.update(candidates, {15, 35});
        QCOMPARE(picker.current()->bounds, row34);
        // Where there is no such level any more the outermost one is offered rather
        // than nothing at all.
        picker.update(candidates, {15, 15});
        QCOMPARE(picker.current()->bounds, row12);
        // Forgetting is what a fresh start does, and a fresh look offers the smallest
        // block under the pointer again.
        picker.forget();
        picker.update(candidates, {15, 35});
        QCOMPARE(picker.current()->bounds, cell3);
        picker.step(1);
        QCOMPARE(picker.current()->bounds, row34);
        picker.update(candidates, {15, 15});
        QCOMPARE(picker.current()->bounds, row12);
    }
    void aBlockIsKeptWhileThePointerIsFineTuned() {
        const auto target = manualTarget();
        const QRect cell1(10, 10, 20, 20), row12(10, 10, 40, 20), cell3(10, 30, 20, 20),
            row34(10, 30, 40, 20), lower3456(10, 30, 40, 40), entire123456(10, 10, 40, 60);
        QVector<Candidate> candidates{{lower3456, target},    {row12, target}, {cell1, target},
                                      {entire123456, target}, {cell3, target}, {row34, target}};
        CandidatePicker picker;
        // Beside the cell rather than in it: the row is the smallest block here, so
        // the row is what is offered.
        picker.update(candidates, {35, 15});
        QCOMPARE(picker.current()->bounds, row12);
        // Fine-tuning into the cell with the keys keeps the row: a block that moved
        // while it was being fine-tuned is a block that has to be found again.
        picker.update(candidates, {15, 15}, true);
        QCOMPARE(picker.current()->bounds, row12);
        picker.update(candidates, {25, 25}, true);
        QCOMPARE(picker.current()->bounds, row12);
        // Arriving there with the mouse takes what is under the pointer instead, which
        // is what finding blocks by hovering is for. It has to be a spot the pointer
        // has really travelled to: a look at the very spot it is already on is not a
        // request for anything.
        picker.update(candidates, {12, 12});
        QCOMPARE(picker.current()->bounds, cell1);
        // Out of the row altogether: the smallest block under the pointer again, and
        // fine-tuning from there keeps that.
        picker.update(candidates, {15, 35});
        QCOMPARE(picker.current()->bounds, cell3);
        picker.update(candidates, {25, 35}, true);
        QCOMPARE(picker.current()->bounds, cell3);
    }
    void equalAreaAndDuplicateRegions() {
        auto native = manualTarget(), visual = manualTarget();
        native["source"] = "uia";
        visual["source"] = "vision";
        const QRect tall(5, 0, 20, 40), wide(0, 5, 40, 20), larger(0, 0, 50, 50);
        QVector<Candidate> candidates{{tall, native}, {wide, visual}, {tall, visual}, {larger, visual}};
        CandidatePicker picker;
        picker.update(candidates, {15, 15});
        QCOMPARE(picker.count(), 3);
        QCOMPARE(picker.current()->bounds, tall);
        QCOMPARE(picker.current()->target["source"].toString(), QString("uia"));
        picker.step(1);
        QCOMPARE(picker.current()->bounds, wide);
        // A new small candidate must not reset an already selected region.
        candidates.prepend({QRect(12, 12, 10, 10), visual});
        picker.update(candidates, {16, 15});
        QCOMPARE(picker.count(), 4);
        QCOMPARE(picker.current()->bounds, wide);
        QCOMPARE(picker.level(), 3);
        picker.step(1);
        QCOMPARE(picker.current()->bounds, larger);
        picker.reset();
        picker.forget();
        picker.update(candidates, {16, 15});
        QCOMPARE(picker.current()->bounds, QRect(12, 12, 10, 10));
    }
    void history() {
        Note note;
        note.comment = "初始";
        QVector<Note> first{note}, second = first;
        second[0].comment = "修改";
        History history;
        history.push(first);
        QCOMPARE(history.undo(second)[0].comment, QString("初始"));
        QCOMPARE(history.redo(first)[0].comment, QString("修改"));
        history.undo(second);
        history.push(first);
        QVERIFY(!history.canRedo());
    }
    void roundTrip() {
        QTemporaryDir dir;
        auto doc = fromImage(exampleImage(), "demo", "示例");
        Note point;
        point.point = {200, 100};
        point.comment = "中文 \" \\ \n <script>";
        doc.notes.append(point);
        Note rect;
        rect.isPoint = false;
        rect.rect = {30, 40, 140, 180};
        rect.comment = "保留布局";
        doc.notes.append(rect);
        const auto exported = exportDocument(doc, true);
        QString path = dir.filePath("review.json");
        saveBytes(path, QJsonDocument(exported).toJson());
        auto loaded = loadDocument(path);
        QCOMPARE(loaded.notes, doc.notes);
        QCOMPARE(loaded.png, doc.png);
        QCOMPARE(loaded.image.size(), doc.image.size());
        QVERIFY(exportDocument(doc, false)["capture"].toObject()["pngBase64"].isNull());
        auto broken = doc;
        broken.notes[0].point = {doc.image.width(), 0};
        QVERIFY_EXCEPTION_THROWN(validateDocument(broken), std::runtime_error);
        auto json = exported;
        auto capture = json["capture"].toObject();
        capture["sha256"] = QString(64, '0');
        json["capture"] = capture;
        saveBytes(path, QJsonDocument(json).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
    }
    void savedProjectsRespectReadLimits() {
        // Exercise the exact byte boundaries without allocating image-sized fixtures.
        validateProjectStorageSize(MaxProjectFileBytes, MaxImageFileBytes);
        QVERIFY_EXCEPTION_THROWN(validateProjectStorageSize(MaxProjectFileBytes + 1), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(validateProjectStorageSize(0, MaxImageFileBytes + 1), std::runtime_error);

        QTemporaryDir dir;
        QImage image(24, 18, QImage::Format_ARGB32);
        image.fill(QColor("#4676c9"));
        auto doc = fromImage(image, "demo", "保存边界");
        doc.layout = createLayout(image.size(), {});
        const auto path = dir.filePath("review.json");
        saveBytes(path, serializeDocument(doc, true));
        const auto loaded = loadDocument(path);
        QVERIFY(loaded.layout == doc.layout);
        QCOMPARE(loaded.png, doc.png);
        saveBytes(dir.filePath(doc.imageFile), doc.png);
        saveBytes(path, serializeDocument(doc, false));
        QVERIFY(loadDocument(path).layout == doc.layout);

        // Sparse/truncated files verify the reader enforces the same limits before parsing or decoding.
        QFile oversizedProject(dir.filePath("oversized.json"));
        QVERIFY(oversizedProject.open(QIODevice::WriteOnly));
        QVERIFY(oversizedProject.resize(MaxProjectFileBytes + 1));
        oversizedProject.close();
        bool projectLimitRejected = false;
        try {
            loadDocument(oversizedProject.fileName());
        } catch (const std::runtime_error &error) {
            projectLimitRejected = QString::fromUtf8(error.what()).contains("项目文件不能超过");
        }
        QVERIFY(projectLimitRejected);
        QFile oversizedImage(dir.filePath("oversized.png"));
        QVERIFY(oversizedImage.open(QIODevice::WriteOnly));
        QVERIFY(oversizedImage.resize(MaxImageFileBytes + 1));
        oversizedImage.close();
        bool imageLimitRejected = false;
        try {
            loadDocument(oversizedImage.fileName());
        } catch (const std::runtime_error &error) {
            imageLimitRejected = QString::fromUtf8(error.what()).contains("图片文件不能超过");
        }
        QVERIFY(imageLimitRejected);
    }
    void rejectsInvalidMetadata() {
        QTemporaryDir dir;
        auto doc = fromImage(exampleImage(), "demo", "示例");
        Note note;
        note.point = {10, 20};
        note.comment = "调整";
        doc.notes.append(note);
        auto valid = exportDocument(doc, true);
        auto reject = [&](QJsonObject json) {
            QString path = dir.filePath("bad.json");
            saveBytes(path, QJsonDocument(json).toJson());
            QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        };
        auto bad = valid;
        bad["exportedAt"] = "not-a-date";
        reject(bad);
        bad = valid;
        auto capture = bad["capture"].toObject();
        capture["title"] = 42;
        bad["capture"] = capture;
        reject(bad);
        bad = valid;
        capture = bad["capture"].toObject();
        capture["screenBounds"] = QJsonObject{{"x1", 5}, {"y1", 0}, {"x2", 1}, {"y2", 20}};
        bad["capture"] = capture;
        reject(bad);
        for (const auto &key : {"controlType", "automationId", "originalScreenBounds"}) {
            bad = valid;
            auto notes = bad["annotations"].toArray();
            auto n = notes[0].toObject();
            auto target = n["target"].toObject();
            target[key] = 42;
            n["target"] = target;
            notes[0] = n;
            bad["annotations"] = notes;
            reject(bad);
        }
        bad = valid;
        capture = bad["capture"].toObject();
        capture["imageFile"] = "../capture.png";
        bad["capture"] = capture;
        reject(bad);
    }
    void compatibilityAndSources() {
        QString fixture = QFINDTESTDATA("fixtures/desktop-review.json");
        QVERIFY(!fixture.isEmpty());
        auto legacy = loadDocument(fixture);
        QVERIFY(!legacy.notes.isEmpty());
        QTemporaryDir dir;
        auto doc = fromImage(exampleImage(), "demo", "兼容测试");
        Note note;
        note.comment = "更改按钮";
        note.point = {10, 20};
        note.target["source"] = "accessibility";
        note.target["method"] = "macos-ax";
        doc.notes.append(note);
        auto ax = exportDocument(doc, true);
        QCOMPARE(ax["schemaVersion"].toString(), QString("1.1.0"));
        auto path = dir.filePath("ax.json");
        saveBytes(path, QJsonDocument(ax).toJson());
        QCOMPARE(loadDocument(path).notes, doc.notes);
        ax["schemaVersion"] = "1.0.0";
        saveBytes(path, QJsonDocument(ax).toJson());
        QVERIFY_EXCEPTION_THROWN(loadDocument(path), std::runtime_error);
        doc.notes[0].target = manualTarget();
        saveBytes(dir.filePath(doc.imageFile), doc.png);
        saveBytes(path, QJsonDocument(exportDocument(doc, false)).toJson());
        QCOMPARE(loadDocument(path).notes, doc.notes);
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            saveBytes(QDir(folder).filePath("feedback-v1.json"),
                      QJsonDocument(exportDocument(doc, true)).toJson());
            doc.notes[0].target["source"] = "accessibility";
            saveBytes(QDir(folder).filePath("feedback-v1.1.json"),
                      QJsonDocument(exportDocument(doc, true)).toJson());
        }
    }
    void legacyFeedbackStillImports() {
        // The compact feedback moved to the object structure in 0.9.0, but the parallel
        // annotations/changes arrays stay importable (docs/AGENT-CLI.md). Nothing writes that
        // shape any more, so it is built by hand here; without this test the documented
        // "old files still open" promise would only be verified by reading the source.
        auto doc = fromImage(exampleImage(), "demo", "旧版反馈");
        QTemporaryDir dir;
        const QString path = dir.filePath("legacy.json");
        const QJsonObject feedback{
            {"annotationSpace", "result"},
            {"annotations",
             QJsonArray{QJsonObject{{"point", QJsonObject{{"x", 30}, {"y", 50}}},
                                    {"text", "这里的文字改一下"}},
                        QJsonObject{{"rectangle", QJsonObject{{"x1", 10}, {"y1", 200}, {"x2", 60}, {"y2", 240}}},
                                    {"text", "这块放大"}},
                        QJsonObject{{"change", 0}, {"text", "把这个挪到右下"}},
                        QJsonObject{{"text", "整体再简洁一些"}}}},
            {"changes",
             QJsonArray{QJsonObject{
                 {"from", QJsonObject{{"x1", 0}, {"y1", 0}, {"x2", 40}, {"y2", 40}}},
                 {"to", QJsonObject{{"x1", 80}, {"y1", 80}, {"x2", 120}, {"y2", 120}}}}}},
            {"image", "data:image/png;base64," + QString::fromLatin1(doc.png.toBase64())}};
        saveBytes(path, QJsonDocument(feedback).toJson(QJsonDocument::Compact));
        const auto imported = loadDocument(path);
        QCOMPARE(imported.notes.size(), 4);
        QCOMPARE(imported.notes[0].point, QPoint(30, 50));
        QCOMPARE(imported.notes[0].comment, QString("这里的文字改一下"));
        QCOMPARE(imported.notes[1].rect, QRect(10, 200, 50, 40));
        QVERIFY(imported.notes[2].movementSource.has_value());
        QCOMPARE(imported.notes[2].rect, QRect(80, 80, 40, 40));
        QVERIFY(imported.notes[3].isGlobal);
        QVERIFY(imported.layout.has_value());
        // Re-exporting an imported old file already uses the current structure.
        QCOMPARE(exportFeedback(imported, false).keys(), (QStringList{"annotationSpace", "image", "objects", "schemaVersion", "tool"}));
    }
    void objectFeedbackRejectsInvalidGeometry_data() {
        QTest::addColumn<QJsonValue>("source");
        QTest::addColumn<QJsonArray>("movements");
        QTest::addColumn<QJsonArray>("annotations");
        const QJsonObject rect = rectJson({10, 10, 20, 20});
        const QJsonObject point{{"x1", 10}, {"y1", 10}, {"x2", 10}, {"y2", 10}};
        const QJsonArray text{"comment"};
        QTest::newRow("negative source") << QJsonValue(rectJson({30, 30, -20, -20})) << QJsonArray{} << text;
        QTest::newRow("line source") << QJsonValue(rectJson({10, 10, 0, 20})) << QJsonArray{} << text;
        QTest::newRow("clipped source") << QJsonValue(rectJson({-5, 10, 20, 20})) << QJsonArray{} << text;
        QTest::newRow("empty off-canvas source") << QJsonValue(rectJson({500, 500, 20, 20}))
                                                   << QJsonArray{} << QJsonArray{};
        QTest::newRow("clipped destination") << QJsonValue(rect)
            << QJsonArray{QJsonObject{{"to", rectJson({-5, 10, 20, 20})}}} << text;
        QTest::newRow("point movement") << QJsonValue(point)
            << QJsonArray{QJsonObject{{"to", rectJson({50, 50, 20, 20})}}} << text;
        QTest::newRow("point malformed movement") << QJsonValue(point) << QJsonArray{42} << QJsonArray{};
        QTest::newRow("global malformed movement") << QJsonValue(QJsonValue::Null)
                                                     << QJsonArray{42} << QJsonArray{};
    }
    void objectFeedbackRejectsInvalidGeometry() {
        QFETCH(QJsonValue, source);
        QFETCH(QJsonArray, movements);
        QFETCH(QJsonArray, annotations);
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QJsonObject feedback{{"annotationSpace", "result"},
            {"objects", QJsonArray{QJsonObject{{"source", source}, {"movements", movements},
                                               {"annotations", annotations}}}}};
        QVERIFY_EXCEPTION_THROWN(loadFeedback(feedback, image), std::runtime_error);
    }
    void objectFeedbackLimitsCountAnnotationsAndMovementsSeparately() {
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        auto document = fromImage(image, "demo", "feedback limits");
        document.layout = createLayout(image.size(), {});
        const auto id = addLayoutRegion(*document.layout, {10, 10, 20, 20});
        transformLayoutGroup(*document.layout, id, {50, 50, 20, 20});
        for (int i = 0; i < MaxNotes; ++i) {
            Note note;
            note.isGlobal = true;
            note.comment = "global comment";
            document.notes.append(note);
        }
        const auto feedback = exportFeedback(document);
        QCOMPARE(feedback["objects"].toArray().size(), MaxNotes + 1);
        const auto restored = loadFeedback(feedback, image, document.png);
        QCOMPARE(restored.notes.size(), MaxNotes);
        QVERIFY(restored.layout.has_value());
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QVERIFY(QDir().mkpath(folder));
            saveBytes(QDir(folder).filePath("feedback-limit.json"), serializeFeedback(document, true));
        }
        auto invalid = feedback;
        auto objects = feedback["objects"].toArray();
        objects.append(QJsonObject{{"source", QJsonValue::Null}, {"movements", QJsonArray{}},
                                   {"annotations", QJsonArray{"too many notes"}}});
        invalid["objects"] = objects;
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, image, document.png), std::runtime_error);
        QJsonArray movements;
        for (int i = 0; i <= MaxLayoutMovements; ++i)
            movements.append(QJsonObject{{"to", rectJson({50, 50, 20, 20})}});
        invalid["objects"] = QJsonArray{QJsonObject{{"source", rectJson({10, 10, 20, 20})},
                                                   {"movements", movements}, {"annotations", QJsonArray{}}}};
        QVERIFY_EXCEPTION_THROWN(loadFeedback(invalid, image, document.png), std::runtime_error);
    }
    void objectFeedbackNotesUseTheFinalDestination() {
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QJsonObject feedback{{"annotationSpace", "result"},
            {"objects", QJsonArray{QJsonObject{{"source", rectJson({10, 10, 20, 20})},
                {"movements", QJsonArray{QJsonObject{{"to", rectJson({40, 40, 20, 20})}},
                                         QJsonObject{{"to", rectJson({70, 70, 20, 20})}}}},
                {"annotations", QJsonArray{"final position"}}}}}};
        const auto restored = loadFeedback(feedback, image);
        QVERIFY(restored.layout.has_value());
        QCOMPARE(restored.notes.size(), 1);
        QCOMPARE(restored.notes[0].rect, QRect(70, 70, 20, 20));
        QCOMPARE(movementAnnotationDestination(restored.notes[0], *restored.layout),
                 std::optional<QRectF>(QRectF(70, 70, 20, 20)));
    }
    void imageFormats() {
        auto formats = QImageReader::supportedImageFormats();
        for (auto format : {"png", "jpeg", "bmp", "webp"})
            QVERIFY(formats.contains(format));
    }
    void denseDetectionRetainsOuterContainer() {
        QImage image(1100, 1100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        const QRect outer(2, 2, 1096, 1096);
        painter.fillRect(outer, QColor("#e5e5e5"));
        for (int row = 0; row < 18; ++row)
            for (int column = 0; column < 18; ++column)
                painter.fillRect(QRect(20 + column * 58, 20 + row * 58, 32, 26), QColor("#456b51"));
        painter.end();
        const auto candidates = detectBlocks(image);
        QVERIFY(candidates.size() <= 220);
        QVERIFY(candidates.size() >= 100);
        bool foundOuter = false, foundCell = false;
        for (const auto &candidate : candidates) {
            foundOuter |= candidate.bounds == outer;
            foundCell |= candidate.bounds.size() == QSize(32, 26);
        }
        QVERIFY2(foundOuter, "A dense grid must not evict its large enclosing container.");
        QVERIFY(foundCell);
    }
    void detection() {
        QImage blank(240, 180, QImage::Format_RGB32);
        blank.fill(Qt::white);
        QVERIFY(detectBlocks(blank).isEmpty());
        QPainter painter(&blank);
        painter.fillRect(20, 25, 90, 75, QColor("#456b51"));
        painter.end();
        auto candidates = detectBlocks(blank);
        bool found = false;
        for (auto c : candidates)
            if (c.bounds == QRect(20, 25, 90, 75))
                found = true;
        QVERIFY(found);
    }
};
QTEST_MAIN(CoreTests)
#include "core_test.moc"
