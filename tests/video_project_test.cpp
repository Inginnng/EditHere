#include "videoproject.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <stdexcept>
using namespace h2d;
namespace {
Document frameDocument(const QString &comment) {
    QImage image(320, 180, QImage::Format_ARGB32);
    image.fill(QColor("#26374f"));
    auto document = fromImage(image, "file", "Video frame");
    if (!comment.isEmpty()) {
        Note note;
        note.isPoint = false;
        note.rect = {80, 40, 100, 32};
        note.comment = comment;
        document.notes.append(note);
    }
    return document;
}
VideoProject sampleProject() {
    VideoProject project;
    project.source = "D:/missing-source/宣传动画.mp4";
    project.durationMs = 30000;
    project.positionMs = 12000;
    project.frames = {{12540999, frameDocument("第二个时间点：标题向右移动")},
                      {4000123, frameDocument("第一个时间点：标题改为 EditHere")}};
    return project;
}
QJsonObject projectRoot(const VideoProject &project) {
    return QJsonDocument::fromJson(serializeVideoProject(project)).object();
}
bool rejects(QJsonObject root) {
    QTemporaryDir directory;
    const auto path = directory.filePath("invalid.edithere");
    saveBytes(path, QJsonDocument(root).toJson());
    try {
        loadVideoProject(path);
        return false;
    } catch (const std::runtime_error &) {
        return true;
    }
}
QJsonObject changedFrame(QJsonObject root, int index, const QString &key, const QJsonValue &value) {
    auto frames = root["frames"].toArray();
    auto frame = frames[index].toObject();
    frame[key] = value;
    frames[index] = frame;
    root["frames"] = frames;
    return root;
}
} // namespace

class VideoProjectTests : public QObject {
    Q_OBJECT
  private slots:
    void sameCoordinatesAtDifferentTimesRemainSeparate() {
        const auto project = sampleProject();
        const auto feedback = QJsonDocument::fromJson(serializeVideoFeedback(project)).object();
        QCOMPARE(feedback["schemaVersion"].toString(), QString("video-feedback-1"));
        QCOMPARE(feedback["video"].toObject()["source"].toString(), project.source);
        QCOMPARE(feedback["video"].toObject()["width"].toInt(), 320);
        QCOMPARE(feedback["video"].toObject()["height"].toInt(), 180);
        const auto frames = feedback["frames"].toArray();
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(frames.size(), 2);
        QCOMPARE(objects.size(), 2);
        QCOMPARE(videoAnnotationCount(project), 2);
        QCOMPARE(frames[0].toObject()["timestampUs"].toInteger(), 4000123);
        QCOMPARE(frames[0].toObject()["timestampMs"].toInteger(), 4000);
        QCOMPARE(frames[1].toObject()["timestampUs"].toInteger(), 12540999);
        QCOMPARE(frames[1].toObject()["timestampMs"].toInteger(), 12540);
        QCOMPARE(objects[0].toObject()["source"], objects[1].toObject()["source"]);
        QVERIFY(objects[0].toObject()["frameId"] != objects[1].toObject()["frameId"]);
        for (int i = 0; i < frames.size(); ++i) {
            const auto frame = frames[i].toObject();
            const auto object = objects[i].toObject();
            QCOMPARE(object["frameId"], frame["id"]);
            QCOMPARE(object["timestampMs"], frame["timestampMs"]);
            QCOMPARE(frame["imageFile"].toString(), "frame-" + frame["id"].toString() + ".png");
            auto existingFeedback = frame["feedback"].toObject();
            QVERIFY(existingFeedback.contains("image"));
            QCOMPARE(existingFeedback["objects"].toArray().size(), 1);
            // A frame is ordinary image feedback: no special video-aware parser
            // or removal of timestamp fields is required before loading it.
            const auto parsed = loadFeedback(existingFeedback, {});
            QCOMPARE(parsed.image.size(), QSize(320, 180));
            QCOMPARE(parsed.notes.size(), 1);
            QCOMPARE(parsed.notes[0].rect, QRect(80, 40, 100, 32));
            QCOMPARE(parsed.notes[0].comment, object["annotations"].toArray()[0].toString());
        }
        QVERIFY(!feedback["video"].toObject().contains("data"));
    }

