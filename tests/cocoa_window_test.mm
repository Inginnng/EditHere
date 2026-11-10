#include "platform.h"
#include <QTest>
#include <QWidget>
#import <Cocoa/Cocoa.h>

namespace {
// The window's own coordinates, with the origin at its bottom-left corner, which is
// what AppKit's title bar and its buttons are laid out in.
NSWindow *nativeWindow(QWidget &widget) {
    return ((__bridge NSView *)reinterpret_cast<void *>(widget.winId())).window;
}
NSRect inWindow(NSView *view) {
    return [view convertRect:view.bounds toView:nil];
}
} // namespace

// The editor keeps macOS's title bar so close, minimize and zoom are the traffic
// lights. That bar has to stay over the client area, with no visible strip pushing the
// editor's own bar down, and it may intercept clicks only where the buttons are.
// ui_test covers which chrome the editor shows; this one needs a real Cocoa window.
class CocoaWindowTest : public QObject {
    Q_OBJECT
  private slots:
    void systemTitleBarKeepsTrafficLightsOverTheClientArea() {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("this test needs a real Cocoa window");
        QVERIFY(h2d::usesNativeWindowControls());
        QWidget window;
        window.setWindowFlags(Qt::Window);
        window.resize(640, 420);
        window.show();
        QTest::qWaitForWindowExposed(&window);
        h2d::configureNativeWindow(&window, false);
        QTest::qWait(250);
        NSWindow *nsWindow = nativeWindow(window);
        QVERIFY(nsWindow);
        // Hidden title, transparent title bar, client area over the whole window:
        // otherwise the traffic lights would sit above the editor's own bar.
        QCOMPARE(nsWindow.titleVisibility, NSWindowTitleHidden);
        QVERIFY(nsWindow.titlebarAppearsTransparent);
        QVERIFY(nsWindow.styleMask & NSWindowStyleMaskFullSizeContentView);
        QCOMPARE(nsWindow.contentView.frame.size.height, nsWindow.frame.size.height);

        // The traffic lights are the window controls, inside the corner the editor's
        // bar leaves free, and still taking their own clicks. The title bar and the
        // client area are siblings inside the window's frame view, so a hit test has
        // to start there: that is the view AppKit sends the event to.
        NSView *frameView = nsWindow.contentView.superview;
        QVERIFY(frameView);
        auto hitAt = [&](NSPoint inWindowPoint) {
            return [frameView hitTest:[frameView convertPoint:inWindowPoint fromView:nil]];
        };
        CGFloat right = 0;
        for (NSNumber *type in @[ @(NSWindowCloseButton), @(NSWindowMiniaturizeButton), @(NSWindowZoomButton) ]) {
            NSButton *button = [nsWindow standardWindowButton:(NSWindowButton)type.integerValue];
            QVERIFY(button);
            QVERIFY(!button.hidden);
            const NSRect frame = inWindow(button);
            QVERIFY2(frame.origin.x + frame.size.width <= 78, "a traffic light is outside the bar's inset");
            QVERIFY2(frame.origin.y + frame.size.height >= nsWindow.frame.size.height - 32,
                     "a traffic light is not in the top-left corner");
            right = std::max(right, frame.origin.x + frame.size.width);
            QVERIFY2(hitAt(NSMakePoint(NSMidX(frame), NSMidY(frame))) == button,
                     "a traffic light must keep its own clicks");
        }
        QVERIFY(right > 0);

        // Everywhere else the title bar strip belongs to the client area, so the
        // editor's own bar can carry tools across the top of the window.
        NSView *qtView = (__bridge NSView *)reinterpret_cast<void *>(window.winId());
        for (const CGFloat x : { right + 24.0, nsWindow.frame.size.width / 2 }) {
            NSView *hit = hitAt(NSMakePoint(x, nsWindow.frame.size.height - 16));
            QVERIFY2(hit == qtView || [hit isDescendantOf:qtView],
                     "the title bar area must not swallow clicks next to the traffic lights");
        }
        window.hide();
    }
};

QTEST_MAIN(CocoaWindowTest)
#include "cocoa_window_test.moc"
