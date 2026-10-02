#include "editor.h"
#include "imagearea.h"
#include "videoplayback.h"
#include "ui.h"
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>
#include <functional>
using namespace h2d;
namespace {
// Observe the whole asynchronous transition. A final screenshot alone misses a
// one-frame label flash or a sidebar/viewport layout that jumps and settles back.
class GeometryProbe final : public QObject {
  public:
    GeometryProbe(Editor &editor, const QVector<QWidget *> &widgets,
                  QWidget *sidebar, const std::function<QString()> &extra = {})
        : editor_(editor), widgets_(widgets), sidebar_(sidebar), extra_(extra) {
        for (auto widget : widgets_) {
            bounds_.append(bounds(widget));
            widget->installEventFilter(this);
        }
        sidebarHidden_ = sidebar_->isHidden();
        sidebar_->installEventFilter(this);
        connect(&timer_, &QTimer::timeout, this, [this] { inspect(); });
        timer_.start(1);
        inspect();
    }
    QString issue;
    int samples = 0;
    void inspect() {
        ++samples;
        if (!issue.isEmpty()) return;
        for (int i = 0; i < widgets_.size(); ++i)
            if (bounds(widgets_[i]) != bounds_[i]) {
                issue = QString("%1 moved or resized during playback/seek").arg(widgets_[i]->objectName());
                return;
            }
        if (sidebar_->isHidden() != sidebarHidden_) {
            issue = "The annotation sidebar changed visibility during playback/seek";
            return;
        }
        if (extra_) issue = extra_();
    }
  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
            event->type() == QEvent::Show || event->type() == QEvent::Hide)
            inspect();
        return false;
    }
  private:
    QRect bounds(QWidget *widget) const { return {widget->mapTo(&editor_, QPoint()), widget->size()}; }
    Editor &editor_;
    QVector<QWidget *> widgets_;
    QVector<QRect> bounds_;
    QWidget *sidebar_;
    bool sidebarHidden_ = false;
    std::function<QString()> extra_;
    QTimer timer_;
};
}
class VideoUiTests : public QObject {
    Q_OBJECT
  private:
    QString source() { return qEnvironmentVariable("EDITHERE_VIDEO_TEST_SOURCE"); }
    bool saveWindowArtifact(Editor &editor, const QString &name) {
        const QString folder = qEnvironmentVariable("EDITHERE_VIDEO_TEST_ARTIFACTS");
        if (folder.isEmpty()) return true;
        if (!QDir().mkpath(folder)) return false;
        const auto hasContent = [](const QPixmap &pixmap) {
            if (pixmap.isNull()) return false;
            const QImage image = pixmap.toImage();
            const QRgb first = image.pixel(0, 0);
            const int xStep = qMax(1, image.width() / 32);
            const int yStep = qMax(1, image.height() / 32);
            for (int y = 0; y < image.height(); y += yStep)
                for (int x = 0; x < image.width(); x += xStep)
                    if (image.pixel(x, y) != first) return true;
            return false;
        };
        // Some Windows automation sessions return an entirely black native
        // window capture. Keep a real widget rendering of the chrome in that
        // case; it does not establish that the GPU video surface was captured.
        QPixmap capture;
        QString captureMethod = "native window capture";
        if (QGuiApplication::platformName() != "offscreen" && editor.screen())
            capture = editor.screen()->grabWindow(editor.winId());
        if (!hasContent(capture)) {
            capture = editor.grab();
            captureMethod = "Qt widget rendering (native capture unavailable or uniform; GPU surface not verified by screenshot)";
        }
        qInfo().noquote() << name << ":" << captureMethod;
        if (!hasContent(capture)) return false;
        return capture.save(QDir(folder).filePath(name + ".png"));
    }
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
    void playbackViewportAndSeekRemainStable_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void playbackViewportAndSeekRemainStable() {
        if (source().isEmpty()) QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise real decoding.");
        QFETCH(bool, dark);
        struct RestoreTheme { ~RestoreTheme() { applyTheme(ThemeMode::Light); } } restore;
        applyTheme(dark ? ThemeMode::Dark : ThemeMode::Light);
        Editor editor;
        auto settings = defaultSettings();
        settings.theme = dark ? ThemeMode::Dark : ThemeMode::Light;
        settings.confirmBeforeDiscard = false;
        editor.setPreferences(settings);
        auto playback = editor.findChild<VideoPlayback *>();
        QVERIFY(playback);
        QSignalSpy paused(playback, &VideoPlayback::framePaused);
        editor.loadMedia(source());
        QTRY_VERIFY_WITH_TIMEOUT(!paused.isEmpty() && editor.hasDocument(), 15000);
        QTest::qWait(150); // Initial fit and the window's first layout must settle.
        auto well = static_cast<ImageArea *>(editor.findChild<QScrollArea *>("imageWell"));
        auto sidebar = editor.findChild<QWidget *>("detailsStack");
        auto play = editor.findChild<QPushButton *>("videoPlayPause");
        auto forward = editor.findChild<QPushButton *>("videoSeekForward");
        auto backward = editor.findChild<QPushButton *>("videoSeekBack");
        auto timeline = editor.findChild<QSlider *>("videoTimeline");
        auto collapse = editor.findChild<QPushButton *>("collapseNotes");
        QVERIFY(well && sidebar && play && forward && backward && timeline && collapse);
        auto viewport = well->viewport();
        auto video = playback->videoWidget();
        auto canvas = editor.canvas();
        QCOMPARE(video->parentWidget(), viewport);

        // A paused seek must preserve a deliberately non-fitted and panned view,
        // including the user's collapsed sidebar, throughout decoder activity.
        collapse->click();
        QTRY_VERIFY(sidebar->isHidden());
        editor.findChild<QPushButton *>("zoomOut")->click();
        QTest::qWait(80);
        well->setImageOrigin(well->imageOrigin() + QPointF(21, -17));
        QTest::qWait(40);
        const double savedZoom = canvas->zoom();
        const QPointF savedOrigin = well->imageOrigin();
        const QString pausedLabel = play->text();
        const QVector<QWidget *> chrome{play, timeline, well, viewport, sidebar, canvas};
        const int beforeSeek = paused.size();
        const qint64 oneSecondTarget = playback->positionMs() + 1000;
        {
            GeometryProbe probe(editor, chrome, sidebar, [&] {
                if (play->text() != pausedLabel) return QString("Paused seek flashed the play/pause label");
                if (playback->playing()) return QString("Paused seek exposed decoder activity as user playback");
                if (canvas->zoom() != savedZoom || well->imageOrigin() != savedOrigin)
                    return QString("Paused seek reset image zoom or origin");
                return QString();
            });
            QTest::mouseClick(forward, Qt::LeftButton);
            probe.inspect();
            QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek &&
                qAbs(paused.last()[1].toLongLong() / 1000 - oneSecondTarget) < 150, 15000);
            QTest::qWait(80);
            QVERIFY2(probe.issue.isEmpty(), qPrintable(probe.issue));
            QVERIFY(probe.samples > 2);
        }
        QCOMPARE(canvas->zoom(), savedZoom);
        QCOMPARE(well->imageOrigin(), savedOrigin);
        QVERIFY(sidebar->isHidden());