    void projectRestoresLayoutNotesAndMissingSourceVideo() {
        auto project = sampleProject();
        auto &document = project.frames[0].document;
        document.notes.clear();
        document.layout = createLayout(document.image.size(), {});
        const auto group = addLayoutRegion(*document.layout, QRectF(10, 20, 40, 30));
        transformLayoutGroup(*document.layout, group, QRectF(100, 40, 50, 40));
        Note movement;
        movement.isPoint = false;
        movement.rect = {100, 40, 50, 40};
        movement.movementSource = QRectF(10, 20, 40, 30);
        // Empty text is valid: the graphical movement carries the instruction.
        document.notes.append(movement);
        Note global;
        global.isGlobal = true;
        global.comment = "这一帧整体减少留白";
        document.notes.append(global);
        QTemporaryDir directory;
        const auto path = directory.filePath("video.edithere");
        saveBytes(path, serializeVideoProject(project));
        QVERIFY(isVideoProjectFile(path));
        const auto restored = loadVideoProject(path);
        QCOMPARE(restored.source, project.source);
        QCOMPARE(restored.durationMs, project.durationMs);
        QCOMPARE(restored.positionMs, project.positionMs);
        QVERIFY(!restored.dirty);
        QCOMPARE(restored.frames.size(), 2);
        const auto &roundTrip = restored.frames[1].document;
        QCOMPARE(restored.frames[1].timestampUs, project.frames[0].timestampUs);
        QCOMPARE(roundTrip.id, document.id);
        QCOMPARE(roundTrip.png, document.png);
        QCOMPARE(roundTrip.image, document.image);
        QCOMPARE(roundTrip.notes, document.notes);
        QCOMPARE(roundTrip.layout, document.layout);
        const auto root = projectRoot(restored);
        QCOMPARE(root["frames"].toArray()[1].toObject()["document"].toObject()["schemaVersion"].toString(), QString("3.0.0"));
    }

    void blankFramesAreOmittedAndMovementOnlyFramesRemain() {
        auto project = sampleProject();
        project.frames.append({0, frameDocument({})});
        auto movementOnly = frameDocument({});
        movementOnly.layout = createLayout(movementOnly.image.size(), {});
        const auto group = addLayoutRegion(*movementOnly.layout, QRectF(10, 10, 30, 20));
        transformLayoutGroup(*movementOnly.layout, group, QRectF(120, 60, 60, 40));
        project.frames.append({18000000, movementOnly});
        const auto root = projectRoot(project);
        QCOMPARE(root["frames"].toArray().size(), 3);
        const auto feedback = QJsonDocument::fromJson(serializeVideoFeedback(project, false)).object();
        QCOMPARE(feedback["frames"].toArray().size(), 3);
        QCOMPARE(feedback["objects"].toArray().size(), 3);
        const auto last = feedback["frames"].toArray().last().toObject()["feedback"].toObject();
        QCOMPARE(last["objects"].toArray()[0].toObject()["movements"].toArray().size(), 1);
        QCOMPARE(last["objects"].toArray()[0].toObject()["annotations"].toArray().size(), 0);
    }

    void feedbackRoundTripUsesOneImageAndDoesNotDuplicateIndexNotes() {
        const auto project = sampleProject();
        QTemporaryDir directory;
        const auto path = directory.filePath("video-feedback.json");
        saveBytes(path, serializeVideoFeedback(project));
        QVERIFY(isVideoProjectFile(path));
        const auto restored = loadVideoProject(path);
        QCOMPARE(restored.source, project.source);
        QCOMPARE(restored.frames.size(), 2);
        QCOMPARE(videoAnnotationCount(restored), 2);
        QCOMPARE(restored.frames[0].document.notes.size(), 1);
        QCOMPARE(restored.frames[0].document.notes[0].comment, project.frames[1].document.notes[0].comment);
        QCOMPARE(restored.frames[0].document.id, project.frames[1].document.id);
        QCOMPARE(restored.frames[0].document.image, project.frames[1].document.image);
        // The project form remains lossless and editable after feedback import.
        saveBytes(directory.filePath("resaved.edithere"), serializeVideoProject(restored));
        QCOMPARE(loadVideoProject(directory.filePath("resaved.edithere")).frames.size(), 2);
    }

    void noImageFeedbackLoadsOnlyWithMatchingFrameFiles() {
        const auto project = sampleProject();
        const auto bytes = serializeVideoFeedback(project, false);
        const auto root = QJsonDocument::fromJson(bytes).object();
        QTemporaryDir directory;
        const auto path = directory.filePath("video-feedback.json");
        saveBytes(path, bytes);
        QVERIFY(!bytes.contains("data:image/"));
        for (const auto &frame : root["frames"].toArray())
            QVERIFY(!frame.toObject()["feedback"].toObject().contains("image"));
        QVERIFY_EXCEPTION_THROWN(loadVideoProject(path), std::runtime_error);
        for (const auto &frame : project.frames)
            saveBytes(directory.filePath("frame-" + frame.document.id + ".png"), frame.document.png);
        const auto restored = loadVideoProject(path);
        QCOMPARE(restored.frames.size(), 2);
        QCOMPARE(restored.frames[0].document.image, project.frames[1].document.image);
        QCOMPARE(restored.frames[0].document.notes[0].comment, project.frames[1].document.notes[0].comment);
        QImage wrong(20, 20, QImage::Format_RGB32);
        wrong.fill(Qt::red);
        saveBytes(directory.filePath("frame-" + project.frames[1].document.id + ".png"), encodePng(wrong));
        QVERIFY_EXCEPTION_THROWN(loadVideoProject(path), std::runtime_error);
    }

