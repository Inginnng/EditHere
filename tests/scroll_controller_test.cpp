#include "capturetoolbar.h"
#include "controller.h"
#include "platform.h"
#include "scrollcapture.h"
#include "ui.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace h2d {
class ScrollControllerTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> temporary_;
    std::unique_ptr<Controller> owner_;
    std::function<void()> ready_;
    QList<ScrollRegionCallback> pending_;
    int wheelSteps_ = 0;
    int targetQueries_ = 0;
    int focusRestores_ = 0;
    QPoint targetPoint_;
    QRect capturedRegion_;

    static QImage document(int width = 240, int height = 360) {
        QImage image(width, height, QImage::Format_RGB32);
        quint32 random = 19;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                random = random * 1664525u + 1013904223u;
                image.setPixel(x, y, qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255));
            }
        return image;
    }
    Overlay *overlay(QRect selected = {40, 60, 240, 180}, ScreenFrame frame = {}) {
        if (frame.image.isNull()) {
            frame.image = QImage(640, 480, QImage::Format_RGB32);
            frame.image.fill(Qt::white);
            frame.logicalGeometry = {0, 0, 640, 480};
            frame.nativeGeometry = frame.logicalGeometry;
            frame.nativePixels = true;
        }
        auto *source = new Overlay(frame);
        owner_->overlays_.append(source);
        source->show();
        source->setSelections({selected});
        QTest::keyClick(source, Qt::Key_R);
        connect(source, &Overlay::scrollRequested, owner_.get(), [this, source](QRect) {
            owner_->startScrollCapture(source);
        });
        return source;
    }
    void begin(Overlay *source) {
        QTest::keyClick(source, Qt::Key_L);
        auto ready = std::move(ready_);
        if (ready)
            ready();
    }
    bool reply(const QImage &image, const QString &error = {}) {
        QElapsedTimer deadline;
        deadline.start();
        while (pending_.isEmpty() && deadline.elapsed() < 1500)
            QTest::qWait(10);
        if (pending_.isEmpty())
            return false;
        auto callback = pending_.takeFirst();
        callback(image, error);
        return true;
    }
    QPushButton *button(const char *name) {
        return owner_->scrollProgress_ ? owner_->scrollProgress_->findChild<QPushButton *>(name) : nullptr;
    }
    static QPushButton *captureButton(Overlay *source, const QString &name) {
        for (auto *candidate : source->findChildren<QPushButton *>())
            if (candidate->property("glyphName").toString() == name)
                return candidate;
        return nullptr;
    }

  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        applyTheme();
    }
    void init() {
        temporary_ = std::make_unique<QTemporaryDir>();
        QVERIFY(temporary_->isValid());
        AppSettings settings;
        settings.confirmBeforeDiscard = false;
        settings.checkUpdatesOnStartup = false;
        owner_ = std::make_unique<Controller>(nullptr, settings, temporary_->filePath("settings.json"));
        owner_->shortcut_.stop();
        owner_->history_ = CaptureHistory(temporary_->filePath("history"));
        owner_->capturing_ = true;
        wheelSteps_ = targetQueries_ = 0;
        focusRestores_ = 0;
        ready_ = {};
        pending_.clear();
        capturedRegion_ = {};
        owner_->scrollIo_.prepare = [this](QObject *, std::function<void()> callback) {
            ready_ = std::move(callback);
        };
        owner_->scrollIo_.supported = [](QString *) { return true; };
        owner_->scrollIo_.targetAt = [this](QPoint at) {
            ++targetQueries_;
            targetPoint_ = at;
            return ScrollCaptureTarget{42, 7};
        };
        owner_->scrollIo_.focus = [this](quintptr target) {
            QCOMPARE(target, quintptr(42));
            ++focusRestores_;
        };
        owner_->scrollIo_.step = [this](const ScrollCaptureTarget &target, QPoint at, int steps, QString *) {
            if (target.window != 42 || target.processId != 7 || at != targetPoint_ || steps != 1)
                return false;
            ++wheelSteps_;
            return true;
        };
        owner_->scrollIo_.grab = [this](const QRect &region, ScrollRegionCallback callback) {
            capturedRegion_ = region;
            pending_.append(std::move(callback));
        };
    }
    void cleanup() {
        if (owner_)
            owner_->clearOverlays();
        owner_.reset();
        // A deferred close belongs to the finished run, not the next test.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        ready_ = {};
        pending_.clear();
        temporary_.reset();
    }
    void toolbarMouseClickStartsScrollingWithoutSaving() {
        auto *source = overlay();
        auto *scroll = captureButton(source, QStringLiteral("scroll"));
        QVERIFY(scroll && scroll->isVisible() && scroll->isEnabled());
        const QPoint at = scroll->mapToGlobal(scroll->rect().center());
        QCOMPARE(source->childAt(source->mapFromGlobal(at)), scroll);
        QSignalSpy scrollRequested(source, &Overlay::scrollRequested);
        QSignalSpy saveRequested(source, &Overlay::saveRequested);
        QSignalSpy accepted(source, &Overlay::accepted);
        QTest::mousePress(scroll, Qt::LeftButton);
        QCOMPARE(scrollRequested.count(), 0);
        QCOMPARE(saveRequested.count(), 0);
        QTest::mouseRelease(scroll, Qt::LeftButton);
        QCOMPARE(scrollRequested.count(), 1);
        QCOMPARE(saveRequested.count(), 0);
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(owner_->scrollSource_.data(), source);
        QVERIFY(owner_->scrollProgress_ && owner_->scrollProgress_->isVisible());
        QVERIFY(!source->isVisible());
        QVERIFY(ready_);
        QVERIFY(pending_.isEmpty());
        QCOMPARE(wheelSteps_, 0);
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
    }
    void failedStartupKeepsMouseTargetForRetry() {
        // A short selection fails validation before capture starts. The resulting
        // notice must not move the tools under the pointer into another action.
        auto *source = overlay({40, 60, 240, 100});
        auto *scroll = captureButton(source, QStringLiteral("scroll"));
        QVERIFY(scroll && scroll->isVisible() && scroll->isEnabled());
        const QPoint at = scroll->mapToGlobal(scroll->rect().center());
        QSignalSpy scrollRequested(source, &Overlay::scrollRequested);
        QSignalSpy saveRequested(source, &Overlay::saveRequested);
        QTest::mouseClick(scroll, Qt::LeftButton);
        QCOMPARE(scrollRequested.count(), 1);
        QCOMPARE(saveRequested.count(), 0);
        QVERIFY(source->isVisible());
        QVERIFY(!owner_->scrollSource_);
        QVERIFY(!owner_->scrollProgress_);
        QVERIFY(!ready_);
        auto *notice = source->findChild<QLabel *>("captureStatus");
        QVERIFY(notice && notice->isVisible());
        QVERIFY(notice->text().contains(QStringLiteral("过小")));
        QCoreApplication::processEvents();
        QVERIFY(source->rect().contains(notice->geometry()));
        QVERIFY(!notice->geometry().intersects(source->findChild<CaptureToolbar *>()->geometry()));
        QVERIFY(!notice->geometry().intersects(source->findChild<CaptureSidebar *>()->geometry()));
        QVERIFY(notice->height() >= notice->heightForWidth(notice->width()));
        const QString artifacts = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(source->grab().save(QDir(artifacts).filePath("scroll-start-failure.png")));
        }
        QCOMPARE(scroll->mapToGlobal(scroll->rect().center()), at);
        auto *hit = source->childAt(source->mapFromGlobal(at));
        QCOMPARE(hit, scroll);
        QTest::mouseClick(hit, Qt::LeftButton, Qt::NoModifier, hit->mapFromGlobal(at));
        QCOMPARE(scrollRequested.count(), 2);
        QCOMPARE(saveRequested.count(), 0);
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
    }
    void stoppingDuringPreparationRestoresAllScreensAndRejectsLateReady() {
        auto *source = overlay();
        ScreenFrame second = source->frame();
        second.logicalGeometry.translate(640, 0);
        second.nativeGeometry.translate(640, 0);
        auto *other = overlay({}, second);
        const QRect selected = source->selection();
        QTest::keyClick(source, Qt::Key_L);
        QVERIFY(owner_->scrollProgress_ && owner_->scrollProgress_->isVisible());
        QVERIFY(!source->isVisible());
        QVERIFY(!other->isVisible());
        QVERIFY(ready_);
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(!owner_->scrollSource_);
        QVERIFY(source->isVisible());
        QVERIFY(other->isVisible());
        QCOMPARE(source->selection(), selected);
        QVERIFY(owner_->capturing_);
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
        auto lateReady = std::move(ready_);
        lateReady();
        QTest::qWait(40);
        QCOMPARE(targetQueries_, 0);
        QVERIFY(pending_.isEmpty());
        QCOMPARE(wheelSteps_, 0);
    }
    void nativeCoordinatesKeepNegativeOriginAndPerScreenScale() {
        ScreenFrame frame;
        frame.image = QImage(960, 720, QImage::Format_RGB32);
        frame.image.fill(Qt::white);
        frame.logicalGeometry = {-1200, 100, 640, 480};
        frame.nativeGeometry = {-1800, 150, 960, 720};
        frame.nativePixels = true;
        const QRect selected(60, 90, 360, 270);
        auto *source = overlay(selected, frame);
        begin(source);
        QTRY_VERIFY(!pending_.isEmpty());
        QCOMPARE(capturedRegion_, selected.translated(frame.nativeGeometry.topLeft()));
        QCOMPARE(targetPoint_, capturedRegion_.center());
        QCOMPARE(owner_->scrollRun_.logicalRegion, QRect(-1160, 160, 240, 180));
        QVERIFY(!source->isVisible());
        QTest::keyClick(owner_->scrollProgress_, Qt::Key_Escape);
        QVERIFY(!owner_->scrollSource_);
        QVERIFY(source->isVisible());
        auto lateFrame = pending_.takeFirst();
        lateFrame(frame.image.copy(selected), {});
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(wheelSteps_, 0);
    }
    void manualCaptureWaitsForUserWithoutScrollingOrCompleting() {
        auto *source = overlay();
        const QImage first = document().copy(0, 0, 240, 180);
        owner_->scrollIo_.settleTimeoutMs = 450;
        begin(source);
        auto *automatic = owner_->scrollProgress_->findChild<QCheckBox *>("scrollAutomatic");
        QVERIFY(automatic && !automatic->isChecked());
        QVERIFY(!owner_->scrollRun_.automatic);
        // Several stable rounds outlast the watchdog for a single sampling round.
        // An idle user must retain the controls, with no one-frame fake result.
        for (int sample = 0; sample < 14; ++sample)
            QVERIFY(reply(first));
        QCOMPARE(wheelSteps_, 0);
        QCOMPARE(focusRestores_, 1);
        QVERIFY(owner_->scrollSource_);
        QVERIFY(!owner_->scrollRun_.paused);
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(button("scrollFinish")->isVisible());
        QVERIFY(!button("scrollFinish")->isEnabled());
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
    }
    void manualContinuousMovementWaitsForPauseWhileCaptureKeepsResponding() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        QImage moving(first.size(), first.format());
        moving.fill(Qt::white);
        owner_->scrollIo_.settleTimeoutMs = 350;
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        for (int sample = 0; sample < 12; ++sample)
            QVERIFY(reply(sample % 2 ? moving : next));
        QVERIFY(owner_->scrollSource_);
        QVERIFY(!owner_->scrollRun_.paused);
        QVERIFY(!owner_->scrollRun_.initial);
        QCOMPARE(wheelSteps_, 0);
        QCOMPARE(focusRestores_, 1);
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QVERIFY(!owner_->scrollRun_.initial);
        QCOMPARE(owner_->scroller_->frames(), 1);
        QVERIFY(!owner_->editor_.hasDocument());
    }
    void manualCaptureFinishesWhileRunningAndRejectsLateFrames() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QVERIFY(!owner_->scrollRun_.paused);
        QVERIFY(button("scrollFinish")->isVisible() && button("scrollFinish")->isEnabled());
        // Reaching the bottom does not finish a manual capture by itself.
        for (int sample = 0; sample < 6; ++sample)
            QVERIFY(reply(next));
        QVERIFY(!owner_->editor_.hasDocument());
        QTRY_VERIFY(!pending_.isEmpty());
        auto lateFrame = pending_.takeFirst();
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY(owner_->editor_.hasDocument());
        QVERIFY(!owner_->capturing_);
        QCOMPARE(wheelSteps_, 0);
        const QImage result = owner_->editor_.document().image;
        QCOMPARE(result, whole.copy(0, 0, 240, 240).convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(owner_->history_.count(), 1);
        lateFrame(whole.copy(0, 120, 240, 180), {});
        QCOMPARE(owner_->editor_.document().image, result);
        QCOMPARE(owner_->history_.count(), 1);
        // Automatic scrolling is an explicit choice for this run only.
        owner_->capturing_ = true;
        begin(overlay());
        QVERIFY(!owner_->scrollRun_.automatic);
        QCOMPARE(wheelSteps_, 0);
    }
    void manualCaptureStartsMidPageAndRevisitsWithoutDuplicateRows() {
        auto *source = overlay();
        const QImage whole = document();
        const auto viewport = [&](int offset) { return whole.copy(0, offset, 240, 180); };
        begin(source);
        QVERIFY(reply(viewport(60)));
        QVERIFY(reply(viewport(60)));
        QVERIFY(owner_->scrollRegion_ && owner_->scrollRegion_->isVisible());
        QCOMPARE(owner_->scrollRegion_->selection(), owner_->scrollRun_.logicalRegion);
        auto *preview = owner_->scrollProgress_->findChild<ScrollCapturePreview *>("scrollPreview");
        QVERIFY(preview);
        QVERIFY(reply(viewport(120)));
        QVERIFY(reply(viewport(120)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(owner_->scroller_->viewportRect(), QRect(0, 60, 240, 180));
        QCOMPARE(preview->property("captureViewport").toRect(), QRect(0, 60, 240, 180));
        QVERIFY(reply(viewport(60)));
        QVERIFY(reply(viewport(60)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(owner_->scroller_->viewportRect(), QRect(0, 0, 240, 180));
        QCOMPARE(preview->property("captureViewport").toRect(), QRect(0, 0, 240, 180));
        QVERIFY(preview->property("captureMatched").toBool());
        QVERIFY(reply(viewport(0)));
        QVERIFY(reply(viewport(0)));
        QCOMPARE(owner_->scroller_->frames(), 2);
        QCOMPARE(owner_->scroller_->size(), QSize(240, 300));
        QVERIFY(reply(viewport(120)));
        QVERIFY(reply(viewport(120)));
        QCOMPARE(owner_->scroller_->frames(), 2);
        QCOMPARE(owner_->scroller_->viewportRect(), QRect(0, 120, 240, 180));
        QVERIFY(reply(viewport(180)));
        QVERIFY(reply(viewport(180)));
        QCOMPARE(owner_->scroller_->frames(), 3);
        QVERIFY(owner_->scroller_->matched());
        QVERIFY(!owner_->scrollRun_.paused);
        QCOMPARE(wheelSteps_, 0);
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY(owner_->editor_.hasDocument());
        QCOMPARE(owner_->editor_.document().image, whole.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(owner_->history_.count(), 1);
    }
    void captureFallbackHidesIndependentPreviewAndCropMenu_data() {
        QTest::addColumn<bool>("toolbarOverlaps");
        QTest::newRow("toolbar-outside-selection") << false;
        QTest::newRow("toolbar-also-overlaps") << true;
    }
    void captureFallbackHidesIndependentPreviewAndCropMenu() {
        QFETCH(bool, toolbarOverlaps);
        if (QGuiApplication::platformName() != "offscreen")
            QSKIP("Use offscreen windows to exercise unavailable display-affinity fallback without changing platform permissions.");
        auto *source = overlay();
        const QImage first = document().copy(0, 0, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        auto *progress = owner_->scrollProgress_.data();
        QVERIFY(progress && progress->isVisible());
        auto *preview = progress->findChild<QWidget *>("scrollPreviewArea");
        auto *cropMenu = progress->findChild<QWidget *>("scrollCropMenu");
        QVERIFY(preview && preview->isVisible() && cropMenu);
        if (excludedFromCapture(preview) || excludedFromCapture(progress))
            QSKIP("This platform excludes the test windows from capture; fallback is not applicable.");

        // Drain the scheduled sample, then start a fresh round with independent
        // top-level surfaces covering the live selection. The toolbar can stay
        // outside, so hiding only its parent would leave visible overlay pixels.
        QTRY_VERIFY(!pending_.isEmpty());
        pending_.clear();
        ++owner_->scrollRun_.settleRound;
        const QRect region = owner_->scrollRun_.logicalRegion;
        if (toolbarOverlaps) progress->move(region.topLeft() + QPoint(5, 5));
        QVERIFY(progress->geometry().intersects(region) == toolbarOverlaps);
        button("scrollCrop")->click();
        QVERIFY(cropMenu->isVisible());
        preview->move(region.topLeft() + QPoint(10, 10));
        cropMenu->move(region.topLeft() + QPoint(20, 20));
        QVERIFY(preview->geometry().intersects(region));
        QVERIFY(cropMenu->geometry().intersects(region));
        QVERIFY(!excludedFromCapture(cropMenu));

        bool grabbed = false;
        bool previewHidden = false;
        bool cropHidden = false;
        bool toolbarHidden = false;
        owner_->scrollIo_.grab = [&, this](const QRect &captured, ScrollRegionCallback callback) {
            capturedRegion_ = captured;
            grabbed = true;
            previewHidden = !preview->isVisible();
            cropHidden = !cropMenu->isVisible();
            toolbarHidden = !progress->isVisible();
            pending_.append(std::move(callback));
        };
        owner_->beginScrollSettle();
        owner_->readScrollFrame();
        QVERIFY(!preview->isVisible());
        QVERIFY(!cropMenu->isVisible());
        QTRY_VERIFY(grabbed);
        QVERIFY(previewHidden);
        QVERIFY(cropHidden);
        QCOMPARE(toolbarHidden, toolbarOverlaps);
        QCOMPARE(capturedRegion_, owner_->scrollRun_.nativeRegion);
        QVERIFY(!pending_.isEmpty());
        auto callback = pending_.takeFirst();
        callback(first, {});
        QVERIFY(progress->isVisible());
        QVERIFY(preview->isVisible());
        QVERIFY(cropMenu->isVisible());
        QVERIFY(!owner_->scrollRun_.paused);
        QCOMPARE(owner_->scroller_->picture(), first.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(owner_->history_.count(), 0);
        QVERIFY(!owner_->editor_.hasDocument());
        // Keep callbacks that reference this test's local observation variables
        // from running after the test has returned.
        ++owner_->scrollRun_.settleRound;
        owner_->scrollIo_.grab = [this](const QRect &captured, ScrollRegionCallback callback) {
            capturedRegion_ = captured;
            pending_.append(std::move(callback));
        };
    }
    void stopThenStartKeepsRegionAndRejectsPreviousSamplingCallback() {
        auto *source = overlay();
        const QImage whole = document();
        begin(source);
        for (int offset : {0, 0, 60, 60})
            QVERIFY(reply(whole.copy(0, offset, 240, 180)));
        QTRY_VERIFY(!pending_.isEmpty());
        auto stale = pending_.takeFirst();
        const auto region = owner_->scrollRegion_;
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(owner_->scrollRun_.paused);
        QVERIFY(region && region->isVisible());
        QVERIFY(!owner_->scroller_->running());
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(!button("scrollFinish")->isEnabled());
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(!owner_->scrollRun_.paused);
        QCOMPARE(owner_->scrollRegion_, region);
        stale(whole.copy(0, 180, 240, 180), {});
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(reply(whole.copy(0, 120, 240, 180)));
        QVERIFY(reply(whole.copy(0, 120, 240, 180)));
        QVERIFY(owner_->scroller_->running());
        QVERIFY(reply(whole.copy(0, 180, 240, 180)));
        QVERIFY(reply(whole.copy(0, 180, 240, 180)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(owner_->scroller_->picture(), whole.copy(0, 120, 240, 240).convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(wheelSteps_, 0);
    }
    void userCanStopAndRestartFirstViewportWithoutCreatingAResult() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(!owner_->scrollRun_.initial);
        QCOMPARE(owner_->scroller_->frames(), 0);
        QTRY_VERIFY(!pending_.isEmpty());
        auto stale = pending_.takeFirst();
        auto *region = owner_->scrollRegion_.data();
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(owner_->scrollRun_.paused);
        QVERIFY(owner_->scrollSource_);
        QVERIFY(region && region->isVisible());
        QVERIFY(!source->isVisible());
        QVERIFY(!button("scrollFinish")->isEnabled());
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(!owner_->scrollRun_.paused);
        stale(first, {});
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(reply(whole.copy(0, 60, 240, 180)));
        QVERIFY(reply(whole.copy(0, 60, 240, 180)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(owner_->scrollRegion_.data(), region);
        QVERIFY(button("scrollFinish")->isEnabled());
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
    }
    void resizingPausedRegionRestartsCaptureWithNativePixelMapping() {
        ScreenFrame frame;
        frame.image = QImage(960, 720, QImage::Format_RGB32);
        frame.image.fill(Qt::white);
        frame.logicalGeometry = {-1200, 100, 640, 480};
        frame.nativeGeometry = {-1800, 150, 960, 720};
        frame.nativePixels = true;
        auto *source = overlay({60, 90, 360, 270}, frame);
        const QImage whole = document(360, 540);
        begin(source);
        for (int offset : {0, 0, 90, 90})
            QVERIFY(reply(whole.copy(0, offset, 360, 270)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(owner_->scrollRun_.paused);
        auto *region = owner_->scrollRegion_.data();
        QVERIFY(region);
        const QPoint corner(region->width() - 2, region->height() - 2);
        QTest::mousePress(region, Qt::LeftButton, Qt::NoModifier, corner);
        QTest::mouseMove(region, corner + QPoint(20, 20));
        QTest::mouseRelease(region, Qt::LeftButton, Qt::NoModifier, corner + QPoint(20, 20));
        QCOMPARE(owner_->scrollRun_.logicalRegion, QRect(-1160, 160, 260, 200));
        QCOMPARE(owner_->scrollRun_.nativeRegion, QRect(-1740, 240, 390, 300));
        QVERIFY(owner_->scrollRun_.initial);
        QVERIFY(owner_->scrollRun_.paused);
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(!button("scrollFinish")->isEnabled());
        pending_.clear(); // Every old request belongs to the invalidated generation.
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        const QImage resized = document(390, 600);
        for (int offset : {0, 0, 60, 60})
            QVERIFY(reply(resized.copy(0, offset, 390, 300)));
        QCOMPARE(capturedRegion_, QRect(-1740, 240, 390, 300));
        QCOMPARE(owner_->scroller_->size(), QSize(390, 360));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QVERIFY(!owner_->scrollRun_.initial);
        QVERIFY(!owner_->scrollRun_.paused);
    }
    void cropButtonsTrimCapturedPixelsWhileRunning() {
        auto *source = overlay();
        const QImage whole = document();
        begin(source);
        for (int offset : {0, 0, 60, 60, 120, 120, 60, 60})
            QVERIFY(reply(whole.copy(0, offset, 240, 180)));
        QCOMPARE(owner_->scroller_->size(), QSize(240, 300));
        QTest::mouseClick(button("scrollCropBegin"), Qt::LeftButton);
        QVERIFY(!owner_->scrollRun_.paused);
        QCOMPARE(owner_->scroller_->size(), QSize(240, 240));
        QTest::mouseClick(button("scrollCropEnd"), Qt::LeftButton);
        QCOMPARE(owner_->scroller_->size(), QSize(240, 180));
        QCOMPARE(owner_->scrollResult(), whole.copy(0, 60, 240, 180).convertToFormat(QImage::Format_ARGB32));
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY(owner_->editor_.hasDocument());
        QCOMPARE(owner_->editor_.document().image, whole.copy(0, 60, 240, 180).convertToFormat(QImage::Format_ARGB32));
    }
    void horizontalChoiceResetsSamplingAndDisablesAutomaticWheel() {
        auto *source = overlay();
        begin(source);
        const QImage first = document().copy(0, 0, 240, 180);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(button("scrollDirection")->isEnabled());
        QTRY_VERIFY(!pending_.isEmpty());
        auto stale = pending_.takeFirst();
        owner_->scrollProgress_->findChild<QAction *>("scrollHorizontalAction")->trigger();
        QCOMPARE(owner_->scrollRun_.axis, Qt::Horizontal);
        QVERIFY(owner_->scrollRun_.initial);
        auto *automatic = owner_->scrollProgress_->findChild<QCheckBox *>("scrollAutomatic");
        QVERIFY(automatic && !automatic->isEnabled() && !automatic->isChecked());
        owner_->setAutoScrollCapture(true);
        QVERIFY(!owner_->scrollRun_.automatic);
        stale(document().copy(0, 0, 240, 180), {});
        QVERIFY(owner_->scrollRun_.initial);
        const QImage whole = document(480, 180);
        for (int offset : {0, 0, 60, 60})
            QVERIFY(reply(whole.copy(offset, 0, 240, 180)));
        QCOMPARE(owner_->scroller_->axis(), Qt::Horizontal);
        QCOMPARE(owner_->scroller_->size(), QSize(300, 180));
        QCOMPARE(owner_->scroller_->viewportRect(), QRect(60, 0, 240, 180));
        QCOMPARE(wheelSteps_, 0);
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QCOMPARE(owner_->editor_.document().image, whole.copy(0, 0, 300, 180).convertToFormat(QImage::Format_ARGB32));
    }
    void manualScrollingStitchesInMotionAndRecoversFromAGap() {
        // A user who keeps turning the wheel never gives two equal samples. Each
        // sample that continues the stitch exactly is shown at once; a gap that
        // no longer overlaps waits for the user instead of ending the capture.
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(reply(whole.copy(0, 40, 240, 180)));
        QVERIFY(reply(whole.copy(0, 80, 240, 180)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(owner_->scroller_->size(), QSize(240, 260));
        auto *preview = owner_->scrollProgress_->findChild<QLabel *>("scrollPreview");
        QVERIFY(preview && !preview->pixmap().isNull());
        QVERIFY(button("scrollFinish")->isEnabled());
        QImage unrelated(first.size(), first.format());
        unrelated.fill(Qt::black);
        QVERIFY(reply(unrelated));
        QVERIFY(reply(unrelated));
        QVERIFY(owner_->scrollSource_);
        QVERIFY(!owner_->scrollRun_.paused);
        auto *status = owner_->scrollProgress_->findChild<QLabel *>("scrollStatus");
        QVERIFY(status->text().contains(QStringLiteral("往回滚动")));
        QVERIFY(!preview->property("captureMatched").toBool());
        QVERIFY(reply(whole.copy(0, 120, 240, 180)));
        QVERIFY(reply(whole.copy(0, 120, 240, 180)));
        QVERIFY(!status->text().contains(QStringLiteral("往回滚动")));
        QVERIFY(preview->property("captureMatched").toBool());
        QCOMPARE(wheelSteps_, 0);
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY(owner_->editor_.hasDocument());
        QCOMPARE(owner_->editor_.document().image,
                 whole.copy(0, 0, 240, 300).convertToFormat(QImage::Format_ARGB32));
    }
    void automaticChoiceCancelsPendingManualFrameAndCanReturnToManual() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QTRY_VERIFY(!pending_.isEmpty());
        auto staleManualFrame = pending_.takeFirst();
        auto *automatic = owner_->scrollProgress_->findChild<QCheckBox *>("scrollAutomatic");
        QVERIFY(automatic);
        QTest::mouseClick(automatic, Qt::LeftButton, Qt::NoModifier, QPoint(8, automatic->height() / 2));
        QVERIFY(owner_->scrollRun_.automatic);
        QTRY_COMPARE(wheelSteps_, 1);
        QTest::mouseClick(automatic, Qt::LeftButton, Qt::NoModifier, QPoint(8, automatic->height() / 2));
        QVERIFY(!owner_->scrollRun_.automatic);
        QCOMPARE(focusRestores_, 2);
        staleManualFrame(next, {});
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(wheelSteps_, 1);
        QVERIFY(!owner_->editor_.hasDocument());
    }
    void manualCaptureRequiresOriginalWindowAndStopsBeforeCapturingAnother() {
        auto *source = overlay();
        owner_->scrollIo_.targetAt = [](QPoint) { return ScrollCaptureTarget{}; };
        begin(source);
        QVERIFY(!owner_->scrollSource_);
        QVERIFY(source->isVisible());
        QVERIFY(pending_.isEmpty());
        owner_->scrollIo_.targetAt = [](QPoint) { return ScrollCaptureTarget{42, 7}; };
        const QImage whole = document();
        begin(source);
        QVERIFY(reply(whole.copy(0, 0, 240, 180)));
        QVERIFY(reply(whole.copy(0, 0, 240, 180)));
        QVERIFY(reply(whole.copy(0, 60, 240, 180)));
        QVERIFY(reply(whole.copy(0, 60, 240, 180)));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(wheelSteps_, 0);
        owner_->scrollIo_.targetAt = [](QPoint) { return ScrollCaptureTarget{43, 8}; };
        QTRY_VERIFY(owner_->scrollRun_.paused);
        QVERIFY(pending_.isEmpty());
        QVERIFY(owner_->scrollProgress_->findChild<QLabel *>("scrollStatus")->text().contains("原滚动窗口"));
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY(owner_->editor_.hasDocument());
    }
    void waitsForStableSamplesAndConfirmsBottomTwice() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        begin(source);
        owner_->setAutoScrollCapture(true);
        QImage unsettled(first.size(), first.format());
        unsettled.fill(Qt::white);
        QVERIFY(reply(unsettled));
        QCOMPARE(wheelSteps_, 0);
        QVERIFY(reply(first));
        QCOMPARE(wheelSteps_, 0);
        QVERIFY(reply(first));
        QCOMPARE(wheelSteps_, 1);
        QVERIFY(reply(next));
        QCOMPARE(owner_->scroller_->frames(), 0);
        QVERIFY(reply(next));
        QCOMPARE(owner_->scroller_->frames(), 1);
        QCOMPARE(wheelSteps_, 2);
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QCOMPARE(owner_->scrollRun_.unchanged, 1);
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(wheelSteps_, 3);
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QVERIFY(owner_->editor_.hasDocument());
        QVERIFY(!owner_->capturing_);
        QVERIFY(owner_->overlays_.isEmpty());
        QVERIFY(!owner_->scrollProgress_);
        QCOMPARE(owner_->editor_.document().source, QString("scroll"));
        QCOMPARE(owner_->editor_.document().image,
                 whole.copy(0, 0, 240, 240).convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(owner_->history_.count(), 1);
        QCOMPARE(owner_->history_.at(0), owner_->editor_.document().image);
        QCOMPARE(owner_->history_.selections().first(), QRect(40, 60, 240, 180));
    }
    void noMovementReturnsSelectionWithoutCreatingLongScreenshot() {
        ScreenFrame frame;
        frame.image = QImage(1920, 1080, QImage::Format_RGB32);
        frame.image.fill(Qt::white);
        frame.logicalGeometry = frame.nativeGeometry = frame.image.rect();
        frame.nativePixels = true;
        auto *source = overlay({1640, 240, 240, 180}, frame);
        const QImage first = document().copy(0, 0, 240, 180);
        auto *scroll = captureButton(source, QStringLiteral("scroll"));
        QVERIFY(scroll && scroll->isVisible() && scroll->isEnabled());
        const QPoint at = scroll->mapToGlobal(scroll->rect().center());
        QCOMPARE(source->childAt(source->mapFromGlobal(at)), scroll);
        QSignalSpy scrollRequested(source, &Overlay::scrollRequested);
        QSignalSpy saveRequested(source, &Overlay::saveRequested);
        QTest::mouseClick(scroll, Qt::LeftButton);
        QVERIFY(ready_);
        auto ready = std::move(ready_);
        ready();
        owner_->setAutoScrollCapture(true);
        for (int frame = 0; frame < 6; ++frame)
            QVERIFY(reply(first));
        QVERIFY(!owner_->scrollSource_);
        QVERIFY(source->isVisible());
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
        QCOMPARE(wheelSteps_, 2);
        QCOMPARE(scrollRequested.count(), 1);
        QCOMPARE(saveRequested.count(), 0);
        auto *notice = source->findChild<QLabel *>("captureStatus");
        QVERIFY(notice && notice->isVisible() && !notice->text().isEmpty());
        QCoreApplication::processEvents();
        QCOMPARE(scroll->mapToGlobal(scroll->rect().center()), at);
        auto *hit = source->childAt(source->mapFromGlobal(at));
        QCOMPARE(hit, scroll);
        QTest::mouseClick(hit, Qt::LeftButton, Qt::NoModifier, hit->mapFromGlobal(at));
        QCOMPARE(scrollRequested.count(), 2);
        QCOMPARE(saveRequested.count(), 0);
        QCOMPARE(owner_->scrollSource_.data(), source);
        QVERIFY(owner_->scrollProgress_ && owner_->scrollProgress_->isVisible());
        QVERIFY(ready_);
        QVERIFY(!owner_->editor_.hasDocument());
    }
    void stoppingAfterProgressDiscardsThisRunAndIgnoresLateCallback() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QTRY_VERIFY(!pending_.isEmpty());
        QTest::mouseClick(button("scrollStop"), Qt::LeftButton);
        QVERIFY(owner_->scrollRun_.paused);
        QVERIFY(!owner_->scroller_->running());
        QVERIFY(button("scrollFinish")->isVisible());
        QVERIFY(!button("scrollFinish")->isEnabled());
        QVERIFY(!source->isVisible());
        QVERIFY(owner_->scroller_->picture().isNull());
        auto callback = pending_.takeFirst();
        callback({}, "late failure");
        QVERIFY(owner_->scroller_->picture().isNull());
        QCOMPARE(owner_->history_.count(), 0);
        QVERIFY(!owner_->editor_.hasDocument());
    }
    void missingCallbackTimesOutAndOffersExistingPicture() {
        auto *source = overlay();
        const QImage whole = document();
        const QImage first = whole.copy(0, 0, 240, 180);
        const QImage next = whole.copy(0, 60, 240, 180);
        owner_->scrollIo_.settleTimeoutMs = 700;
        begin(source);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QVERIFY(reply(next));
        QVERIFY(reply(next));
        QTRY_VERIFY(!pending_.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(owner_->scrollRun_.paused, 1200);
        QVERIFY(button("scrollFinish")->isEnabled());
        QVERIFY(!owner_->editor_.hasDocument());
        QVERIFY(owner_->scrollProgress_->findChild<QLabel *>("scrollStatus")->text().contains("超时"));
        QTest::keyClick(owner_->scrollProgress_, Qt::Key_Escape);
        QVERIFY(source->isVisible());
        QVERIFY(!owner_->scrollSource_);
        QCOMPARE(owner_->history_.count(), 0);
        auto callback = pending_.takeFirst();
        callback(next, {});
        QVERIFY(!owner_->editor_.hasDocument());
    }
    void unsupportedAndTargetFailureRemainRecoverable() {
        auto *source = overlay();
        owner_->scrollIo_.supported = [](QString *reason) {
            *reason = "unsupported backend";
            return false;
        };
        QTest::keyClick(source, Qt::Key_L);
        QVERIFY(source->isVisible());
        QVERIFY(!ready_);
        QVERIFY(!owner_->scrollProgress_);
        QVERIFY(source->findChild<QLabel *>("captureStatus")->text().contains("unsupported"));
        owner_->scrollIo_.supported = [](QString *) { return true; };
        owner_->scrollIo_.step = [this](const ScrollCaptureTarget &, QPoint, int, QString *error) {
            ++wheelSteps_;
            *error = "target changed";
            return false;
        };
        begin(source);
        owner_->setAutoScrollCapture(true);
        const QImage first = document().copy(0, 0, 240, 180);
        QVERIFY(reply(first));
        QVERIFY(reply(first));
        QTRY_VERIFY(source->isVisible());
        QVERIFY(!owner_->scrollSource_);
        QCOMPARE(wheelSteps_, 1);
        QVERIFY(!owner_->editor_.hasDocument());
        QCOMPARE(owner_->history_.count(), 0);
    }
    void realScrollingWindowProducesCompleteImage() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen")
            QSKIP("Run with the Windows platform to verify the external native fixture.");
        QProcess fixture;
        fixture.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("scroll_platform_tests.exe"));
        fixture.setArguments({"--scroll-fixture"});
        fixture.start();
        QVERIFY(fixture.waitForStarted());
        QVERIFY(fixture.waitForReadyRead(5000));
        const QByteArray output = fixture.readLine();
        const auto ready = QJsonDocument::fromJson(output).object();
        QVERIFY2(!ready.isEmpty(), output.constData());
        const HWND window = reinterpret_cast<HWND>(quintptr(ready["window"].toString().toULongLong()));
        struct CloseFixture {
            HWND window;
            QProcess &process;
            ~CloseFixture() {
                if (IsWindow(window))
                    PostMessageW(window, WM_CLOSE, 0, 0);
                if (process.state() != QProcess::NotRunning)
                    process.waitForFinished(5000);
            }
        } close{window, fixture};
        const QRect nativeRect = jsonRect(ready["nativeRect"].toObject());
        QVERIFY(!nativeRect.isEmpty());
        SetForegroundWindow(window);
        QTest::qWait(100);
        // Use the production screenshot entry point and its full monitor overlays.
        // A fixture-sized overlay would let targeted QTest clicks reach a toolbar
        // button that is actually clipped outside the native window.
        owner_->capturing_ = false;
        owner_->capture();
        QTRY_VERIFY_WITH_TIMEOUT(!owner_->overlays_.isEmpty(), 5000);
        Overlay *source = nullptr;
        for (auto *candidate : owner_->overlays_)
            if (candidate->frame().nativeGeometry.contains(nativeRect)) {
                source = candidate;
                break;
            }
        QVERIFY(source);
        const ScreenFrame &frame = source->frame();
        QVERIFY(frame.nativePixels);
        QCOMPARE(frame.nativeGeometry.size(), frame.image.size());
        QVERIFY(frame.nativeGeometry.size() != nativeRect.size());
        QVERIFY(QTest::qWaitForWindowExposed(source));
        const QRect viewport = nativeRect.translated(-frame.nativeGeometry.topLeft());
        source->setSelections({viewport});
        QTest::keyClick(source, Qt::Key_R);
        QCOMPARE(source->selection(), viewport);
        const QImage frozen = source->selectionPixels();
        QCOMPARE(frozen.size(), nativeRect.size());
        QCOMPARE(frozen.pixelColor(3, 0), QColor(44, 86, 160));
        QCOMPARE(frozen.pixelColor(3, ready["fixedHeader"].toInt()), QColor(180, 180, 180));
        QCOMPARE(frozen.pixelColor(3, frozen.height() - 1), QColor(55, 65, 75));
        const QRect selection = source->selection();
        owner_->scrollIo_ = {};
        owner_->scrollIo_.step = [this](const ScrollCaptureTarget &target, QPoint at, int steps,
                                       QString *error) {
            ++wheelSteps_;
            return scrollCaptureStep(target, at, steps, error);
        };
        auto *scroll = captureButton(source, QStringLiteral("scroll"));
        QVERIFY(scroll && scroll->isVisible() && scroll->isEnabled());
        const QPoint at = scroll->mapToGlobal(scroll->rect().center());
        QCOMPARE(source->childAt(source->mapFromGlobal(at)), scroll);
        QSignalSpy scrollRequested(source, &Overlay::scrollRequested);
        QSignalSpy saveRequested(source, &Overlay::saveRequested);
        auto *hit = source->childAt(source->mapFromGlobal(at));
        QTest::mouseClick(hit, Qt::LeftButton, Qt::NoModifier, hit->mapFromGlobal(at));
        QCOMPARE(scrollRequested.count(), 1);
        QCOMPARE(saveRequested.count(), 0);
        const int header = ready["fixedHeader"].toInt();
        const int footer = ready["fixedFooter"].toInt();
        const int documentHeight = ready["documentHeight"].toInt();
        const int completeHeight = documentHeight + header + footer;
        // The timer represents the user turning the wheel. The controller itself
        // must only observe and stitch those movements in its default mode.
        QTimer userWheel;
        userWheel.setInterval(100);
        int previousFrames = -1;
        bool inputFailed = false;
        QString inputError;
        connect(&userWheel, &QTimer::timeout, owner_.get(), [&] {
            if (!owner_->scrollSource_ || owner_->scrollRun_.paused || owner_->scrollRun_.initial ||
                !owner_->scroller_ || owner_->scroller_->height() >= completeHeight)
                return;
            if (previousFrames >= 0 && owner_->scroller_->frames() <= previousFrames)
                return;
            previousFrames = owner_->scroller_->frames();
            if (!scrollCaptureStep(owner_->scrollRun_.target, nativeRect.center(), 1, &inputError)) {
                inputFailed = true;
                userWheel.stop();
            }
        });
        userWheel.start();
        QTRY_VERIFY_WITH_TIMEOUT((owner_->scroller_ && owner_->scroller_->height() >= completeHeight) ||
                                    !owner_->scrollSource_ || owner_->scrollRun_.paused || inputFailed, 20000);
        userWheel.stop();
        const QString notice = owner_->scrollProgress_
                                   ? owner_->scrollProgress_->findChild<QLabel *>("scrollStatus")->text()
                                   : QString();
        QVERIFY2(!inputFailed, qPrintable(inputError));
        QVERIFY2(owner_->scroller_ && owner_->scroller_->height() == completeHeight, qPrintable(notice));
        QCOMPARE(wheelSteps_, 0);
        QVERIFY(!owner_->editor_.hasDocument());
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY2(owner_->editor_.hasDocument(), qPrintable(notice));
        const QImage result = owner_->editor_.document().image;
        QCOMPARE(result.size(), QSize(selection.width(), documentHeight + header + footer));
        for (int y = 0; y < header; ++y)
            QCOMPARE(result.pixelColor(3, y), QColor(44, 86, 160));
        for (int y = 0; y < documentHeight; ++y) {
            const int row = y / 24;
            QCOMPARE(result.pixelColor(3, header + y),
                     QColor(180 + (row * 19) % 65, 180 + (row * 37) % 65, 180 + (row * 43) % 65));
        }
        for (int y = result.height() - footer; y < result.height(); ++y)
            QCOMPARE(result.pixelColor(3, y), QColor(55, 65, 75));
        QCOMPARE(owner_->history_.at(0), result);
        const QString artifactDirectory = qEnvironmentVariable(
            "H2D_TEST_ARTIFACTS", QDir(QCoreApplication::applicationDirPath()).filePath("artifacts"));
        QVERIFY(QDir().mkpath(artifactDirectory));
        QVERIFY(result.save(QDir(artifactDirectory).filePath("scroll-native-complete.png")));
#else
        QSKIP("The native scrolling backend is currently Windows only.");
#endif
    }
    void realBrowserProducesCompleteImage() {
#ifdef Q_OS_WIN
        if (!qEnvironmentVariableIsSet("H2D_SCROLL_BROWSER_TEST"))
            QSKIP("Open tests/fixtures/scroll-browser.html and set H2D_SCROLL_BROWSER_TEST=1.");
        struct BrowserSearch {
            QString title = qEnvironmentVariable("H2D_SCROLL_BROWSER_TITLE", "EditHere scrolling browser fixture");
            HWND window = nullptr;
        } search;
        EnumWindows([](HWND window, LPARAM value) -> BOOL {
            auto &search = *reinterpret_cast<BrowserSearch *>(value);
            wchar_t title[512]{};
            GetWindowTextW(window, title, 512);
            if (IsWindowVisible(window) && QString::fromWCharArray(title).contains(search.title)) {
                search.window = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        const HWND browser = search.window;
        QVERIFY2(browser, "The browser fixture must be the active browser tab.");
        const bool wasTopmost = (GetWindowLongPtrW(browser, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        struct RestoreBrowser {
            HWND window;
            bool topmost;
            ~RestoreBrowser() {
                if (IsWindow(window) && !topmost)
                    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        } restore{browser, wasTopmost};
        ShowWindow(browser, SW_RESTORE);
        SetWindowPos(browser, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(browser);
        QTest::qWait(400);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(browser, MONITOR_DEFAULTTOPRIMARY), &monitor));
        const QRect screen(monitor.rcMonitor.left, monitor.rcMonitor.top,
                           monitor.rcMonitor.right - monitor.rcMonitor.left,
                           monitor.rcMonitor.bottom - monitor.rcMonitor.top);
        QImage desktop;
        captureScrollRegion(screen, [&](QImage image, QString) { desktop = image; });
        QVERIFY(!desktop.isNull());
        int left = desktop.width(), top = desktop.height(), right = -1, bottom = -1;
        for (int y = 0; y < desktop.height(); ++y)
            for (int x = 0; x < desktop.width(); ++x)
                if (desktop.pixel(x, y) == qRgb(3, 251, 151)) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
        QVERIFY2(right > left && bottom > top, "The fixture's green capture marker must be visible.");
        const double scale = double(right - left + 1) / 492.0;
        const int border = qRound(6 * scale);
        const QRect region(screen.x() + left + border, screen.y() + top + border,
                           qRound(480 * scale), qRound(440 * scale));
        const auto target = scrollCaptureTargetAt(region.center());
        QString inputError;
        // Reset the observed native browser page, including any previous smooth
        // scroll still in flight. This test validates actual OS wheel handling.
        for (int step = 0; step < 8; ++step) {
            QVERIFY2(scrollCaptureStep(target, region.center(), -8, &inputError), qPrintable(inputError));
            QTest::qWait(400);
        }
        ScreenFrame frame;
        frame.nativeGeometry = region;
        const double dpi = QGuiApplication::primaryScreen()->devicePixelRatio();
        frame.logicalGeometry = {qRound(region.x() / dpi), qRound(region.y() / dpi),
                                 qRound(region.width() / dpi), qRound(region.height() / dpi)};
        frame.nativePixels = true;
        captureScrollRegion(region, [&](QImage image, QString) { frame.image = image; });
        QVERIFY(!frame.image.isNull());
        QCOMPARE(frame.image.pixelColor(qRound(15 * scale), qRound(56 * scale)), QColor(0, 0, 0));
        auto *source = overlay(frame.image.rect(), frame);
        QCOMPARE(source->selection(), frame.image.rect());
        owner_->scrollIo_ = {};
        owner_->scrollIo_.step = [this](const ScrollCaptureTarget &target, QPoint at, int steps,
                                       QString *error) {
            ++wheelSteps_;
            return scrollCaptureStep(target, at, steps, error);
        };
        QTest::keyClick(source, Qt::Key_L);
        const int completeHeight = qRound(2480 * scale);
        QTimer userWheel;
        userWheel.setInterval(100);
        int previousFrames = -1;
        bool inputFailed = false;
        // Stay away from the preview when this small fixture leaves it inside
        // the capture bounds, as a real user would after moving the controls.
        const QPoint wheelPoint = region.topLeft() + QPoint(qRound(15 * scale), qRound(65 * scale));
        connect(&userWheel, &QTimer::timeout, owner_.get(), [&] {
            if (!owner_->scrollSource_ || owner_->scrollRun_.paused || owner_->scrollRun_.initial ||
                !owner_->scroller_ || owner_->scroller_->height() >= completeHeight)
                return;
            if (previousFrames >= 0 && owner_->scroller_->frames() <= previousFrames)
                return;
            previousFrames = owner_->scroller_->frames();
            if (!scrollCaptureStep(target, wheelPoint, 1, &inputError)) {
                inputFailed = true;
                userWheel.stop();
            }
        });
        userWheel.start();
        QTRY_VERIFY_WITH_TIMEOUT((owner_->scroller_ && owner_->scroller_->height() >= completeHeight) ||
                                    !owner_->scrollSource_ || owner_->scrollRun_.paused || inputFailed, 40000);
        userWheel.stop();
        const QString notice = owner_->scrollProgress_
                                   ? owner_->scrollProgress_->findChild<QLabel *>("scrollStatus")->text()
                                   : owner_->editor_.hasDocument() ? QString()
                                                                  : source->findChild<QLabel *>("captureStatus")->text();
        QVERIFY2(!inputFailed, qPrintable(inputError));
        QVERIFY2(owner_->scroller_ && owner_->scroller_->height() == completeHeight, qPrintable(notice));
        QCOMPARE(wheelSteps_, 0);
        QVERIFY(!owner_->editor_.hasDocument());
        QTest::mouseClick(button("scrollFinish"), Qt::LeftButton);
        QVERIFY2(owner_->editor_.hasDocument(), qPrintable(notice));
        const QImage result = owner_->editor_.document().image;
        QCOMPARE(result.size(), QSize(qRound(480 * scale), qRound(2480 * scale)));
        const int sampleX = qRound(15 * scale);
        QCOMPARE(result.pixelColor(sampleX, qRound(20 * scale)), QColor(44, 86, 160));
        for (int row = 0; row < 60; ++row) {
            const int sampleY = qRound((40 + row * 40 + 16) * scale);
            QCOMPARE(result.pixelColor(sampleX, sampleY),
                     QColor((row * 61) % 255, (row * 7) % 255, (row * 37) % 255));
        }
        QCOMPARE(result.pixelColor(sampleX, result.height() - qRound(20 * scale)), QColor(55, 65, 75));
        const QString artifacts = QDir(QCoreApplication::applicationDirPath()).filePath("artifacts");
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(result.save(QDir(artifacts).filePath("scroll-browser-complete.png")));
#else
        QSKIP("The native scrolling backend is currently Windows only.");
#endif
    }
};
} // namespace h2d
QTEST_MAIN(h2d::ScrollControllerTests)
#include "scroll_controller_test.moc"
