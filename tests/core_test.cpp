#include "detector.h"
#include "model.h"
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
using namespace h2d;
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
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
        QCOMPARE(objects[0].toObject()["source"], QJsonValue(QJsonValue::Null));
        QCOMPARE(objects[0].toObject()["annotations"].toArray()[0].toString(), global.comment);
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
    void movementNumbersMatchNotesAndStayOutOfJson() {
        QImage image(360, 220, QImage::Format_ARGB32);
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
        QCOMPARE(previewImage(doc).size(), QSize(768, 268));
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
        QCOMPARE(exportFeedback(doc), feedback);
        QCOMPARE(feedback.keys(), (QStringList{"annotationSpace", "objects"}));
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
                QCOMPARE(obj["movements"].toArray().size(), 1);
                QCOMPARE(obj["movements"].toArray()[0].toObject()["to"].toObject(),
                         (QJsonObject{{"x1", 200.}, {"y1", 100.}, {"x2", 240.}, {"y2", 130.}}));
                QCOMPARE(obj["annotations"].toArray().size(), 1);
                QCOMPARE(obj["annotations"].toArray()[0].toString(), movement.comment);
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
        // A resize about the same center has no directional arrow to number.
        transformLayoutGroup(layout, left, {15, 10, 30, 60});
        QVERIFY(!exportLayoutChanges(layout).isEmpty());
        QVERIFY(movementMarkers(layout, {leftNote}).isEmpty());
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
                QCOMPARE(obj["movements"].toArray().size(), 1);
                QCOMPARE(obj["movements"].toArray()[0].toObject()["to"].toObject(),
                         (QJsonObject{{"x1", 350.}, {"y1", 280.}, {"x2", 650.}, {"y2", 520.}}));
                QCOMPARE(obj["annotations"].toArray()[0].toString(), parentNote.comment);
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
