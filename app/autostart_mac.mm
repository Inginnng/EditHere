#include "autostart.h"
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

namespace h2d {
namespace {
bool loginLaunch = false;
id launchObserver = nil;

bool currentEventIsLoginLaunch() {
    NSAppleEventDescriptor *event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
    return event.eventClass == kCoreEventClass && event.eventID == kAEOpenApplication &&
           [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem;
}
bool isAppBundle() {
    NSBundle *bundle = NSBundle.mainBundle;
    return [bundle.bundlePath.pathExtension caseInsensitiveCompare:@"app"] == NSOrderedSame &&
           [bundle.bundleIdentifier isEqualToString:@"com.help2design.capture"];
}
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
QString approvalMessage() {
    return "已申请开机自启，但 macOS 尚未允许。请在“系统设置 → 通用 → 登录项与扩展”中允许 HelpDesign，然后重新打开设置确认。";
}
} // namespace

void initializeLaunchAtLoginDetection() {
    if (launchObserver)
        return;
    launchObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:NSApplicationDidFinishLaunchingNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification *) {
                    loginLaunch = currentEventIsLoginLaunch();
                }];
}
bool wasLaunchedAtLogin() {
    return loginLaunch || currentEventIsLoginLaunch();
}

bool launchAtLoginEnabled(QString *error) {
    if (error)
        error->clear();
    // Command-line/test binaries are not login items; other preferences remain available.
    if (!isAppBundle())
        return false;
    const auto status = SMAppService.mainAppService.status;
    return status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval;
}

QString launchAtLoginNotice() {
    if (isAppBundle() && SMAppService.mainAppService.status == SMAppServiceStatusRequiresApproval)
        return approvalMessage();
    return {};
}

bool setLaunchAtLoginEnabled(bool enabled, QString *error) {
    if (error)
        error->clear();
    if (!isAppBundle())
        return !enabled || fail(error, "无法设置开机自启：请从已安装的 HelpDesign.app 中运行应用。");
    SMAppService *service = SMAppService.mainAppService;
    const auto previous = service.status;
    if ((enabled && (previous == SMAppServiceStatusEnabled || previous == SMAppServiceStatusRequiresApproval)) ||
        (!enabled && (previous == SMAppServiceStatusNotRegistered || previous == SMAppServiceStatusNotFound)))
        return true;
    NSError *nativeError = nil;
    const BOOL success = enabled ? [service registerAndReturnError:&nativeError]
                                 : [service unregisterAndReturnError:&nativeError];
    if (!success) {
        // Registration can remain when macOS denies launching pending user approval.
        // The notice API distinguishes that retained request from an approved login item.
        if (enabled && service.status == SMAppServiceStatusRequiresApproval)
            return true;
        return fail(error, QString("无法%1开机自启：%2")
                               .arg(enabled ? "开启" : "关闭",
                                    nativeError ? QString::fromUtf8(nativeError.localizedDescription.UTF8String)
                                                : QString("macOS 未提供错误详情。")));
    }
    const auto status = service.status;
    if (enabled && status == SMAppServiceStatusRequiresApproval)
        return true;
    if ((enabled && status != SMAppServiceStatusEnabled) ||
        (!enabled && status != SMAppServiceStatusNotRegistered && status != SMAppServiceStatusNotFound))
        return fail(error, "macOS 尚未完成更新登录项，请稍后重新打开设置确认。");
    return true;
}
} // namespace h2d