    void rejectsInvalidTimeIdsDimensionsAndMissingEmbeddedProjectFrames() {
        const auto root = projectRoot(sampleProject());
        QVERIFY(rejects(changedFrame(root, 0, "timestampUs", -1)));
        QVERIFY(rejects(changedFrame(root, 0, "timestampUs", 4000123.5)));
        QVERIFY(rejects(changedFrame(root, 0, "timestampMs", 4001)));
        QVERIFY(rejects(changedFrame(root, 0, "timestampUs", 31000000)));
        auto duplicate = root;
        auto frames = duplicate["frames"].toArray();
        frames.append(frames[0]);
        duplicate["frames"] = frames;
        QVERIFY(rejects(duplicate));
        QVERIFY(rejects(changedFrame(root, 1, "id", root["frames"].toArray()[0].toObject()["id"])));
        QVERIFY(rejects(changedFrame(root, 0, "id", "../escape")));
        auto document = root["frames"].toArray()[0].toObject()["document"].toObject();
        auto capture = document["capture"].toObject();
        capture["width"] = 321;
        document["capture"] = capture;
        QVERIFY(rejects(changedFrame(root, 0, "document", document)));
        capture["width"] = 320;
        capture["pngBase64"] = QJsonValue::Null;
        document["capture"] = capture;
        QVERIFY(rejects(changedFrame(root, 0, "document", document)));
        auto badVideo = root;
        auto video = root["video"].toObject();
        video["positionMs"] = 30001;
        badVideo["video"] = video;
        QVERIFY(rejects(badVideo));
        badVideo["unexpected"] = true;
        QVERIFY(rejects(badVideo));
    }

    void rejectsInconsistentFeedbackIndexAndUnsafeImageFiles() {
        auto root = QJsonDocument::fromJson(serializeVideoFeedback(sampleProject())).object();
        auto changed = root;
        auto objects = root["objects"].toArray();
        auto object = objects[0].toObject();
        object["timestampMs"] = 9999;
        objects[0] = object;
        changed["objects"] = objects;
        QVERIFY(rejects(changed));
        object["timestampMs"] = root["objects"].toArray()[0].toObject()["timestampMs"];
        object["frameId"] = "unknown-frame";
        objects[0] = object;
        changed["objects"] = objects;
        QVERIFY(rejects(changed));
        QVERIFY(rejects(changedFrame(root, 0, "imageFile", "../outside.png")));
        auto video = root["video"].toObject();
        video["height"] = 181;
        changed = root;
        changed["video"] = video;
        QVERIFY(rejects(changed));
    }

    void serializerRejectsInvalidOrAmbiguousProjectState() {
        auto project = sampleProject();
        project.frames[0].timestampUs = -1;
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
        project = sampleProject();
        project.frames[0].document.id = project.frames[1].document.id;
        QVERIFY_EXCEPTION_THROWN(serializeVideoFeedback(project), std::runtime_error);
        project = sampleProject();
        project.frames[0].timestampUs = project.frames[1].timestampUs;
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
        project = sampleProject();
        QImage other(321, 180, QImage::Format_RGB32);
        other.fill(Qt::white);
        project.frames[0].document = fromImage(other, "file", "different-size");
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
        project = sampleProject();
        project.source.clear();
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
        project = sampleProject();
        while (project.frames.size() <= MaxVideoFrames)
            project.frames.append(project.frames.first());
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
    }