        // Both surfaces occupy the same image rect, so viewport margins keep
        // their theme colour instead of turning into a separate black player.
        const QRect canvasBounds(canvas->mapTo(viewport, QPoint()), canvas->size());
        QCOMPARE(video->geometry(), canvasBounds);
        QPoint margin;
        bool foundMargin = false;
        for (const QPoint point : {QPoint(12, 12), QPoint(viewport->width() - 13, 12),
                                   QPoint(12, viewport->height() - 13)})
            if (!canvasBounds.contains(point)) { margin = point; foundMargin = true; break; }
        QVERIFY2(foundMargin, "The test must sample an actual viewport margin outside the image");
        auto marginColour = [viewport, margin] {
            const auto pixmap = viewport->grab();
            const auto image = pixmap.toImage();
            const QPoint pixel(qRound(margin.x() * pixmap.devicePixelRatio()),
                               qRound(margin.y() * pixmap.devicePixelRatio()));
            return image.pixelColor(pixel);
        };
        const QColor expectedWell = dark ? QColor("#18191e") : QColor("#efeff2");
        QCOMPARE(marginColour(), expectedWell);
        const int beforePause = paused.size();
        {
            GeometryProbe probe(editor, chrome, sidebar);
            QTest::mouseClick(play, Qt::LeftButton);
            QTRY_VERIFY(playback->playing() && video->isVisible());
            QCOMPARE(video->geometry(), canvasBounds);
            QVERIFY(play->text() != pausedLabel);
            QTest::qWait(180);
            QCOMPARE(marginColour(), expectedWell);
            QVERIFY(saveWindowArtifact(editor, dark ? "video-layout-dark-playing" : "video-layout-light-playing"));
            // Space works while the shared video surface owns keyboard focus.
            video->setFocus();
            QTest::keyClick(video, Qt::Key_Space);
            QTRY_VERIFY_WITH_TIMEOUT(!playback->playing() && paused.size() > beforePause, 5000);
            QTRY_VERIFY(!video->isVisible());
            QCOMPARE(play->text(), pausedLabel);
            QTest::qWait(80);
            QCOMPARE(marginColour(), expectedWell);
            QVERIFY(saveWindowArtifact(editor, dark ? "video-layout-dark-paused" : "video-layout-light-paused"));
            QVERIFY2(probe.issue.isEmpty(), qPrintable(probe.issue));
        }

