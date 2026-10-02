#include "editor.h"
#include "videoplayback.h"
#include "ui.h"
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>
using namespace h2d;
class VideoUiTests : public QObject {
    Q_OBJECT
  private:
    QString source() { return qEnvironmentVariable("EDITHERE_VIDEO_TEST_SOURCE"); }
    void writeArtifacts(Editor &editor, const QString &name) {
        const QString folder = qEnvironmentVariable("EDITHERE_VIDEO_TEST_ARTIFACTS");
        if (folder.isEmpty()) return;
        QDir().mkpath(folder);
        saveBytes(QDir(folder).filePath(name + ".json"), editor.agentFeedback(true));
        saveBytes(QDir(folder).filePath(name + ".edithere"), editor.projectBytes());
        editor.canvas()->setMagnifierEnabled(false);
        QTest::qWait(150);
        editor.grab().save(QDir(folder).filePath(name + "-ui.png"));
        for (const auto &frame : editor.videoProject().frames) {
            frame.document.image.save(QDir(folder).filePath("frame-" + frame.document.id + ".png"));
            previewImage(frame.document).save(QDir(folder).filePath("annotations-" + frame.document.id + ".png"));
        }
    }
    void pointNote(Editor &editor, QPoint imagePoint, const QString &comment) {
        editor.findChild<QPushButton *>("mode_point")->click();
        auto canvas = editor.canvas();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(imagePoint) * canvas->zoom()).toPoint());
        QTRY_VERIFY(!editor.document().notes.isEmpty());
        const QString id = editor.document().notes.last().id;
        auto edit = editor.findChild<QPlainTextEdit *>("noteText_" + id);
        QVERIFY(edit); edit->setPlainText(comment);
        editor.agentFeedback(true); // Commit via the same export boundary used by AI.
    }
    void rectangleNote(Editor &editor, QRect imageRect, const QString &comment) {
        editor.findChild<QPushButton *>("mode_rect")->click();
        auto canvas = editor.canvas();
        const QPoint start = (QPointF(imageRect.topLeft()) * canvas->zoom()).toPoint();
        const QPoint end = (QPointF(imageRect.bottomRight()) * canvas->zoom()).toPoint();
        const auto count = editor.document().notes.size();
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(editor.document().notes.size(), count + 1);
        auto edit = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(edit); edit->setPlainText(comment); editor.agentFeedback(true);
    }
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName("EditHereVideoTests");
        applyTheme(ThemeMode::Light);
    }
    void sourceMissingStillRestoresScreenshots() {
        QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::white);
        auto doc = fromImage(image, "file", "saved frame");
        Note note; note.point = {50, 40}; note.comment = "Keep this label"; doc.notes.append(note);
        VideoProject project; project.source = "Z:/does-not-exist/video.mp4"; project.durationMs = 20000;
        project.frames.append({12540000, doc});
        Editor editor; editor.setVideoProject(project);
        QVERIFY(editor.hasVideo()); QCOMPARE(editor.document().image, doc.image);
        QCOMPARE(editor.totalAnnotationCount(), 1);
        QVERIFY(editor.findChild<QComboBox *>("videoAnnotatedFrames")->count() == 2);
        QTranslator english;
        QVERIFY(english.load(QFINDTESTDATA("../app/translations/built/edithere_en.qm")));
        QVERIFY(QApplication::installTranslator(&english));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
        QCOMPARE(editor.findChild<QPushButton *>("videoPlayPause")->text(), QString("Play"));
        QCOMPARE(editor.findChild<QComboBox *>("videoAnnotatedFrames")->itemText(0), QString("Annotated frames: 1"));
        QVERIFY(editor.findChild<QLabel *>("videoStatus")->text().startsWith("Saved frame"));
        QVERIFY(QApplication::removeTranslator(&english));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LanguageChange);
        QCOMPARE(editor.findChild<QComboBox *>("videoAnnotatedFrames")->itemText(0), QString::fromUtf8("已标注画面 1 个"));
        const auto output = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(output["frames"].toArray().size(), 1);
        editor.setDocument(fromImage(image, "file", "ordinary image"));
        QVERIFY(!editor.hasVideo());
        QVERIFY(!QJsonDocument::fromJson(editor.agentFeedback(true)).object().contains("frames"));
    }
    void playbackSeekAnnotateAndReopen() {
        if (source().isEmpty()) QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise real decoding.");
        try {
        Editor editor;
        auto settings = defaultSettings(); settings.confirmBeforeDiscard = false; editor.setPreferences(settings);
        auto playback = editor.findChild<VideoPlayback *>(); QVERIFY(playback);
        QSignalSpy paused(playback, &VideoPlayback::framePaused);
        editor.loadMedia(source());
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() >= 1 && !editor.document().image.isNull(), 15000);
        QCOMPARE(editor.document().image.size(), QSize(1920, 1080));
        QVERIFY(playback->durationMs() > 100000);
        QVERIFY(!playback->playing());
        QVERIFY(editor.findChild<QSlider *>("videoTimeline")->isEnabled());
        const qint64 first = paused.last()[1].toLongLong();
        QVERIFY(first >= 0);
        playback->seek(65000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.last()[1].toLongLong() > 64000000, 15000);
        // Exercise the actual draggable timeline, not just the player API.
        auto timeline = editor.findChild<QSlider *>("videoTimeline");
        const int timelineCount = paused.size();
        const int timelineX = 9 + qRound(65000.0 / playback->durationMs() * (timeline->width() - 18));
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, {timelineX, timeline->height() / 2});
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > timelineCount, 15000);
        QVERIFY(qAbs(paused.last()[1].toLongLong() / 1000 - 65000) < 300);
        QCOMPARE(playback->positionMs(), paused.last()[1].toLongLong() / 1000);
        rectangleNote(editor, {765, 800, 355, 90}, QString::fromUtf8("将底部“只有像素。”这行文字放大 20%，并向上移动 30 像素。"));
        const auto firstFrame = editor.videoProject().frames.first();
        const int oldCount = paused.size();
        auto playButton = editor.findChild<QPushButton *>("videoPlayPause");
        QTest::mouseClick(playButton, Qt::LeftButton);
        QTRY_VERIFY(playback->playing());
        QTest::qWait(200);
        QTest::mouseClick(playButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > oldCount, 5000);
        const int beforeSeek = paused.size();
        playback->seek(105000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek && paused.last()[1].toLongLong() > 104000000, 15000);
        pointNote(editor, {1188, 528}, QString::fromUtf8("把红色牛奶盒换成蓝色包装，保留位置和大小。"));
        rectangleNote(editor, {325, 593, 143, 55}, QString::fromUtf8("把左侧“第 3 轮”标签改成“第 3 次修改”，保持原来的灰白色。"));
        const auto json = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(json["frames"].toArray().size(), 2);
        QCOMPARE(json["objects"].toArray().size(), 3);
        QVERIFY(json["objects"].toArray()[0].toObject()["timestampMs"].toInteger() < json["objects"].toArray()[1].toObject()["timestampMs"].toInteger());
        auto choices = editor.findChild<QComboBox *>("videoAnnotatedFrames");
        QCOMPARE(choices->count(), 3);
        QTemporaryDir bundle;
        QByteArray copied;
        bool bundleSaved = false;
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer::singleShot(50, &editor, [&] {
            auto dialog = editor.findChild<QDialog *>("feedbackDialog");
            if (!dialog) return;
            dialog->findChild<QPushButton *>("copyJsonText")->click();
            copied = QApplication::clipboard()->text().toUtf8();
            QTimer::singleShot(50, dialog, [&] {
                for (auto widget : QApplication::topLevelWidgets())
                    if (auto picker = qobject_cast<QFileDialog *>(widget)) {
                        picker->setDirectory(bundle.path());
                        QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
                    }
            });
            dialog->findChild<QPushButton *>("saveFeedbackBundle")->click();
            bundleSaved = !QDir(bundle.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
            dialog->accept();
        });
        editor.exportJson();
        QVERIFY(!copied.isEmpty()); QVERIFY(bundleSaved);
        const auto exported = QJsonDocument::fromJson(copied).object();
        QCOMPARE(exported["frames"].toArray().size(), 2);
        QCOMPARE(exported["objects"], json["objects"]);
        const auto folders = QDir(bundle.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const auto bundlePath = QDir(bundle.path()).filePath(folders.first());
        const auto imported = loadVideoProject(QDir(bundlePath).filePath("feedback.json"));
        QCOMPARE(imported.frames.size(), 2);
        const auto artifactFolder = qEnvironmentVariable("EDITHERE_VIDEO_TEST_ARTIFACTS");
        if (!artifactFolder.isEmpty()) {
            QVERIFY(QDir().mkpath(artifactFolder));
            saveBytes(QDir(artifactFolder).filePath("video-ui-export-compressed.json"), copied);
        }
        choices->setCurrentIndex(1); choices->activated(1);
        QCOMPARE(editor.document().image, firstFrame.document.image);
        QCOMPARE(editor.document().notes.first().comment, firstFrame.document.notes.first().comment);
        QTemporaryDir temporary;
        const QString projectPath = temporary.filePath("video.edithere");
        saveBytes(projectPath, editor.projectBytes());
        auto restored = loadVideoProject(projectPath);
        QCOMPARE(restored.frames.size(), 2);
        QCOMPARE(restored.frames.first().document.image, firstFrame.document.image);
        writeArtifacts(editor, "video-acceptance");
        editor.setVideoProject(restored, projectPath);
        QTRY_VERIFY(!editor.document().image.isNull());
        QCOMPARE(editor.videoProject().frames.size(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!playback->playing(), 10000);
        editor.findChild<QPushButton *>("videoPlayPause")->click();
        QTRY_VERIFY_WITH_TIMEOUT(playback->playing(), 10000);
        playback->pause();
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
    }
};
QTEST_MAIN(VideoUiTests)
#include "video_ui_test.moc"