    void aggregateLimitsBoundDecodedImagesAndAnnotationMemory() {
        VideoProject project;
        project.source = "missing.mp4";
        QImage image(2000, 2000, QImage::Format_ARGB32);
        image.fill(Qt::white);
        auto document = fromImage(image, "file", "Shared pixels");
        Note note;
        note.point = {10, 10};
        note.comment = "修改这里";
        document.notes.append(note);
        // Image and PNG copies share backing storage in the test. The serialized
        // video would need 132 million independent decoded screenshot pixels.
        for (int i = 0; i < 33; ++i) {
            auto copy = document;
            copy.id = uniqueId();
            project.frames.append({qint64(i) * 1000000, copy});
        }
        QVERIFY_EXCEPTION_THROWN(serializeVideoProject(project), std::runtime_error);
        project.frames.clear();
        document = frameDocument({});
        for (int i = 0; i < MaxNotes; ++i) {
            Note copy;
            copy.point = {10, 10};
            copy.comment = "批注";
            document.notes.append(copy);
        }
        for (int i = 0; i < 11; ++i) {
            auto copy = document;
            copy.id = uniqueId();
            project.frames.append({qint64(i) * 1000000, copy});
        }
        QVERIFY_EXCEPTION_THROWN(serializeVideoFeedback(project, false), std::runtime_error);
    }

    void relativeVideoAddressesResolveAgainstProjectDirectory() {
        auto project = sampleProject();
        project.source = "../media/演示.mp4";
        QTemporaryDir directory;
        const auto path = directory.filePath("relative.edithere");
        saveBytes(path, serializeVideoProject(project));
        const auto restored = loadVideoProject(path);
        QCOMPARE(restored.source, QDir::cleanPath(directory.filePath(project.source)));
        const auto feedbackPath = directory.filePath("relative.json");
        saveBytes(feedbackPath, serializeVideoFeedback(project));
        QCOMPARE(loadVideoProject(feedbackPath).source, restored.source);
        project.source = "https://example.com/media.mp4";
        saveBytes(path, serializeVideoProject(project));
        QCOMPARE(loadVideoProject(path).source, project.source);
    }

    void emptyProjectStillReferencesVideoAndExportsValidZeroDimensions() {
        VideoProject project;
        project.source = "relative-video.mp4";
        QTemporaryDir directory;
        const auto path = directory.filePath("empty.edithere");
        saveBytes(path, serializeVideoProject(project));
        QCOMPARE(loadVideoProject(path).source, directory.filePath(project.source));
        const auto feedback = QJsonDocument::fromJson(serializeVideoFeedback(project)).object();
        QCOMPARE(feedback["frames"].toArray().size(), 0);
        QCOMPARE(feedback["video"].toObject()["width"].toInt(), 0);
        QCOMPARE(feedback["video"].toObject()["height"].toInt(), 0);
        QCOMPARE(videoAnnotationCount(project), 0);
    }

    void fileRecognitionAndTimestampLabels() {
        QVERIFY(isVideoFile("demo.MP4"));
        QVERIFY(isVideoFile("演示.webm"));
        QVERIFY(!isVideoFile("capture.png"));
        QVERIFY(!isVideoProjectFile("nonexistent.edithere"));
        QCOMPARE(videoTimeLabel(0), QString("00:00.000"));
        QCOMPARE(videoTimeLabel(12540), QString("00:12.540"));
        QCOMPARE(videoTimeLabel(3600123), QString("01:00:00.123"));
        QCOMPARE(videoTimeLabel(-10), QString("00:00.000"));
    }

    void cliExportsVideoAndHonorsNoImage() {
        const auto project = sampleProject();
        QTemporaryDir directory;
        const auto input = directory.filePath("input.edithere");
        saveBytes(input, serializeVideoProject(project));
#ifdef Q_OS_WIN
        const auto executable = QCoreApplication::applicationDirPath() + "/edithere-cli.exe";
#else
        const auto executable = QCoreApplication::applicationDirPath() + "/edithere-cli";
#endif
        for (const bool embed : {true, false}) {
            const auto output = directory.filePath(embed ? "embedded.json" : "without-images.json");
            QStringList arguments{"export", input, "--output", output};
            if (!embed)
                arguments.append("--no-image");
            QProcess process;
            process.start(executable, arguments);
            QVERIFY2(process.waitForStarted(), qPrintable(process.errorString()));
            QVERIFY(process.waitForFinished(15000));
            QCOMPARE(process.exitCode(), 0);
            const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
            QVERIFY(result["ok"].toBool());
            QCOMPARE(result["annotations"].toInt(), 2);
            QCOMPARE(result["frames"].toInt(), 2);
            QCOMPARE(result["imageIncluded"].toBool(), embed);
            QCOMPARE(result["schemaVersion"].toString(), QString("video-feedback-1"));
            QFile file(output);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto feedback = QJsonDocument::fromJson(file.readAll()).object();
            for (const auto &frame : feedback["frames"].toArray())
                QCOMPARE(frame.toObject()["feedback"].toObject().contains("image"), embed);
        }
    }
};
QTEST_GUILESS_MAIN(VideoProjectTests)
#include "video_project_test.moc"
