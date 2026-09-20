#include "platform.h"
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
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

constexpr OSType ShortcutSignature = 0x48324453; // H2DS
bool carbonKey(Qt::Key key, UInt32 &code) {
    static constexpr UInt32 letters[] = {
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
        kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
        kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
        kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
    static constexpr UInt32 digits[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
                                        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
    static constexpr UInt32 functions[] = {kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,
                                           kVK_F8,  kVK_F9,  kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
                                           kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        code = letters[key - Qt::Key_A];
        return true;
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        code = digits[key - Qt::Key_0];
        return true;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) {
        code = functions[key - Qt::Key_F1];
        return true;
    }
    switch (key) {
    case Qt::Key_Space:
        code = kVK_Space;
        break;
    case Qt::Key_Tab:
        code = kVK_Tab;
        break;
    case Qt::Key_Backspace:
        code = kVK_Delete;
        break;
    case Qt::Key_Return:
        code = kVK_Return;
        break;
    case Qt::Key_Enter:
        code = kVK_ANSI_KeypadEnter;
        break;
    case Qt::Key_Escape:
        code = kVK_Escape;
        break;
    case Qt::Key_Insert:
        code = kVK_Help;
        break;
    case Qt::Key_Delete:
        code = kVK_ForwardDelete;
        break;
    case Qt::Key_Home:
        code = kVK_Home;
        break;
    case Qt::Key_End:
        code = kVK_End;
        break;
    case Qt::Key_Left:
        code = kVK_LeftArrow;
        break;
    case Qt::Key_Right:
        code = kVK_RightArrow;
        break;
    case Qt::Key_Up:
        code = kVK_UpArrow;
        break;
    case Qt::Key_Down:
        code = kVK_DownArrow;
        break;
    case Qt::Key_PageUp:
        code = kVK_PageUp;
        break;
    case Qt::Key_PageDown:
        code = kVK_PageDown;
        break;
    default:
        return false;
    }
    return true;
}
} // namespace
void prepareScreenCapture(QObject *context, std::function<void()> ready) {
    // Cocoa finishes dismissing the status-item menu after its action returns.
    QTimer::singleShot(250, context, std::move(ready));
}
void captureScreens(CaptureCallback callback) {
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        callback({}, "请在系统设置 → 隐私与安全性 → 屏幕录制中允许 EditHere，然后重试。");
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
            AXValueGetValue((AXValueRef)pos, kAXValueTypeCGPoint, &p) &&
            AXValueGetValue((AXValueRef)size, kAXValueTypeCGSize, &s) && s.width > 0 && s.height > 0) {
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
    if (overlay) {
        window.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        window.level = NSStatusWindowLevel + 1;
        window.hasShadow = NO;
    } else {
        window.collectionBehavior = NSWindowCollectionBehaviorDefault;
        window.level = NSNormalWindowLevel;
    }
}
QString globalShortcutLabel() {
    return QKeySequence("Ctrl+Shift+2", QKeySequence::PortableText).toString(QKeySequence::NativeText);
}
GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() {
    stop();
}
void GlobalShortcut::stop() {
    if (handle_)
        UnregisterEventHotKey((EventHotKeyRef)handle_);
    if (handler_)
        RemoveEventHandler((EventHandlerRef)handler_);
    handle_ = nullptr;
    handler_ = nullptr;
    shortcutId_ = 0;
    sequence_ = {};
    lastError_.clear();
}
bool GlobalShortcut::start(const QKeySequence &sequence) {
    lastError_.clear();
    if (sequence.isEmpty()) {
        stop();
        return true;
    }
    if (handle_ && sequence == sequence_)
        return true;
    if (sequence.count() != 1) {
        lastError_ = "全局截图快捷键只支持一组按键，不能使用连续组合。";
        return false;
    }
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    const auto supported = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    UInt32 key = 0;
    if (!carbonKey(combination.key(), key) || (modifiers & ~supported)) {
        lastError_ = "不支持此按键，请使用字母、数字、F1–F20 或方向与导航键。";
        return false;
    }
    UInt32 nativeModifiers = 0;
    // Qt uses ControlModifier for Command and MetaModifier for physical Control on macOS.
    if (modifiers.testFlag(Qt::ControlModifier))
        nativeModifiers |= cmdKey;
    if (modifiers.testFlag(Qt::MetaModifier))
        nativeModifiers |= controlKey;
    if (modifiers.testFlag(Qt::AltModifier))
        nativeModifiers |= optionKey;
    if (modifiers.testFlag(Qt::ShiftModifier))
        nativeModifiers |= shiftKey;
    EventHandlerRef newHandler = nullptr;
    if (!handler_) {
        EventTypeSpec event{kEventClassKeyboard, kEventHotKeyPressed};
        const OSStatus installed = InstallApplicationEventHandler(
            [](EventHandlerCallRef, EventRef event, void *data) -> OSStatus {
                auto self = static_cast<GlobalShortcut *>(data);
                EventHotKeyID fired{};
                if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                      sizeof(fired), nullptr, &fired) != noErr ||
                    fired.signature != ShortcutSignature || !self->handle_ || fired.id != self->shortcutId_)
                    return eventNotHandledErr;
                const UInt32 expectedId = fired.id;
                QMetaObject::invokeMethod(
                    self,
                    [self, expectedId] {
                        if (self->handle_ && self->shortcutId_ == expectedId)
                            emit self->triggered();
                    },
                    Qt::QueuedConnection);
                return noErr;
            },
            1, &event, this, &newHandler);
        if (installed != noErr) {
            lastError_ = QString("无法监听系统快捷键（错误 %1）。").arg(installed);
            return false;
        }
    }
    static std::atomic<UInt32> nextId{1};
    const EventHotKeyID newId{ShortcutSignature, nextId.fetch_add(1)};
    EventHotKeyRef newKey = nullptr;
    const OSStatus registered =
        RegisterEventHotKey(key, nativeModifiers, newId, GetApplicationEventTarget(), 0, &newKey);
    if (registered != noErr) {
        if (newHandler)
            RemoveEventHandler(newHandler);
        lastError_ = QString("此快捷键已被占用或系统无法注册（错误 %1），请更换一组按键。").arg(registered);
        return false;
    }
    // Keep the existing shortcut and handler alive until the replacement is registered.
    if (handle_)
        UnregisterEventHotKey((EventHotKeyRef)handle_);
    handle_ = newKey;
    if (newHandler)
        handler_ = newHandler;
    shortcutId_ = newId.id;
    sequence_ = sequence;
    return true;
}
bool GlobalShortcut::nativeEventFilter(const QByteArray &, void *, qintptr *) {
    return false;
}
} // namespace h2d
