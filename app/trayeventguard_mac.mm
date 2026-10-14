#include "trayeventguard_mac.h"
#include <QGuiApplication>
#include <QThread>
#include <QVersionNumber>
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <cstring>

namespace {
IMP clickedOriginal = nullptr;
IMP trackingOriginal = nullptr;

bool currentEventIsMouseEvent() {
    NSEvent *event = NSApp.currentEvent;
    if (!event)
        return false;
    switch (event.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
    case NSEventTypeMouseMoved:
        return true;
    default:
        return false;
    }
}

void guardedClick(id delegate, SEL selector) {
    if (currentEventIsMouseEvent())
        reinterpret_cast<void (*)(id, SEL)>(clickedOriginal)(delegate, selector);
}

void guardedTracking(id delegate, SEL selector, NSNotification *notification) {
    if (currentEventIsMouseEvent())
        reinterpret_cast<void (*)(id, SEL, NSNotification *)>(trackingOriginal)(delegate, selector, notification);
}

bool hasCallbackSignature(Method method, unsigned arguments) {
    if (!method || method_getNumberOfArguments(method) != arguments)
        return false;
    char type[16]{};
    method_getReturnType(method, type, sizeof(type));
    if (std::strcmp(type, "v") != 0)
        return false;
    method_getArgumentType(method, 0, type, sizeof(type));
    if (std::strcmp(type, "@") != 0)
        return false;
    method_getArgumentType(method, 1, type, sizeof(type));
    if (std::strcmp(type, ":") != 0)
        return false;
    if (arguments == 3) {
        method_getArgumentType(method, 2, type, sizeof(type));
        if (std::strcmp(type, "@") != 0)
            return false;
    }
    return true;
}
}

namespace h2d {
bool installCocoaTrayEventGuard(QString *error) {
    if (error)
        error->clear();
    if (QGuiApplication::platformName() != "cocoa")
        return true;
    Q_ASSERT(QThread::currentThread() == qGuiApp->thread());

    // Our distribution uses Qt 6.8.3. These two Qt callbacks only call
    // QCocoaSystemTrayIcon::emitActivated(), which reads clickCount unchecked.
    // Qt's upstream fix checks the event type and emits Unknown for non-mouse
    // events: https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/cocoa/qcocoasystemtrayicon.mm
    // EditHere ignores Unknown, so skip only this activation parsing and let
    // AppKit continue opening the native menu. Never replace NSEvent methods.
    // Recheck the private delegate contract when upgrading the Qt minor version.
    const auto version = QVersionNumber::fromString(QString::fromLatin1(qVersion()));
    Class delegateClass = NSClassFromString(@"QStatusItemDelegate");
    Method clicked = class_getInstanceMethod(delegateClass, @selector(statusItemClicked));
    Method tracking = class_getInstanceMethod(delegateClass, @selector(statusItemMenuBeganTracking:));
    if (version.majorVersion() != 6 || version.minorVersion() != 8 ||
        !delegateClass || !hasCallbackSignature(clicked, 2) || !hasCallbackSignature(tracking, 3)) {
        if (error)
            *error = QStringLiteral("Cannot guard the Cocoa tray callbacks for Qt %1; the tray remains hidden.")
                         .arg(QString::fromLatin1(qVersion()));
        return false;
    }
    if (clickedOriginal)
        return true;
    clickedOriginal = method_setImplementation(clicked, reinterpret_cast<IMP>(guardedClick));
    trackingOriginal = method_setImplementation(tracking, reinterpret_cast<IMP>(guardedTracking));
    return true;
}
}
