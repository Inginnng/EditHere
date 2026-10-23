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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QThreadPool>
#include <QTranslator>
#include <QUrl>
#include <QVideoSink>
#include <QWindow>
#include <functional>
using namespace h2d;
namespace {
QPointF timelineTagTip(QSlider *timeline, qint64 timestampUs, qint64 durationMs) {
    QStyleOptionSlider option;
    option.initFrom(timeline);
    option.orientation = Qt::Horizontal;
    option.minimum = timeline->minimum();
    option.maximum = timeline->maximum();
    option.sliderPosition = option.sliderValue = qRound(double(timestampUs / 1000) / durationMs * option.maximum);
    const QRect handle = timeline->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, timeline);
    const QRect groove = timeline->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, timeline);
    return {handle.left() + handle.width() / 2.0, groove.center().y() - 2.0};
}
bool isTimelineTagPixel(QColor color) {
    return color.green() - color.red() > 45 && color.blue() - color.red() > 45 &&
           qAbs(color.green() - color.blue()) < 45;
}
QImage timelineRender(QSlider *timeline) {
    return timeline->grab().toImage().scaled(timeline->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
QRect timelineTagPixels(const QImage &image, QPointF tip) {
    QRect bounds;
    const QRect sample = QRect(qRound(tip.x()) - 8, 0, 17, qRound(tip.y()) + 2).intersected(image.rect());
    for (int y = sample.top(); y <= sample.bottom(); ++y)
        for (int x = sample.left(); x <= sample.right(); ++x)
            if (isTimelineTagPixel(image.pixelColor(x, y))) bounds |= QRect(x, y, 1, 1);
    return bounds;
}
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
    void clearingVideoDoesNotRestoreAnInFlightPreview() {
        VideoPlayback playback;
        const auto view = playback.videoWidget();
        view->setParent(&playback);
        view->resize(128, 72);
        playback.show();
        view->show();
        const auto sink = playback.findChild<QVideoSink *>();
        QVERIFY(sink);

        QImage image(128, 72, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVideoFrame frame(image);
        frame.setStartTime(1000000);
        sink->setVideoFrame(frame);
        // Do not process UI events until after clearing. Even a worker that
        // completes immediately still has an undelivered completion callback.
        playback.clear();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        QCOMPARE(view->grab().toImage().pixelColor(64, 36), QColor(Qt::black));

        image.fill(Qt::blue);
        frame = QVideoFrame(image);
        frame.setStartTime(2000000);
        sink->setVideoFrame(frame);
        QTRY_COMPARE(view->grab().toImage().pixelColor(64, 36), QColor(Qt::blue));
    }
    void sourceMissingStillRestoresScreenshots_data() {
        QTest::addColumn<bool>("windowsPath");
        QTest::newRow("native-missing-file") << false;
        QTest::newRow("missing-windows-drive") << true;
    }
    void sourceMissingStillRestoresScreenshots() {
        QFETCH(bool, windowsPath);
        QTemporaryDir missingSource;
        QVERIFY(missingSource.isValid());
        QImage image(320, 180, QImage::Format_RGB32); image.fill(Qt::white);
        auto doc = fromImage(image, "file", "saved frame");
        Note note; note.point = {50, 40}; note.comment = "Keep this label"; doc.notes.append(note);
        VideoProject project;
        project.source = windowsPath ? QStringLiteral("Z:/does-not-exist/video.mp4")
                                     : missingSource.filePath(QStringLiteral("missing-video.mp4"));
        project.durationMs = 20000;
        project.frames.append({12540000, doc});
        Editor editor; editor.setVideoProject(project);
        QVERIFY(editor.hasVideo()); QCOMPARE(editor.document().image, doc.image);
        // Missing local sources must not start an asynchronous media open that
        // later replaces the already restored screenshot's review state.
        QVERIFY(editor.findChild<QMediaPlayer *>());
        QVERIFY(editor.findChild<QMediaPlayer *>()->source().isEmpty());
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
    void timelineTagsPointAtTrackAndEnlarge_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void timelineTagsPointAtTrackAndEnlarge() {
        if (source().isEmpty()) QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise real decoding.");
        QFETCH(bool, dark);
        struct RestoreTheme { ~RestoreTheme() { applyTheme(ThemeMode::Light); } } restore;
        applyTheme(dark ? ThemeMode::Dark : ThemeMode::Light);
        VideoPlayback playback;
        playback.videoWidget()->setParent(&playback);
        playback.resize(850, 115);
        playback.show();
        QSignalSpy paused(&playback, &VideoPlayback::framePaused);
        QSignalSpy reviewed(&playback, &VideoPlayback::reviewRequested);
        playback.open(source());
        QTRY_VERIFY_WITH_TIMEOUT(!paused.isEmpty() && !playback.positioning(), 15000);
        auto timeline = playback.findChild<QSlider *>("videoTimeline");
        QVERIFY(timeline && timeline->isEnabled());
        QVERIFY(playback.durationMs() > 100000);
        const qint64 middlePts = playback.durationMs() / 2 * 1000;
        const qint64 endPts = (playback.durationMs() - 1) * 1000;
        playback.setAnnotatedFrames({{0, 1}, {middlePts, 3}, {endPts, 1}});
        QTest::qWait(100);
        const QImage normal = timelineRender(timeline);
        const auto tip = timelineTagTip(timeline, middlePts, playback.durationMs());
        const QRect tag = timelineTagPixels(normal, tip);
        QVERIFY2(!tag.isEmpty(), "Annotation marker must render as a cyan tag above the groove");
        QVERIFY(tag.height() >= 11 && tag.height() <= 15);
        QVERIFY(tag.width() >= 8 && tag.width() <= 11);
        QVERIFY(qAbs(tag.center().x() - tip.x()) <= 1);
        QVERIFY(qAbs(tag.bottom() - tip.y()) <= 2);
        const auto rowWidth = [](const QImage &image, QRect area, int y) {
            int width = 0;
            for (int x = area.left(); x <= area.right(); ++x)
                if (isTimelineTagPixel(image.pixelColor(x, y))) ++width;
            return width;
        };
        QVERIFY(rowWidth(normal, tag, tag.bottom()) < rowWidth(normal, tag, tag.top() + 3));
        const QString artifacts = qEnvironmentVariable("EDITHERE_VIDEO_TEST_ARTIFACTS");
        const QString theme = dark ? "dark" : "light";
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(playback.grab().save(QDir(artifacts).filePath("timeline-tags-" + theme + "-normal.png")));
        }
        const QPointF body = tip - QPointF(0, 7);
        QMouseEvent hover(QEvent::MouseMove, body, timeline->mapToGlobal(body),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &hover);
        QCOMPARE(timeline->cursor().shape(), Qt::PointingHandCursor);
        QVERIFY(timeline->toolTip().contains(videoTimeLabel(middlePts / 1000)));
        const QImage enlarged = timelineRender(timeline);
        const QRect hovered = timelineTagPixels(enlarged, tip);
        QVERIFY(hovered.width() > tag.width());
        QVERIFY(hovered.height() > tag.height());
        QVERIFY(hovered.top() >= 0);
        QVERIFY(qAbs(hovered.bottom() - tag.bottom()) <= 1);
        if (!artifacts.isEmpty())
            QVERIFY(playback.grab().save(QDir(artifacts).filePath("timeline-tags-" + theme + "-hover.png")));
        QSignalSpy released(timeline, &QSlider::sliderReleased);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, body.toPoint());
        QCOMPARE(reviewed.size(), 1);
        QCOMPARE(reviewed.first().first().toLongLong(), middlePts);
        QCOMPARE(released.size(), 0);
        // The groove below the same timestamp remains a seek target.
        const QPointF track = tip + QPointF(0, 3);
        QMouseEvent overTrack(QEvent::MouseMove, track, timeline->mapToGlobal(track),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(timeline, &overTrack);
        QCOMPARE(timeline->cursor().shape(), Qt::ArrowCursor);
        QVERIFY(timeline->toolTip().isEmpty());
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, track.toPoint());
        QCOMPARE(reviewed.size(), 1);
        QCOMPARE(released.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!playback.positioning(), 15000);
        QVERIFY(qAbs(playback.positionMs() - middlePts / 1000) < 300);
        // Start/end markers fit inside the widget and remain clickable.
        for (const qint64 pts : {qint64(0), endPts}) {
            const auto endpoint = timelineTagTip(timeline, pts, playback.durationMs());
            const QRect pixels = timelineTagPixels(timelineRender(timeline), endpoint);
            QVERIFY(!pixels.isEmpty());
            QVERIFY(pixels.left() >= 0 && pixels.right() < timeline->width());
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, (endpoint - QPointF(0, 7)).toPoint());
            QCOMPARE(reviewed.last().first().toLongLong(), pts);
        }
        playback.setAnnotatedFrames({});
        QVERIFY(timeline->toolTip().isEmpty());
        QCOMPARE(timeline->cursor().shape(), Qt::ArrowCursor);
        QVERIFY(timelineTagPixels(timelineRender(timeline), tip).isEmpty());
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
        QSignalSpy captured(playback, &VideoPlayback::frameCaptured);
        editor.loadMedia(source());
        QTRY_VERIFY_WITH_TIMEOUT(!paused.isEmpty() && !playback->positioning(), 15000);
        QCOMPARE(paused.last()[0].toSize(), QSize(1920, 1080));
        QCOMPARE(playback->frameSize(), QSize(1920, 1080));
        QCOMPARE(editor.canvas()->imageSize(), QSize(1920, 1080));
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 0);
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
        QVERIFY(video->isVisible());

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
        const QVector<QWidget *> chrome{play, timeline, well, viewport, sidebar, canvas, video};
        const auto previewIssue = [&] {
            if (!video->isVisible()) return QString("Ordinary preview hid the player");
            if (editor.hasDocument() || !captured.isEmpty()) return QString("Ordinary preview captured a screenshot");
            return QString();
        };
        const int beforeSeek = paused.size();
        const qint64 oneSecondTarget = playback->positionMs() + 1000;
        {
            GeometryProbe probe(editor, chrome, sidebar, [&] {
                if (play->text() != pausedLabel) return QString("Paused seek flashed the play/pause label");
                if (playback->playing()) return QString("Paused seek exposed decoder activity as user playback");
                if (const auto issue = previewIssue(); !issue.isEmpty()) return issue;
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
            GeometryProbe probe(editor, chrome, sidebar, previewIssue);
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
            QTRY_VERIFY(video->isVisible());
            QVERIFY(!editor.hasDocument());
            QCOMPARE(captured.size(), 0);
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
                if (play->text() != pausedLabel || playback->playing())
                    return QString("Rapid seeks changed the user's paused state");
                return previewIssue();
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
        QVERIFY(video->isVisible());
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 0);
        // Saving/exporting an ordinary preview must not silently read back a
        // frame or turn an unannotated video into an exportable screenshot.
        QCOMPARE(editor.videoProject().frames.size(), 0);
        const auto feedback = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(feedback["frames"].toArray().size(), 0);
        QCOMPARE(feedback["objects"].toArray().size(), 0);
        QCOMPARE(QJsonDocument::fromJson(editor.projectBytes()).object()["frames"].toArray().size(), 0);
        QCOMPARE(captured.size(), 0);
    }
    void firstAnnotationCapturesOnlyOnce_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("point") << QString("point");
        QTest::newRow("rectangle") << QString("rectangle");
        QTest::newRow("global") << QString("global");
        QTest::newRow("global-shortcut") << QString("global-shortcut");
    }
    void firstAnnotationCapturesOnlyOnce() {
        if (source().isEmpty()) QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise real decoding.");
        QFETCH(QString, kind);
        QTemporaryDir feedbackFiles;
        QVERIFY(feedbackFiles.isValid());
        Editor editor;
        auto settings = defaultSettings(); settings.confirmBeforeDiscard = false;
        settings.feedbackDir = feedbackFiles.path();
        if (kind == "global-shortcut")
            settings.shortcuts["addGlobalNote"] = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G);
        editor.setPreferences(settings);
        editor.setShortcuts(settings.shortcuts);
        auto playback = editor.findChild<VideoPlayback *>(); QVERIFY(playback);
        QSignalSpy paused(playback, &VideoPlayback::framePaused);
        QSignalSpy captured(playback, &VideoPlayback::frameCaptured);
        editor.loadMedia(source());
        QTRY_VERIFY_WITH_TIMEOUT(!paused.isEmpty() && !playback->positioning(), 15000);
        QTest::qWait(150);
        auto canvas = editor.canvas();
        auto video = playback->videoWidget();
        auto well = static_cast<ImageArea *>(editor.findChild<QScrollArea *>("imageWell"));
        QVERIFY(well);
        QCOMPARE(canvas->imageSize(), QSize(1920, 1080));
        QVERIFY(video->isVisible());
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 0);
        const int beforeSeek = paused.size();
        playback->seek(65000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek && !playback->positioning() &&
            qAbs(paused.last()[1].toLongLong() / 1000 - 65000) < 150, 15000);
        QTest::qWait(80); // Let any already queued presentation frame finish settling.
        QCOMPARE(captured.size(), 0);
        QVERIFY(!editor.hasDocument());
        QVERIFY(video->isVisible());
        QCOMPARE(editor.videoProject().frames.size(), 0);
        QCOMPARE(QJsonDocument::fromJson(editor.agentFeedback(true)).object()["frames"].toArray().size(), 0);
        QCOMPARE(QJsonDocument::fromJson(editor.projectBytes()).object()["frames"].toArray().size(), 0);
        QCOMPARE(captured.size(), 0);

        const qint64 displayedPts = paused.last()[1].toLongLong();
        const double zoom = canvas->zoom();
        const QPointF origin = well->imageOrigin();
        const QRect imageBounds(canvas->mapTo(well->viewport(), QPoint()), canvas->size());
        const QPoint point(640, 360);
        const QRect rectangle(765, 800, 355, 90);
        QRect expectedRectangle;
        QPoint rectangleStart, rectangleEnd;
        if (kind == "point" || kind == "rectangle") {
            auto mode = editor.findChild<QPushButton *>(kind == "point" ? "mode_point" : "mode_rect");
            QVERIFY(mode && mode->isEnabled());
            mode->click();
            QCOMPARE(captured.size(), 0); // Selecting a tool only arms it.
            QVERIFY(!editor.hasDocument());
            if (kind == "point") {
                const QPoint canvasPoint = (QPointF(point) * zoom).toPoint();
                QTest::mouseClick(canvas, Qt::LeftButton, Qt::AltModifier, canvasPoint);
                QCOMPARE(captured.size(), 0); // The pan modifier must not begin an annotation.
                if (QGuiApplication::platformName() == "windows") {
                    QVERIFY(editor.windowHandle());
                    // Deliver through the native window's widget hit-testing,
                    // including the still-visible QVideoWidget overlay.
                    QTest::mouseClick(editor.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                                      canvas->mapTo(&editor, canvasPoint));
                    qInfo("First point annotation delivered through QWindow hit-testing");
                } else {
                    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint);
                    qInfo("First point annotation delivered directly to Canvas on non-Windows platform");
                }
            } else {
                rectangleStart = (QPointF(rectangle.topLeft()) * zoom).toPoint();
                // dragRect uses an exclusive right/bottom boundary. Account
                // exactly for the integer screen pixels sent to the widget,
                // instead of widening a tolerance around the nominal image box.
                rectangleEnd = (QPointF(rectangle.x() + rectangle.width(),
                                       rectangle.y() + rectangle.height()) * zoom).toPoint();
                const QPoint imageStart(qRound(rectangleStart.x() / zoom), qRound(rectangleStart.y() / zoom));
                const QPoint imageEnd(qRound(rectangleEnd.x() / zoom), qRound(rectangleEnd.y() / zoom));
                expectedRectangle = QRect(imageStart, QSize(imageEnd.x() - imageStart.x(),
                                                            imageEnd.y() - imageStart.y()));
                QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, rectangleStart);
                QCOMPARE(captured.size(), 1); // Read back before passing on the original press.
                QTest::mouseMove(canvas, rectangleEnd);
                QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, rectangleEnd);
            }
        } else {
            auto global = editor.findChild<QPushButton *>("addGlobalNote");
            QVERIFY(global && global->isEnabled());
            if (kind == "global-shortcut") {
                canvas->setFocus();
                QTest::keyClick(canvas, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
            } else global->click();
        }
        QTRY_COMPARE(editor.document().notes.size(), 1);
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first()[1].toLongLong(), displayedPts);
        QCOMPARE(editor.document().image,
                 qvariant_cast<QImage>(captured.first()[0]).convertToFormat(editor.document().image.format()));
        QCOMPARE(editor.document().image.size(), QSize(1920, 1080));
        QVERIFY(editor.hasDocument());
        QVERIFY(!video->isVisible());
        QCOMPARE(canvas->zoom(), zoom);
        QCOMPARE(well->imageOrigin(), origin);
        QCOMPARE(QRect(canvas->mapTo(well->viewport(), QPoint()), canvas->size()), imageBounds);
        const auto note = editor.document().notes.first();
        if (kind == "point") {
            QVERIFY(note.isPoint && !note.isGlobal);
            QVERIFY(qAbs(note.point.x() - point.x()) <= 1 && qAbs(note.point.y() - point.y()) <= 1);
        } else if (kind == "rectangle") {
            QVERIFY(!note.isPoint && !note.isGlobal);
            const QString detail = QString("actual=(%1,%2,%3,%4), expected quantized=(%5,%6,%7,%8), "
                                           "nominal=(%9,%10,%11,%12), zoom=%13, screen=(%14,%15)->(%16,%17)")
                .arg(note.rect.x()).arg(note.rect.y()).arg(note.rect.width()).arg(note.rect.height())
                .arg(expectedRectangle.x()).arg(expectedRectangle.y()).arg(expectedRectangle.width()).arg(expectedRectangle.height())
                .arg(rectangle.x()).arg(rectangle.y()).arg(rectangle.width()).arg(rectangle.height()).arg(zoom, 0, 'g', 16)
                .arg(rectangleStart.x()).arg(rectangleStart.y()).arg(rectangleEnd.x()).arg(rectangleEnd.y());
            QVERIFY2(note.rect == expectedRectangle, qPrintable(detail));
        } else QVERIFY(note.isGlobal);
        auto firstEdit = editor.findChild<QPlainTextEdit *>("noteText_" + note.id);
        QVERIFY(firstEdit); firstEdit->setPlainText("First annotation retains its original press and PTS");
        const auto firstFeedback = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(firstFeedback["frames"].toArray().size(), 1);
        QCOMPARE(firstFeedback["objects"].toArray().size(), 1);
        QCOMPARE(firstFeedback["frames"].toArray().first().toObject()["timestampUs"].toInteger(), displayedPts);
        QCOMPARE(firstFeedback["objects"].toArray().first().toObject()["timestampMs"].toInteger(), displayedPts / 1000);

        // A second annotation on the captured frame reuses its image.
        editor.findChild<QPushButton *>("addGlobalNote")->click();
        QTRY_COMPARE(editor.document().notes.size(), 2);
        auto secondEdit = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(secondEdit); secondEdit->setPlainText("Second annotation reuses the frame");
        const auto savedFeedback = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(savedFeedback["objects"].toArray().size(), 2);
        QCOMPARE(captured.size(), 1);
        const int beforeReplayPause = paused.size();
        playback->toggle();
        QTRY_VERIFY(playback->playing() && video->isVisible());
        QVERIFY(!editor.hasDocument());
        QTest::qWait(180);
        playback->pause();
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeReplayPause && !playback->playing(), 5000);
        QVERIFY(video->isVisible());
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 1);
        const int beforeSecondSeek = paused.size();
        playback->seek(105000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSecondSeek && !playback->positioning() &&
            qAbs(paused.last()[1].toLongLong() / 1000 - 105000) < 150, 15000);
        QVERIFY(video->isVisible());
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 1);
        const auto unchanged = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(unchanged["frames"], savedFeedback["frames"]);
        QCOMPARE(unchanged["objects"], savedFeedback["objects"]);
        QCOMPARE(editor.videoProject().frames.size(), 1);
        QCOMPARE(QJsonDocument::fromJson(editor.projectBytes()).object()["frames"].toArray().size(), 1);
        QCOMPARE(captured.size(), 1);
        if (kind == "point") {
            // Exercise the actual toolbar exports while preview has no image
            // document. A hasDocument() guard must not discard saved video notes.
            auto copyText = editor.findChild<QPushButton *>("copyJsonText");
            auto copyFile = editor.findChild<QPushButton *>("copyJson");
            QVERIFY(copyText && copyFile && copyText->isEnabled() && copyFile->isEnabled());
            QApplication::clipboard()->clear();
            QTest::qWait(150); // Allow Windows OLE ownership messages to settle.
            copyText->click();
            const auto textFeedback = QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
            QCOMPARE(textFeedback["schemaVersion"].toString(), QString("video-feedback-1"));
            QCOMPARE(textFeedback["frames"].toArray().size(), 1);
            QCOMPARE(textFeedback["objects"], savedFeedback["objects"]);
            QCOMPARE(captured.size(), 1);
            QVERIFY(video->isVisible());
            QVERIFY(!editor.hasDocument());

            QApplication::clipboard()->clear();
            QTest::qWait(150);
            QString copiedPath;
            const QString feedbackPrefix = QDir::cleanPath(feedbackFiles.path()) + '/';
            // Another application can briefly hold the Windows clipboard OLE
            // lock (0x800401d0). Retry only this actual toolbar action, with a
            // short fixed bound; never inspect another application's file URL.
            for (int attempt = 0; attempt < 5 && copiedPath.isEmpty(); ++attempt) {
                copyFile->click();
                QTest::qWait(150);
                const auto mime = QApplication::clipboard()->mimeData();
                if (!mime || !mime->hasUrls() || mime->urls().size() != 1 ||
                    !mime->urls().first().isLocalFile()) continue;
                const QString candidate = QDir::cleanPath(QFileInfo(mime->urls().first().toLocalFile()).absoluteFilePath());
                if (candidate.startsWith(feedbackPrefix, Qt::CaseInsensitive)) copiedPath = candidate;
            }
            QVERIFY2(!copiedPath.isEmpty(), "Toolbar JSON copy did not provide this test's file URL within five attempts");
            QFile copiedFile(copiedPath);
            QVERIFY(copiedFile.open(QIODevice::ReadOnly));
            const auto fileFeedback = QJsonDocument::fromJson(copiedFile.readAll()).object();
            QCOMPARE(fileFeedback["schemaVersion"].toString(), QString("video-feedback-1"));
            QCOMPARE(fileFeedback["frames"].toArray().size(), 1);
            QCOMPARE(fileFeedback["objects"], savedFeedback["objects"]);
            QCOMPARE(captured.size(), 1);
            QVERIFY(video->isVisible());
            QVERIFY(!editor.hasDocument());
        }
    }
    void playbackSeekAnnotateAndReopen() {
        if (source().isEmpty()) QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise real decoding.");
        try {
        Editor editor;
        auto settings = defaultSettings(); settings.confirmBeforeDiscard = false; editor.setPreferences(settings);
        auto playback = editor.findChild<VideoPlayback *>(); QVERIFY(playback);
        QSignalSpy paused(playback, &VideoPlayback::framePaused);
        QSignalSpy captured(playback, &VideoPlayback::frameCaptured);
        editor.loadMedia(source());
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() >= 1 && !playback->positioning(), 15000);
        QCOMPARE(playback->frameSize(), QSize(1920, 1080));
        QVERIFY(!editor.hasDocument());
        QCOMPARE(captured.size(), 0);
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
        QCOMPARE(captured.size(), 0);
        const qint64 firstPts = paused.last()[1].toLongLong();
        rectangleNote(editor, {765, 800, 355, 90}, QString::fromUtf8("将底部“只有像素。”这行文字放大 20%，并向上移动 30 像素。"));
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.first()[1].toLongLong(), firstPts);
        QCOMPARE(editor.document().image.size(), QSize(1920, 1080));
        const auto firstFrame = editor.videoProject().frames.first();
        const int oldCount = paused.size();
        auto playButton = editor.findChild<QPushButton *>("videoPlayPause");
        QTest::mouseClick(playButton, Qt::LeftButton);
        QTRY_VERIFY(playback->playing());
        QTest::qWait(200);
        QTest::mouseClick(playButton, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > oldCount, 5000);
        QVERIFY(!editor.hasDocument());
        QVERIFY(playback->videoWidget()->isVisible());
        QCOMPARE(captured.size(), 1);
        QCOMPARE(editor.videoProject().frames.size(), 1);
        const int beforeSeek = paused.size();
        playback->seek(105000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek && paused.last()[1].toLongLong() > 104000000, 15000);
        QCOMPARE(captured.size(), 1);
        const qint64 secondPts = paused.last()[1].toLongLong();
        pointNote(editor, {1188, 528}, QString::fromUtf8("把红色牛奶盒换成蓝色包装，保留位置和大小。"));
        QCOMPARE(captured.size(), 2);
        QCOMPARE(captured.last()[1].toLongLong(), secondPts);
        rectangleNote(editor, {325, 593, 143, 55}, QString::fromUtf8("把左侧“第 3 轮”标签改成“第 3 次修改”，保持原来的灰白色。"));
        QCOMPARE(captured.size(), 2);
        const auto json = QJsonDocument::fromJson(editor.agentFeedback(true)).object();
        QCOMPARE(json["frames"].toArray().size(), 2);
        QCOMPARE(json["objects"].toArray().size(), 3);
        QVERIFY(json["objects"].toArray()[0].toObject()["timestampMs"].toInteger() < json["objects"].toArray()[1].toObject()["timestampMs"].toInteger());
        auto choices = editor.findChild<QComboBox *>("videoAnnotatedFrames");
        QCOMPARE(choices->count(), 3);
        {
            // The cyan timeline tags open their annotated frame. Leave the
            // second frame first so the click demonstrably returns somewhere.
            const int beforeLeave = paused.size();
            playback->seek(30000);
            QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeLeave && !playback->positioning(), 15000);
            QVERIFY(!editor.hasDocument());
            const QPointF marker = timelineTagTip(timeline, firstPts, playback->durationMs()) - QPointF(0, 7);
            QMouseEvent hover(QEvent::MouseMove, marker, timeline->mapToGlobal(marker),
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &hover);
            QCOMPARE(timeline->cursor().shape(), Qt::PointingHandCursor);
            QVERIFY(timeline->toolTip().contains(videoTimeLabel(firstPts / 1000)));
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, marker.toPoint());
            QTRY_VERIFY(editor.hasDocument());
            QCOMPARE(editor.document().image, firstFrame.document.image);
            QCOMPARE(editor.document().notes.first().comment, firstFrame.document.notes.first().comment);
            QCOMPARE(choices->currentData().toLongLong(), firstPts);
            QVERIFY(!playback->videoWidget()->isVisible());
            QCOMPARE(captured.size(), 2);
            // Annotations must stay ordinary child widgets of the editor window;
            // a native video surface would hide every mark drawn on the canvas.
            QCOMPARE(editor.canvas()->internalWinId(), WId(0));
            QCOMPARE(playback->videoWidget()->internalWinId(), WId(0));
        }
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
