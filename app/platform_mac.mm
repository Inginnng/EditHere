#include "platform.h"
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
namespace h2d {
namespace {
// Free functions have no tr(); the enclosing "h2d" context groups them so the
// translation file stays easy to review.
inline QString tr(const char *text) {
    return QCoreApplication::translate("h2d", text);
}
QImage fromCGImage(CGImageRef image) {
    if (!image)
        return {};
    size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    if (!w || !h || w * h > MaxPixels)
        return {};
    QImage result(int(w), int(h), QImage::Format_RGBA8888_Premultiplied);
    if (result.isNull())
        return {};
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

struct MacScrollSession {
    CGDirectDisplayID display = kCGNullDirectDisplay;
    QRect nativeGeometry;
    CGRect displayBounds{};
    QSize backingMode;
    __strong SCContentFilter *filter = nil;
    bool excludesOwnApplication = false;
};
// All access is on the Qt GUI thread. The shared owner keeps a pending native
// callback safe after cancellation, without letting it replace the next session.
std::shared_ptr<MacScrollSession> scrollingSession;

bool screenRecordingAllowed(QString *reason, bool request) {
    if (CGPreflightScreenCaptureAccess() || (request && CGRequestScreenCaptureAccess())) {
        if (reason)
            reason->clear();
        return true;
    }
    if (reason)
        *reason = tr("请在系统设置 → 隐私与安全性 → 屏幕录制中允许 EditHere，然后重试。");
    return false;
}
QSize displayBackingMode(CGDirectDisplayID display) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    if (!mode)
        return {};
    const QSize size(int(CGDisplayModeGetPixelWidth(mode)), int(CGDisplayModeGetPixelHeight(mode)));
    CGDisplayModeRelease(mode);
    return size;
}
bool validDisplay(const std::shared_ptr<MacScrollSession> &session) {
    return session && CGDisplayIsActive(session->display) &&
           CGRectEqualToRect(CGDisplayBounds(session->display), session->displayBounds) &&
           displayBackingMode(session->display) == session->backingMode;
}
CGPoint scrollPoint(QPoint nativePoint, const MacScrollSession &session) {
    // nativeGeometry deliberately preserves the selected frame's origin; the
    // screen-local offset, rather than a global DPR, determines Quartz points.
    const QPoint local = nativePoint - session.nativeGeometry.topLeft();
    return CGPointMake(session.displayBounds.origin.x +
                           local.x() * session.displayBounds.size.width / session.nativeGeometry.width(),
                       session.displayBounds.origin.y +
                           local.y() * session.displayBounds.size.height / session.nativeGeometry.height());
}
ScrollCaptureTarget scrollWindowAt(CGPoint point) {
    const pid_t ownPid = NSProcessInfo.processInfo.processIdentifier;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    ScrollCaptureTarget result;
    // CGWindowListCopyWindowInfo returns windows in front-to-back order. Skip
    // our input-transparent live frame and menu, but keep other apps' popups as
    // blockers rather than silently sending to a window underneath them.
    for (NSDictionary *info in (__bridge NSArray *)list) {
        const pid_t pid = [info[(__bridge NSString *)kCGWindowOwnerPID] intValue];
        if (pid <= 0 || pid == ownPid ||
            [info[(__bridge NSString *)kCGWindowAlpha] doubleValue] <= 0)
            continue;
        CGRect bounds{};
        if (!CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &bounds) ||
            !CGRectContainsPoint(bounds, point))
            continue;
        result = {quintptr([info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue]), quint32(pid)};
        break;
    }
    if (list)
        CFRelease(list);
    return result;
}
bool scrollWindowExists(const ScrollCaptureTarget &target) {
    if (!target.window || !target.processId ||
        target.processId == quint32(NSProcessInfo.processInfo.processIdentifier))
        return false;
    CFArrayRef list = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    bool found = false;
    for (NSDictionary *info in (__bridge NSArray *)list) {
        if ([info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue] == target.window &&
            [info[(__bridge NSString *)kCGWindowOwnerPID] unsignedIntValue] == target.processId &&
            [info[(__bridge NSString *)kCGWindowAlpha] doubleValue] > 0) {
            found = true;
            break;
        }
    }
    if (list)
        CFRelease(list);
    return found;
}
SCContentFilter *displayFilter(SCShareableContent *content, SCDisplay *display, bool *excludesOwn) {
    NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
    const pid_t pid = NSProcessInfo.processInfo.processIdentifier;
    for (SCRunningApplication *application in content.applications)
        if (application.processID == pid)
            [excluded addObject:application];
    if (excludesOwn)
        *excludesOwn = excluded.count != 0;
    // Exclude the application, not just its current SCWindow snapshot. New
    // preview, tooltip and crop-menu windows are then excluded automatically.
    // https://developer.apple.com/documentation/screencapturekit/sccontentfilter/init(display:excludingapplications:exceptingwindows:)
    return [[SCContentFilter alloc] initWithDisplay:display
                             excludingApplications:excluded
                                  exceptingWindows:@[]];
}
QSize nativeDisplaySize(SCContentFilter *filter, CGRect bounds) {
    const double scale = filter.pointPixelScale;
    if (!std::isfinite(scale) || scale <= 0 ||
        bounds.size.width * scale > std::numeric_limits<int>::max() ||
        bounds.size.height * scale > std::numeric_limits<int>::max())
        return {};
    return {qRound(bounds.size.width * scale), qRound(bounds.size.height * scale)};
}
QRect logicalDisplayGeometry(CGRect bounds) {
    QRect logical(qRound(bounds.origin.x), qRound(bounds.origin.y),
                  qRound(bounds.size.width), qRound(bounds.size.height));
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->geometry().topLeft() == logical.topLeft())
            return screen->geometry();
    return logical;
}
struct RegionRequest {
    ScrollRegionCallback callback;
    bool completed = false;
    void finish(QImage image, QString error) {
        // Screenshot completion and the timeout are delivered on the GUI thread.
        if (completed)
            return;
        completed = true;
        auto done = std::move(callback);
        done(std::move(image), std::move(error));
    }
};
void captureSessionRegion(const std::shared_ptr<MacScrollSession> &session, const QRect &nativeRegion,
                          const std::shared_ptr<RegionRequest> &request) {
    if (request->completed)
        return;
    if (scrollingSession != session || !validDisplay(session)) {
        request->finish({}, tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
        return;
    }
    const QPoint local = nativeRegion.topLeft() - session->nativeGeometry.topLeft();
    const double sx = session->displayBounds.size.width / session->nativeGeometry.width();
    const double sy = session->displayBounds.size.height / session->nativeGeometry.height();
    SCStreamConfiguration *configuration = [SCStreamConfiguration new];
    // sourceRect is in display-local screen points; output width/height are in
    // pixels. This remains one-to-one at Retina scale and negative origins.
    configuration.sourceRect = CGRectMake(local.x() * sx, local.y() * sy,
                                          nativeRegion.width() * sx, nativeRegion.height() * sy);
    configuration.width = nativeRegion.width();
    configuration.height = nativeRegion.height();
    configuration.showsCursor = NO;
    configuration.colorSpaceName = kCGColorSpaceSRGB;
    auto onImage = ^(CGImageRef image, NSError *error) {
        QImage pixels = error ? QImage() : fromCGImage(image);
        const QString details = error
            ? QString::fromUtf8(error.localizedDescription.UTF8String) : QString();
        QMetaObject::invokeMethod(qApp, [session, nativeRegion, request, pixels, details] {
            if (scrollingSession != session || !validDisplay(session)) {
                request->finish({}, tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
                return;
            }
            if (pixels.isNull() || pixels.size() != nativeRegion.size()) {
                request->finish({}, details.isEmpty()
                    ? tr("无法读取长截图区域，请检查屏幕采集权限。")
                    : tr("无法读取长截图区域：%1").arg(details));
                return;
            }
            request->finish(pixels, {});
        }, Qt::QueuedConnection);
    };
    [SCScreenshotManager captureImageWithFilter:session->filter
                                 configuration:configuration
                             completionHandler:onImage];
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
bool supportsScrollingCapture(QString *reason) {
    if (QGuiApplication::platformName() != "cocoa") {
        if (reason)
            *reason = tr("长截图需要在 macOS 桌面会话中运行。");
        return false;
    }
    // Manual capture only reads pixels; accessibility/post-event permission is
    // needed exclusively when the user turns on automatic scrolling.
    return screenRecordingAllowed(reason, false);
}
bool supportsAutomaticScrollInput(QString *reason) {
    if (reason)
        reason->clear();
    if (QGuiApplication::platformName() == "cocoa" &&
        AXIsProcessTrusted() && CGPreflightPostEventAccess())
        return true;
    if (reason)
        *reason = tr("自动滚动需要辅助功能权限。请在系统设置 → 隐私与安全性 → 辅助功能中允许 EditHere；也可继续手动滚动。");
    return false;
}
bool beginScrollingCapture(const ScreenFrame &frame, const QRect &nativeRegion, QString *error) {
    endScrollingCapture();
    if (!supportsScrollingCapture(error))
        return false;
    bool parsed = false;
    const uint displayId = frame.name.toUInt(&parsed);
    if (!parsed || !displayId || !frame.nativePixels || frame.nativeGeometry.isEmpty() ||
        frame.nativeGeometry.size() != frame.image.size() || nativeRegion.isEmpty() ||
        !frame.nativeGeometry.contains(nativeRegion) || !CGDisplayIsActive(displayId)) {
        if (error)
            *error = tr("长截图区域无效或过大。");
        return false;
    }
    const CGRect bounds = CGDisplayBounds(displayId);
    const QRect logical = logicalDisplayGeometry(bounds);
    if (logical != frame.logicalGeometry) {
        if (error)
            *error = tr("长截图屏幕已断开或显示设置已改变，请重新截图。");
        return false;
    }
    auto session = std::make_shared<MacScrollSession>();
    session->display = displayId;
    session->displayBounds = bounds;
    session->nativeGeometry = frame.nativeGeometry;
    session->backingMode = displayBackingMode(displayId);
    if (session->backingMode.isEmpty()) {
        if (error)
            *error = tr("长截图屏幕已断开或显示设置已改变，请重新截图。");
        return false;
    }
    scrollingSession = std::move(session);
    return true;
}
void endScrollingCapture(bool) {
    scrollingSession.reset();
}
void prepareScrollFrameRead() {}
ScrollCaptureTarget scrollCaptureTargetAt(QPoint nativePoint) {
    const auto session = scrollingSession;
    if (!validDisplay(session) || !session->nativeGeometry.contains(nativePoint))
        return {};
    return scrollWindowAt(scrollPoint(nativePoint, *session));
}
void focusScrollingCaptureTarget(const ScrollCaptureTarget &target) {
    if (!scrollWindowExists(target))
        return;
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(target.processId)];
    if (application && !application.terminated)
        [application activateWithOptions:NSApplicationActivateIgnoringOtherApps];
}
bool scrollCaptureStep(const ScrollCaptureTarget &target, QPoint nativePoint, int wheelSteps,
                       QString *error) {
    if (error)
        error->clear();
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    const auto session = scrollingSession;
    if (!validDisplay(session))
        return fail(tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
    if (!wheelSteps || wheelSteps < -8 || wheelSteps > 8 ||
        !session->nativeGeometry.contains(nativePoint))
        return fail(tr("滚动步长或屏幕坐标无效。"));
    if (!scrollWindowExists(target))
        return fail(tr("原滚动窗口已关闭或不可见，长截图已停止。"));
    const CGPoint point = scrollPoint(nativePoint, *session);
    const auto current = scrollWindowAt(point);
    if (current.window != target.window || current.processId != target.processId)
        return fail(tr("滚动区域被其他窗口遮挡，长截图已停止。"));
    if (!supportsAutomaticScrollInput(error))
        return false;
    CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, -wheelSteps);
    if (!event)
        return fail(tr("无法创建 macOS 滚动事件，请继续手动滚动。"));
    CGEventSetLocation(event, point);
    CGEventSetFlags(event, 0);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, target.window);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent,
                               target.window);
    // Post to the locked process rather than to whichever app gains focus.
    // Delivery has no success response; Controller verifies subsequent pixels.
    CGEventPostToPid(pid_t(target.processId), event);
    CFRelease(event);
    return true;
}
void captureScrollRegion(const QRect &nativeRegion, ScrollRegionCallback callback) {
    const auto session = scrollingSession;
    QString permissionError;
    if (!screenRecordingAllowed(&permissionError, false)) {
        callback({}, permissionError);
        return;
    }
    if (!validDisplay(session)) {
        callback({}, tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
        return;
    }
    if (nativeRegion.isEmpty() || !session->nativeGeometry.contains(nativeRegion) ||
        qint64(nativeRegion.width()) * nativeRegion.height() > MaxPixels) {
        callback({}, tr("长截图区域无效或过大。"));
        return;
    }
    auto request = std::make_shared<RegionRequest>();
    request->callback = std::move(callback);
    QTimer::singleShot(8000, qApp, [request] {
        request->finish({}, tr("macOS 屏幕采集超时，请检查屏幕录制权限后重试。"));
    });
    if (session->filter) {
        captureSessionRegion(session, nativeRegion, request);
        return;
    }
    // Acquire the app-exclusion filter after the live UI exists, once per
    // session. Keeping it avoids enumerating all windows every captured frame.
    auto onContent = ^(SCShareableContent *content, NSError *error) {
        QMetaObject::invokeMethod(qApp, [session, nativeRegion, request, content, error] {
            if (request->completed)
                return;
            if (scrollingSession != session || !validDisplay(session)) {
                request->finish({}, tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
                return;
            }
            SCDisplay *selected = nil;
            for (SCDisplay *display in content.displays)
                if (display.displayID == session->display) {
                    selected = display;
                    break;
                }
            if (error || !selected) {
                request->finish({}, tr("无法获取屏幕，请检查屏幕录制权限"));
                return;
            }
            bool excluded = false;
            SCContentFilter *filter = displayFilter(content, selected, &excluded);
            if (!excluded) {
                request->finish({}, tr("无法从长截图中排除 EditHere 窗口，请重新截图。"));
                return;
            }
            if (nativeDisplaySize(filter, session->displayBounds) != session->nativeGeometry.size()) {
                request->finish({}, tr("长截图屏幕已断开或显示设置已改变，请重新截图。"));
                return;
            }
            session->filter = filter;
            session->excludesOwnApplication = true;
            captureSessionRegion(session, nativeRegion, request);
        }, Qt::QueuedConnection);
    };
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:NO
                                               completionHandler:onContent];
}
quintptr captureForegroundWindow() {
    NSRunningApplication *application = NSWorkspace.sharedWorkspace.frontmostApplication;
    // Restoring the application activates its original key window, without AX
    // permission; manual long capture must not require accessibility permission.
    return application && application.processIdentifier != NSProcessInfo.processInfo.processIdentifier
        ? quintptr(application.processIdentifier) : 0;
}
void restoreCaptureForegroundWindow(quintptr window) {
    if (!window || window > quintptr(std::numeric_limits<pid_t>::max()))
        return;
    NSRunningApplication *application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(window)];
    if (application && !application.terminated)
        [application activateWithOptions:NSApplicationActivateIgnoringOtherApps];
}
void prepareScreenCapture(QObject *context, std::function<void()> ready, bool) {
    // Cocoa finishes dismissing the status-item menu after its action returns.
    QTimer::singleShot(250, context, std::move(ready));
}
void captureScreens(CaptureCallback callback) {
    QString permissionError;
    if (!screenRecordingAllowed(&permissionError, true)) {
        callback({}, permissionError);
        return;
    }
    struct State {
        QVector<ScreenFrame> frames;
        CaptureCallback callback;
        int pending = 0;
        QString error;
        bool completed = false;
        void finish(QString problem) {
            if (completed)
                return;
            completed = true;
            auto done = std::move(callback);
            done(std::move(frames), std::move(problem));
        }
        void addFrame(QString name, QRect logical, QRect native, QImage pixels) {
            if (completed)
                return;
            if (pixels.isNull() || pixels.size() != native.size()) {
                error = tr("部分屏幕无法采集");
            } else {
                ScreenFrame frame{name, logical, native, pixels, true};
                // AX operates in Quartz points, independently of native pixels.
                frame.elementGeometry = logical;
                frames.append(std::move(frame));
            }
            if (--pending == 0)
                finish(frames.isEmpty() ? tr("无法读取屏幕画面") : error);
        }
    };
    auto state = std::make_shared<State>();
    state->callback = std::move(callback);
    QTimer::singleShot(8000, qApp, [state] {
        state->finish(tr("macOS 屏幕采集超时，请检查屏幕录制权限后重试。"));
    });
    auto onContent = ^(SCShareableContent *content, NSError *error) {
        QMetaObject::invokeMethod(qApp, [state, content, error] {
            if (state->completed)
                return;
            if (error || !content.displays.count) {
                state->finish(tr("无法获取屏幕，请检查屏幕录制权限"));
                return;
            }
            state->pending = int(content.displays.count);
            for (SCDisplay *display in content.displays) {
                const CGRect bounds = CGDisplayBounds(display.displayID);
                const QRect logical = logicalDisplayGeometry(bounds);
                SCContentFilter *filter = displayFilter(content, display, nullptr);
                const QSize size = nativeDisplaySize(filter, bounds);
                const QRect native(logical.topLeft(), size);
                const QString name = QString::number(display.displayID);
                if (size.isEmpty() || qint64(size.width()) * size.height() > MaxPixels) {
                    state->addFrame(name, logical, native, {});
                    continue;
                }
                SCStreamConfiguration *configuration = [SCStreamConfiguration new];
                configuration.width = size.width();
                configuration.height = size.height();
                configuration.showsCursor = NO;
                configuration.colorSpaceName = kCGColorSpaceSRGB;
                auto onImage = ^(CGImageRef image, NSError *captureError) {
                    QImage pixels = captureError ? QImage() : fromCGImage(image);
                    QMetaObject::invokeMethod(qApp, [state, pixels, logical, native, name] {
                        state->addFrame(name, logical, native, pixels);
                    }, Qt::QueuedConnection);
                };
                [SCScreenshotManager captureImageWithFilter:filter
                                             configuration:configuration
                                         completionHandler:onImage];
            }
        }, Qt::QueuedConnection);
    };
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:YES
                                               completionHandler:onContent];
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
bool excludedFromCapture(const QWidget *widget) {
    // NSWindow.sharingType alone does not guarantee ScreenCaptureKit exclusion.
    // Report success only after the session's application filter is installed.
    return widget && QGuiApplication::platformName() == "cocoa" && scrollingSession &&
           scrollingSession->filter && scrollingSession->excludesOwnApplication;
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
        window.sharingType = NSWindowSharingReadWrite;
    }
}
QString globalShortcutLabel() {
    return QKeySequence("Alt+Shift+2", QKeySequence::PortableText).toString(QKeySequence::NativeText);
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
        lastError_ = tr("全局截图快捷键只支持一组按键，不能使用连续组合。");
        return false;
    }
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    const auto supported = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    UInt32 key = 0;
    if (!carbonKey(combination.key(), key) || (modifiers & ~supported)) {
        lastError_ = tr("不支持此按键，请使用字母、数字、F1–F20 或方向与导航键。");
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
            lastError_ = tr("无法监听系统快捷键（错误 %1）。").arg(installed);
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
        lastError_ = tr("此快捷键已被占用或系统无法注册（错误 %1），请更换一组按键。").arg(registered);
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
