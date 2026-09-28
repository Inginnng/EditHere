#include "controller.h"
#include "capturetoolbar.h"
#include <QPushButton>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QTest>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace h2d {
class ScrollControllerTests : public QObject {
    Q_OBJECT
    static QImage content() {
        QImage image(800, 1000, QImage::Format_ARGB32);
        quint32 seed = 17;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                image.setPixel(x, y, qRgb(seed >> 24, (seed >> 16) & 255, (seed >> 8) & 255));
            }
        return image;
    }
    static Overlay *addOverlay(Controller &owner, const QString &name, const QImage &image) {
        auto *overlay = new Overlay({name, {0, 0, 800, 600}, {0, 0, 800, 600},
                                     image.copy(0, 0, 800, 600), true});
        owner.overlays_.append(overlay);
        overlay->show();
        QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(overlay, {500, 400});
        QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {500, 400});
        return overlay;
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "msyh.ttc") >= 0);
        }
#endif
    }
    void nativeScrollingRemainsInteractiveAndBuildsALongPicture_data() {
        QTest::addColumn<Qt::Orientation>("axis");
        QTest::newRow("vertical") << Qt::Vertical;
        QTest::newRow("horizontal") << Qt::Horizontal;
    }
    void nativeScrollingRemainsInteractiveAndBuildsALongPicture() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen")
            QSKIP("Requires the Windows desktop compositor");
        QFETCH(Qt::Orientation, axis);
        QWidget page;
        page.setWindowFlag(Qt::WindowStaysOnTopHint);
        page.setWindowTitle("EditHere long capture regression fixture");
        auto *layout = new QVBoxLayout(&page);
        auto *scroll = new QScrollArea(&page);
        auto *label = new QLabel;
        label->setPixmap(QPixmap::fromImage(content()));
        scroll->setWidget(label);
        layout->addWidget(scroll);
        page.resize(600, 500);
        page.move(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
        page.show();
        page.raise();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        QTest::qWait(200);
        const QPoint global = scroll->viewport()->mapToGlobal(QPoint(20, 20));
        Controller owner(nullptr, AppSettings{});
        owner.capture();
        QTRY_VERIFY_WITH_TIMEOUT(!owner.overlays_.isEmpty(), 3000);
        Overlay *source = nullptr;
        for (auto *overlay : owner.overlays_)
            if (overlay->frame().logicalGeometry.contains(global)) source = overlay;
        QVERIFY(source);
        const QPoint local = global - source->frame().logicalGeometry.topLeft();
        QTest::mousePress(source, Qt::LeftButton, Qt::NoModifier, local);
        QTest::mouseMove(source, local + QPoint(300, 240));
        QTest::mouseRelease(source, Qt::LeftButton, Qt::NoModifier, local + QPoint(300, 240));
        auto *bar = source->findChild<CaptureToolbar *>();
        QVERIFY(bar);
        if (axis == Qt::Horizontal)
            bar->scrollAxisChanged(axis);
        const int initialLength = axis == Qt::Vertical ? source->selection().height() : source->selection().width();
        bar->scroll();
        const QImage first = owner.scroller_->picture();
        QTest::qWait(500);
        QVERIFY(owner.scroller_->running());
        QVERIFY(!owner.editor_.hasDocument());
        const QPoint native = source->scrollTarget().nativePoint;
        HWND hit = WindowFromPoint({native.x(), native.y()});
        QCOMPARE(GetAncestor(hit, GA_ROOT), reinterpret_cast<HWND>(page.winId()));
        auto *scrollbar = axis == Qt::Vertical ? scroll->verticalScrollBar() : scroll->horizontalScrollBar();
        scrollbar->setValue(40);
        QTest::qWait(1000);
        const QString artifacts = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            const QString prefix = axis == Qt::Vertical ? "/native-vertical" : "/native-horizontal";
            first.save(artifacts + prefix + "-first.png");
            source->frame().image.copy(source->selection()).save(artifacts + prefix + "-next.png");
        }
        qInfo() << "Native capture" << source->selection() << source->frame().image.size()
                << "scroll" << scrollbar->value() << "running" << owner.scroller_->running()
                << "frames" << owner.scroller_->frames()
                << "difference" << frameDifference(first, source->frame().image.copy(source->selection()));
        QTRY_VERIFY_WITH_TIMEOUT(owner.scroller_->height() > initialLength, 4000);
        QVERIFY(source->isScrolling());
        QVERIFY(!owner.editor_.hasDocument());
        bar->scroll();
        QVERIFY(!owner.scroller_->running());
        QVERIFY(source->hasScrollPicture());
        bar->annotate();
        QVERIFY(owner.editor_.hasDocument());
        QVERIFY(owner.overlays_.isEmpty());
#else
        QSKIP("Windows native integration test");
#endif
    }
    void enteringAndRepeatedOrFailedFramesNeverOpenAnnotation() {
        Controller owner(nullptr, AppSettings{});
        const QImage image = content();
        auto *overlay = addOverlay(owner, "test-screen", image);
        owner.startScrollCapture(overlay);
        QVERIFY(overlay->isScrolling());
        QVERIFY(owner.scroller_->running());
        QVERIFY(!owner.editor_.hasDocument());
        auto *bar = overlay->findChild<CaptureToolbar *>();
        QVERIFY(bar && bar->scrollRunning());
        for (auto *button : bar->findChildren<QPushButton *>())
            if (button->property("glyphName").toString() == "scroll")
                QVERIFY(button->isEnabled());
        const QImage first = image.copy(overlay->selection());
        for (int i = 0; i < 4; ++i)
            owner.placeScrollFrame(first);
        owner.placeScrollFrame(image.copy(100, 600, 400, 300));
        QVERIFY(owner.scroller_->running());
        QVERIFY(overlay->isScrolling());
        QVERIFY(!owner.editor_.hasDocument());
        QCOMPARE(owner.scroller_->picture(), first);
        owner.abortScrollCapture(true);
        // Previously queued reads must not restart a paused session.
        QTest::qWait(400);
        QVERIFY(!owner.scroller_->running());
        QVERIFY(overlay->isScrolling());
        QVERIFY(!owner.editor_.hasDocument());
        owner.clearOverlays();
    }
    void captureUsesOnlyTheSelectedMonitorAndCanResume() {
        Controller owner(nullptr, AppSettings{});
        const QImage image = content();
        auto *other = addOverlay(owner, "other-screen", image);
        auto *source = addOverlay(owner, "selected-screen", image);
        owner.startScrollCapture(source);
        QCOMPARE(owner.activeScrollOverlay(), source);
        QVERIFY(!other->isScrolling());
        owner.placeScrollFrame(image.copy(100, 120, 400, 300));
        QCOMPARE(owner.scroller_->frames(), 1);
        owner.abortScrollCapture(true);
        owner.startScrollCapture(source);
        QCOMPARE(owner.scroller_->frames(), 1);
        owner.placeScrollFrame(image.copy(100, 140, 400, 300));
        QCOMPARE(owner.scroller_->picture(), image.copy(100, 100, 400, 340));
        QVERIFY(!owner.editor_.hasDocument());
        owner.clearOverlays();
    }
};
}
QTEST_MAIN(h2d::ScrollControllerTests)
#include "scroll_controller_test.moc"
