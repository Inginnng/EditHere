#include "trayeventguard_mac.h"
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>
#include <QtWidgets/private/qsystemtrayicon_p.h>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

@protocol EditHereQtTrayDelegate
- (instancetype)initWithSysTray:(void *)tray;
- (void)statusItemClicked;
- (void)statusItemMenuBeganTracking:(NSNotification *)notification;
@end

namespace {
__strong NSEvent *injectedEvent = nil;
NSEvent *testCurrentEvent(id, SEL) { return injectedEvent; }

class CurrentEvent {
    Method method_;
    IMP original_;
  public:
    explicit CurrentEvent(NSEvent *event) {
        injectedEvent = event;
        method_ = class_getInstanceMethod(object_getClass(NSApp), @selector(currentEvent));
        original_ = method_setImplementation(method_, reinterpret_cast<IMP>(testCurrentEvent));
    }
    ~CurrentEvent() {
        method_setImplementation(method_, original_);
        injectedEvent = nil;
    }
};

NSEvent *otherEvent(NSEventType type) {
    return [NSEvent otherEventWithType:type location:NSZeroPoint modifierFlags:0
                           timestamp:0 windowNumber:0 context:nil subtype:255 data1:0 data2:0];
}

NSEvent *mouseEvent(NSEventType type, NSInteger count) {
    return [NSEvent mouseEventWithType:type location:NSZeroPoint modifierFlags:0
                           timestamp:0 windowNumber:0 context:nil eventNumber:0 clickCount:count pressure:0];
}

id<EditHereQtTrayDelegate> delegateFor(QSystemTrayIcon &tray) {
    auto *privateTray = static_cast<QSystemTrayIconPrivate *>(QObjectPrivate::get(&tray));
    Class delegateClass = NSClassFromString(@"QStatusItemDelegate");
    return [(id<EditHereQtTrayDelegate>)[delegateClass alloc] initWithSysTray:privateTray->qpa_sys];
}

QIcon trayIcon() {
    QPixmap image(16, 16);
    image.fill(Qt::blue);
    return QIcon(image);
}
}

