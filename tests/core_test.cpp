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
    void geometry() {
        QCOMPARE(dragRect({220, 80}, {-5, 15}, {200, 100}), QRect(0, 15, 200, 65));
        QCOMPARE(moveRect({20, 20, 60, 40}, {300, -50}, {100, 100}), QRect(40, 0, 60, 40));
        QCOMPARE(moveRect({20, 20, 60, 40}, {-100, -100}, {100, 100}, 4), QRect(20, 20, 1, 1));
    }
    void hierarchy() {
        auto target = manualTarget();
        QVector<Candidate> candidates{{{0, 0, 100, 100}, target},
                                      {{10, 10, 10, 10}, target},
                                      {{15, 0, 35, 30}, target},
                                      {{5, 5, 45, 45}, target}};
        CandidatePicker picker;
        picker.update(candidates, {16, 16});
        QCOMPARE(picker.count(), 3);
        QCOMPARE(picker.current()->bounds, QRect(10, 10, 10, 10));
        picker.step(1);
        picker.update(candidates, {17, 17});
        QCOMPARE(picker.level(), 2);
        picker.step(1);
        picker.step(1);
        QCOMPARE(picker.level(), 3);
        picker.update(candidates, {100, 100});
        QVERIFY(!picker.current());
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
