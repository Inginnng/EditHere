#include "platform.h"
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <memory>
namespace h2d {
namespace {
QImage fromCGImage(CGImageRef image) {
    if (!image)
        return {};
    size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    if (!w || !h || w * h > MaxPixels)
        return {};
    QImage result(int(w), int(h), QImage::Format_RGBA8888_Premultiplied);
    result.fill(Qt::transparent);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(result.bits(), w, h, 8, result.bytesPerLine(), space,
                                                 kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!context)
        return {};
    CGContextDrawImage(context, CGRectMake(0, 0, w, h), image);
    CGContextRelease(context);
    return result;
}
QString attributeText(AXUIElementRef element, CFStringRef attribute) {
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value)
        return {};
    QString text;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        NSString *str = (__bridge NSString *)value;
        text = QString::fromUtf8(str.UTF8String);
    }
    CFRelease(value);
    return text;
}
} // namespace
void captureScreens(CaptureCallback callback) {
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        callback({}, "请在系统设置 → 隐私与安全性 → 屏幕录制中允许 Help2Design，然后重试。");
        return;
    }
    struct State {
        QVector<ScreenFrame> frames;
        CaptureCallback callback;
        int pending = 0;
        QString error;
    };
    auto state = std::make_shared<State>();
    state->callback = std::move(callback);
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:YES
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent *content, NSError *error) {
                                   QMetaObject::invokeMethod(
                                       qApp,
                                       [state, content, error] {
                                           if (error || !content.displays.count) {
                                               state->callback({}, "无法获取屏幕，请检查屏幕录制权限");
                                               return;
                                           }
                                           state->pending = int(content.displays.count);
                                           for (SCDisplay *display in content.displays) {
                                               CGRect bounds = CGDisplayBounds(display.displayID);
                                               QRect logical(qRound(bounds.origin.x), qRound(bounds.origin.y),
                                                             qRound(bounds.size.width),
                                                             qRound(bounds.size.height));
                                               for (QScreen *screen : QGuiApplication::screens())
                                                   if (screen->geometry().topLeft() == logical.topLeft()) {
                                                       logical = screen->geometry();
                                                       break;
                                                   }
                                               SCContentFilter *filter =
                                                   [[SCContentFilter alloc] initWithDisplay:display
                                                                           excludingWindows:@[]];
                                               SCStreamConfiguration *config = [SCStreamConfiguration new];
                                               config.width = CGDisplayPixelsWide(display.displayID);
                                               config.height = CGDisplayPixelsHigh(display.displayID);
                                               config.showsCursor = NO;
                                               const QString name = QString::number(display.displayID);
                                               [SCScreenshotManager
                                                   captureImageWithFilter:filter
                                                            configuration:config
                                                        completionHandler:^(CGImageRef image,
                                                                            NSError *captureError) {
                                                          QImage pixels = fromCGImage(image);
                                                          bool failed =
                                                              captureError != nil || pixels.isNull();
                                                          QMetaObject::invokeMethod(
                                                              qApp,
                                                              [state, pixels, logical, name, failed] {
                                                                  if (!failed)
                                                                      state->frames.append({name, logical,
                                                                                            logical, pixels,
                                                                                            false});
                                                                  else
                                                                      state->error = "部分屏幕无法采集";
                                                                  if (--state->pending == 0)
                                                                      state->callback(state->frames,
                                                                                      state->frames.isEmpty()
                                                                                          ? "无法读取屏幕画面"
                                                                                          : state->error);
                                                              },
                                                              Qt::QueuedConnection);
                                                        }];
                                           }
                                       },
                                       Qt::QueuedConnection);
                                 }];
}
QVector<Candidate> nativeElementsAt(QPoint point, qint64 excludedPid) {
    QVector<Candidate> output;
    if (!AXIsProcessTrusted())
        return output;
    pid_t targetPid = 0;
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    for (NSDictionary *info in (__bridge NSArray *)windows) {
        pid_t pid = [info[(__bridge NSString *)kCGWindowOwnerPID] intValue];
        if (pid == excludedPid)
            continue;
        CGRect bounds{};
        if (CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &bounds) &&
            CGRectContainsPoint(bounds, CGPointMake(point.x(), point.y()))) {
            targetPid = pid;
            break;
        }
    }
    if (windows)
        CFRelease(windows);
    if (!targetPid)
        return output;
    AXUIElementRef system = AXUIElementCreateApplication(targetPid);
    AXUIElementSetMessagingTimeout(system, 0.15);
    AXUIElementRef element = nullptr;
    AXUIElementCopyElementAtPosition(system, point.x(), point.y(), &element);
    CFRelease(system);
    for (int depth = 0; element && depth < 14; depth++) {
        CFTypeRef pos = nullptr, size = nullptr;
        AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &pos);
        AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &size);
        CGPoint p{};
        CGSize s{};
        if (pos && size && CFGetTypeID(pos) == AXValueGetTypeID() &&
            CFGetTypeID(size) == AXValueGetTypeID() &&
            AXValueGetValue((AXValueRef)pos, kAXValueCGPointType, &p) &&
            AXValueGetValue((AXValueRef)size, kAXValueCGSizeType, &s) && s.width > 0 && s.height > 0) {
            auto target = manualTarget();
            target["source"] = "accessibility";
            target["method"] = "macos-ax";
            target["controlType"] = attributeText(element, kAXRoleAttribute);
            QString label = attributeText(element, kAXTitleAttribute);
            if (label.isEmpty())
                label = attributeText(element, kAXDescriptionAttribute);
            if (label.isEmpty())
                label = target["controlType"].toString();
            target["label"] = label.left(1000);
            output.append({QRect(qRound(p.x), qRound(p.y), qRound(s.width), qRound(s.height)), target});
        }
        if (pos)
            CFRelease(pos);
        if (size)
            CFRelease(size);
        CFTypeRef parent = nullptr;
        AXUIElementCopyAttributeValue(element, kAXParentAttribute, &parent);
        CFRelease(element);
        element = parent && CFGetTypeID(parent) == AXUIElementGetTypeID() ? (AXUIElementRef)parent : nullptr;
        if (parent && !element)
            CFRelease(parent);
    }
    if (element)
        CFRelease(element);
    return output;
}
bool requestAccessibility() {
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}
void configureNativeWindow(QWidget *widget, bool overlay) {
    // Offscreen test windows do not have an NSView-backed native handle.
    if (QGuiApplication::platformName() != "cocoa")
        return;
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    NSWindow *window = view.window;
    if (!window)
        return;
    window.collectionBehavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    if (overlay) {
        window.level = NSStatusWindowLevel + 1;
        window.hasShadow = NO;
    } else
        window.level = NSFloatingWindowLevel;
}
QString globalShortcutLabel() {
    return "⌘ + Shift + 2";
}
GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() {
    if (handle_)
        UnregisterEventHotKey((EventHotKeyRef)handle_);
    if (handler_)
        RemoveEventHandler((EventHandlerRef)handler_);
}
bool GlobalShortcut::start() {
    EventTypeSpec event{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    OSStatus status = InstallApplicationEventHandler(
        [](EventHandlerCallRef, EventRef, void *data) -> OSStatus {
            auto self = static_cast<GlobalShortcut *>(data);
            QMetaObject::invokeMethod(self, [self] { emit self->triggered(); }, Qt::QueuedConnection);
            return noErr;
        },
        1, &event, this, &handler);
    if (status != noErr)
        return false;
    handler_ = handler;
    EventHotKeyRef key = nullptr;
    EventHotKeyID id{'H2DS', 1};
    if (RegisterEventHotKey(kVK_ANSI_2, cmdKey | shiftKey, id, GetApplicationEventTarget(), 0, &key) != noErr)
        return false;
    handle_ = key;
    return true;
}
bool GlobalShortcut::nativeEventFilter(const QByteArray &, void *, qintptr *) {
    return false;
}
} // namespace h2d