        // Quick relative seeks must accumulate against the requested position,
        // not repeatedly use the older displayed frame while it is decoding.
        const qint64 burstStart = playback->positionMs();
        const int beforeBurst = paused.size();
        {
            GeometryProbe probe(editor, chrome, sidebar, [&] {
                return play->text() == pausedLabel && !playback->playing()
                    ? QString() : QString("Rapid seeks changed the user's paused state");
            });
            QTest::mouseClick(forward, Qt::LeftButton);
            QTest::mouseClick(forward, Qt::LeftButton);
            QTest::mouseClick(forward, Qt::LeftButton);
            QTest::mouseClick(backward, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeBurst &&
                qAbs(paused.last()[1].toLongLong() / 1000 - (burstStart + 2000)) < 150, 15000);
            QTest::qWait(80);
            QVERIFY2(probe.issue.isEmpty(), qPrintable(probe.issue));
        }
        QCOMPARE(canvas->zoom(), savedZoom);
        QCOMPARE(well->imageOrigin(), savedOrigin);

        // Seeking while playing intentionally leaves the newly requested frame
        // paused for annotation; a subsequent Space resumes from that frame.
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Space);
        QTRY_VERIFY(playback->playing());
        const qint64 reverseTarget = playback->positionMs() - 1000;
        const int beforeReverse = paused.size();
        QTest::mouseClick(backward, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!playback->playing() && paused.size() > beforeReverse &&
            qAbs(paused.last()[1].toLongLong() / 1000 - reverseTarget) < 150, 15000);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Space);
        QTRY_VERIFY(playback->playing());
        QTest::qWait(120);
        const int beforeSpacePause = paused.size();
        video->setFocus();
        QTest::keyClick(video, Qt::Key_Space);
        QTRY_VERIFY_WITH_TIMEOUT(!playback->playing() && paused.size() > beforeSpacePause, 5000);
        QCOMPARE(play->text(), pausedLabel);
        QCOMPARE(video->geometry(), QRect(canvas->mapTo(viewport, QPoint()), canvas->size()));
        QCOMPARE(canvas->zoom(), savedZoom);
        QCOMPARE(well->imageOrigin(), savedOrigin);
        QVERIFY(sidebar->isHidden());
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
        auto restoredPlay = editor.findChild<QPushButton *>("videoPlayPause");
        QTRY_VERIFY_WITH_TIMEOUT(!playback->playing() && !playback->positioning() && restoredPlay->isEnabled(), 10000);
        restoredPlay->click();
        QTRY_VERIFY_WITH_TIMEOUT(playback->playing(), 10000);
        playback->pause();
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
    }
};
QTEST_MAIN(VideoUiTests)
#include "video_ui_test.moc"
