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
    void globalFeedbackAndPortableProject() {
        auto doc = fromImage(exampleImage(), "demo", "风格意见");
        Note global;
        global.isGlobal = true;
        global.comment = "整体采用克制、通透的杂志风格";
        global.point = {-100, -100}; // A global note intentionally has no valid image position.
        doc.notes = {global};
        const auto feedback = exportFeedback(doc, true);
        QCOMPARE(feedback["annotations"].toArray()[0].toObject(),
                 (QJsonObject{{"text", global.comment}}));
        const auto imported = loadFeedback(feedback, {});
        QVERIFY(imported.notes[0].isGlobal);
        QCOMPARE(imported.notes[0].comment, global.comment);
        QVERIFY(!imported.notes[0].movementSource);
        QTemporaryDir directory;
        const auto path = directory.filePath("风格设计.HELPDESIGN");
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
