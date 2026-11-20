#include "editor.h"
#include "platform.h"
#include <QPushButton>
#include <QTest>
#import <Cocoa/Cocoa.h>

namespace {
NSRect inWindow(NSView *view) {
    return [view convertRect:view.bounds toView:nil];
}
}

class CocoaWindowTest : public QObject {
    Q_OBJECT
  private slots:
    void editorToolsAreVisibleAndClickableBelowTheSystemTitleBar() {
        if (QGuiApplication::platformName() != "cocoa")
            QSKIP("Requires a native Cocoa window.");
        QVERIFY(h2d::usesNativeWindowControls());

        h2d::Editor editor;
        editor.openEmpty();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        NSView *qtView = (__bridge NSView *)reinterpret_cast<void *>(editor.winId());
        NSWindow *window = qtView.window;
        QVERIFY(window);
        QVERIFY(!(window.styleMask & NSWindowStyleMaskFullSizeContentView));
        QVERIFY(!window.titlebarAppearsTransparent);

        NSView *frameView = window.contentView.superview;
        QVERIFY(frameView);
        const NSRect client = inWindow(window.contentView);
        const NSRect qtBounds = inWindow(qtView);
        const auto hitAt = [frameView](NSPoint point) {
            return [frameView hitTest:[frameView convertPoint:point fromView:nil]];
        };

        for (const auto kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            NSButton *button = [window standardWindowButton:kind];
            QVERIFY(button && !button.hidden);
            const NSRect rect = inWindow(button);
            QVERIFY(NSMinY(rect) >= NSMaxY(client));
            QVERIFY(hitAt(NSMakePoint(NSMidX(rect), NSMidY(rect))) == button);
        }

        for (const auto name : {"captureImage", "openSettings", "showGuide"}) {
            auto *tool = editor.findChild<QPushButton *>(name);
            QVERIFY(tool && tool->isVisible());
            const QPoint topLeft = tool->mapTo(&editor, QPoint());
            const NSRect rect = NSMakeRect(NSMinX(qtBounds) + topLeft.x(),
                NSMaxY(qtBounds) - topLeft.y() - tool->height(), tool->width(), tool->height());
            QVERIFY(NSContainsRect(client, rect));
            NSView *hit = hitAt(NSMakePoint(NSMidX(rect), NSMidY(rect)));
            QVERIFY(hit == qtView || [hit isDescendantOf:qtView]);
        }
    }
};

QTEST_MAIN(CocoaWindowTest)
#include "cocoa_window_test.moc"