class CocoaTrayTests : public QObject {
    Q_OBJECT
    IMP originalClicked_ = nullptr;
    IMP originalTracking_ = nullptr;
  private slots:
    void initTestCase() {
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("cocoa"));
        Class delegateClass = NSClassFromString(@"QStatusItemDelegate");
        QVERIFY(delegateClass);
        originalClicked_ = method_getImplementation(class_getInstanceMethod(delegateClass, @selector(statusItemClicked)));
        originalTracking_ = method_getImplementation(class_getInstanceMethod(delegateClass, @selector(statusItemMenuBeganTracking:)));
        QString error;
        QVERIFY2(h2d::installCocoaTrayEventGuard(&error), qPrintable(error));
        QVERIFY(error.isEmpty());
        // Repeated Controller construction must not wrap a wrapper recursively.
        QVERIFY2(h2d::installCocoaTrayEventGuard(&error), qPrintable(error));
    }

    void unguardedQtRejectsKitDefinedEvent() {
        QSystemTrayIcon tray(trayIcon());
        tray.show();
        auto delegate = delegateFor(tray);
        QVERIFY(delegate);
        CurrentEvent event(otherEvent(NSEventTypeAppKitDefined));
        // Negative control: invoke the actual bundled Qt implementation, not a
        // duplicate of its clickCount logic. Catching the native exception keeps
        // the regression runner alive where an unguarded application aborts.
        bool clickThrew = false, trackingThrew = false;
        @try {
            reinterpret_cast<void (*)(id, SEL)>(originalClicked_)(delegate, @selector(statusItemClicked));
        } @catch (NSException *exception) {
            clickThrew = [exception.name isEqualToString:NSInternalInconsistencyException];
        }
        @try {
            reinterpret_cast<void (*)(id, SEL, NSNotification *)>(originalTracking_)(
                delegate, @selector(statusItemMenuBeganTracking:), nil);
        } @catch (NSException *exception) {
            trackingThrew = [exception.name isEqualToString:NSInternalInconsistencyException];
        }
        QVERIFY(clickThrew);
        QVERIFY(trackingThrew);
    }

    void nonMouseCallbacksDoNotParseClicks() {
        QSystemTrayIcon tray(trayIcon());
        tray.show();
        auto delegate = delegateFor(tray);
        QVERIFY(delegate);
        QSignalSpy activated(&tray, &QSystemTrayIcon::activated);
        NSEvent *keyboard = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:0 timestamp:0 windowNumber:0 context:nil characters:@"a"
            charactersIgnoringModifiers:@"a" isARepeat:NO keyCode:0];
        for (NSEvent *nativeEvent : {otherEvent(NSEventTypeAppKitDefined), otherEvent(NSEventTypeSystemDefined),
                                   otherEvent(NSEventTypeApplicationDefined), keyboard, (NSEvent *)nil}) {
            CurrentEvent event(nativeEvent);
            @try {
                [delegate statusItemClicked];
                [delegate statusItemMenuBeganTracking:nil];
            } @catch (NSException *exception) {
                QFAIL(qPrintable(QString::fromNSString(exception.reason)));
            }
        }
        QCOMPARE(activated.count(), 0);
    }

    void mouseCallbacksPreserveQtActivation() {
        QSystemTrayIcon tray(trayIcon());
        tray.show();
        auto delegate = delegateFor(tray);
        QVERIFY(delegate);
        QSignalSpy activated(&tray, &QSystemTrayIcon::activated);
        const struct { NSEventType type; NSInteger count; QSystemTrayIcon::ActivationReason reason; } cases[] = {
            {NSEventTypeLeftMouseDown, 1, QSystemTrayIcon::Trigger},
            {NSEventTypeLeftMouseDown, 2, QSystemTrayIcon::DoubleClick},
            {NSEventTypeRightMouseDown, 1, QSystemTrayIcon::Context},
        };
        for (const auto &input : cases) {
            CurrentEvent event(mouseEvent(input.type, input.count));
            [delegate statusItemClicked];
            [delegate statusItemMenuBeganTracking:nil];
            QCOMPARE(activated.count(), 2);
            for (const auto &signal : activated)
                QCOMPARE(qvariant_cast<QSystemTrayIcon::ActivationReason>(signal.at(0)), input.reason);
            activated.clear();
        }
    }

    void nativeMenuStillOpensAndRunsActions() {
        QMenu menu;
        int actions = 0;
        menu.addAction(QStringLiteral("Regression action"), [&] { ++actions; });
        QSystemTrayIcon tray(trayIcon());
        tray.setContextMenu(&menu);
        tray.show();
        NSMenu *nativeMenu = menu.toNSMenu();
        QVERIFY(nativeMenu);
        QSignalSpy activated(&tray, &QSystemTrayIcon::activated);
        QSignalSpy opened(&menu, &QMenu::aboutToShow);
        CurrentEvent event(otherEvent(NSEventTypeAppKitDefined));
        // This notification is the exact entry into the Qt delegate in #11.
        [NSNotificationCenter.defaultCenter postNotificationName:NSMenuDidBeginTrackingNotification object:nativeMenu];
        QCOMPARE(activated.count(), 0);
        NSTimer *closeMenu = [NSTimer timerWithTimeInterval:0.05 repeats:NO block:^(NSTimer *) {
            [nativeMenu cancelTracking];
        }];
        [NSRunLoop.currentRunLoop addTimer:closeMenu forMode:NSEventTrackingRunLoopMode];
        [NSRunLoop.currentRunLoop addTimer:closeMenu forMode:NSRunLoopCommonModes];
        @try {
            [nativeMenu popUpMenuPositioningItem:nil atLocation:NSMakePoint(20, 20) inView:nil];
        } @catch (NSException *exception) {
            [closeMenu invalidate];
            QFAIL(qPrintable(QString::fromNSString(exception.reason)));
        }
        [closeMenu invalidate];
        QVERIFY(opened.count() >= 1);
        QCOMPARE(activated.count(), 0);
        [nativeMenu performActionForItemAtIndex:0];
        QCOMPARE(actions, 1);
    }
};

QTEST_MAIN(CocoaTrayTests)
#include "cocoa_tray_test.moc"
