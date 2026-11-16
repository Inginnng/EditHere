// Compile the production backend in this test target, without h2d_ui, so the
// asynchronous callback can be exercised before it crosses ScreenCaptureKit.
// Native filter construction and screenshot submission are replaced; no
// recording permission or screenshot of the CI desktop is needed.
#include "../app/platform_mac.mm"
#include <QTest>
#import <objc/runtime.h>

namespace {
__strong NSArray *excludedApplications = nil;
}

// Use the public content selectors without relying on private initializers of
// ScreenCaptureKit's enumeration objects or asking CI for recording permission.
@interface EHTestShareableContent : NSObject
@property(copy) NSArray *applications;
@property(copy) NSArray *windows;
@property(copy) NSArray *displays;
@end
@implementation EHTestShareableContent
@end

@interface EHTestShareableItem : NSObject
@property pid_t processID;
@property CGDirectDisplayID displayID;
@property(strong) SCRunningApplication *owningApplication;
@end
@implementation EHTestShareableItem
@end

@interface EHTestContentFilter : NSObject
- (instancetype)initWithDisplay:(SCDisplay *)display excludingApplications:(NSArray *)applications
              exceptingWindows:(NSArray *)windows;
- (float)pointPixelScale;
@end
@implementation EHTestContentFilter
- (instancetype)initWithDisplay:(SCDisplay *)display excludingApplications:(NSArray *)applications
              exceptingWindows:(NSArray *)windows {
    self = [super init];
    if (self)
        excludedApplications = [applications copy];
    return self;
}
- (float)pointPixelScale { return 1; }
@end

namespace {
typedef void (^ScreenshotCompletion)(CGImageRef, NSError *);
__strong ScreenshotCompletion pendingScreenshot = nil;
__strong SCContentFilter *pendingFilter = nil;
QSize pendingScreenshotSize;

id allocateContentFilter(id, SEL) __attribute__((ns_returns_retained));
id allocateContentFilter(id, SEL) {
    return [EHTestContentFilter alloc];
}

void deferScreenshot(id, SEL, SCContentFilter *filter, SCStreamConfiguration *configuration,
                     ScreenshotCompletion completion) {
    pendingScreenshot = [completion copy];
    pendingFilter = filter;
    pendingScreenshotSize = QSize(int(configuration.width), int(configuration.height));
}

CGImageRef testImage(QSize size) {
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(nullptr, size.width(), size.height(), 8, size.width() * 4, space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!context)
        return nullptr;
    CGContextSetRGBFillColor(context, 0.2, 0.4, 0.8, 1);
    CGContextFillRect(context, CGRectMake(0, 0, size.width(), size.height()));
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    return image;
}

void deliverScreenshot() {
    ScreenshotCompletion completion = pendingScreenshot;
    pendingScreenshot = nil;
    CGImageRef image = testImage(pendingScreenshotSize);
    completion(image, nil);
    if (image)
        CGImageRelease(image);
}
}

namespace h2d {
class CocoaCaptureTests : public QObject {
    Q_OBJECT
    Method submit_ = nullptr;
    IMP originalSubmit_ = nullptr;
    Method allocateFilter_ = nullptr;
    IMP originalAllocateFilter_ = nullptr;

    static std::shared_ptr<MacScrollSession> fullDisplaySession() {
        auto session = std::make_shared<MacScrollSession>();
        session->display = CGMainDisplayID();
        session->displayBounds = CGDisplayBounds(session->display);
        session->backingMode = displayBackingMode(session->display);
        session->nativeGeometry = {0, 0, qRound(session->displayBounds.size.width),
                                       qRound(session->displayBounds.size.height)};
        scrollingSession = session;
        return session;
    }

    static EHTestShareableContent *contentFor(CGDirectDisplayID displayID) {
        EHTestShareableItem *display = [EHTestShareableItem new];
        display.displayID = displayID;
        EHTestShareableContent *content = [EHTestShareableContent new];
        content.displays = @[display];
        content.applications = @[];
        content.windows = @[];
        return content;
    }

    static std::weak_ptr<RegionRequest> queueFrame(ScrollRegionCallback callback) {
        auto session = std::make_shared<MacScrollSession>();
        session->display = CGMainDisplayID();
        session->displayBounds = CGDisplayBounds(session->display);
        session->backingMode = displayBackingMode(session->display);
        session->nativeGeometry = {0, 0, 2, 2};
        scrollingSession = session;
        auto request = std::make_shared<RegionRequest>();
        request->callback = std::move(callback);
        captureSessionRegion(session, {0, 0, 2, 2}, request);
        // Both locals disappear here, before the native completion runs. In the
        // old reference-parameter implementation the block owns neither one.
        return request;
    }

  private slots:
    void initTestCase() {
        if (!CGDisplayIsActive(CGMainDisplayID()))
            QSKIP("A display is needed for the backend's validity check; recording permission is not.");
        submit_ = class_getClassMethod([SCScreenshotManager class],
            @selector(captureImageWithFilter:configuration:completionHandler:));
        QVERIFY(submit_);
        originalSubmit_ = method_setImplementation(submit_, reinterpret_cast<IMP>(deferScreenshot));
        // Add a local +alloc override instead of mutating NSObject's inherited
        // method. Its replacement returns only our synthetic filter objects.
        Method allocate = class_getClassMethod([SCContentFilter class], @selector(alloc));
        QVERIFY(allocate);
        originalAllocateFilter_ = method_getImplementation(allocate);
        class_addMethod(object_getClass([SCContentFilter class]), @selector(alloc),
                        reinterpret_cast<IMP>(allocateContentFilter), method_getTypeEncoding(allocate));
        allocateFilter_ = class_getClassMethod([SCContentFilter class], @selector(alloc));
        method_setImplementation(allocateFilter_, reinterpret_cast<IMP>(allocateContentFilter));
    }
    void cleanupTestCase() {
        if (originalSubmit_)
            method_setImplementation(submit_, originalSubmit_);
        if (originalAllocateFilter_)
            method_setImplementation(allocateFilter_, originalAllocateFilter_);
    }
    void cleanup() {
        pendingScreenshot = nil;
        pendingFilter = nil;
        excludedApplications = nil;
        scrollingSession.reset();
        QCoreApplication::sendPostedEvents();
    }
    void firstFrameDoesNotRequireAnOwnApplicationEntry_data() {
        QTest::addColumn<bool>("ownApplication");
        QTest::addColumn<bool>("ownWindow");
        QTest::addColumn<bool>("otherApplication");
        QTest::newRow("all-controls-hidden") << false << false << false;
        QTest::newRow("only-other-applications") << false << false << true;
        QTest::newRow("application-exclusion") << true << false << true;
        QTest::newRow("recover-from-window-owner") << false << true << true;
    }
    void firstFrameDoesNotRequireAnOwnApplicationEntry() {
        QFETCH(bool, ownApplication);
        QFETCH(bool, ownWindow);
        QFETCH(bool, otherApplication);
        auto session = fullDisplaySession();
        EHTestShareableContent *content = contentFor(session->display);
        EHTestShareableItem *own = [EHTestShareableItem new];
        own.processID = NSProcessInfo.processInfo.processIdentifier;
        EHTestShareableItem *other = [EHTestShareableItem new];
        other.processID = own.processID + 1;
        NSMutableArray *applications = [NSMutableArray array];
        if (otherApplication) [applications addObject:other];
        if (ownApplication) [applications addObject:own];
        content.applications = applications;
        if (ownWindow) {
            EHTestShareableItem *window = [EHTestShareableItem new];
            window.owningApplication = (id)own;
            content.windows = @[window];
        }
        const QString error = prepareScrollFilter(session, (id)content, nil);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(session->filter != nil);
        const bool excludesOwn = ownApplication || ownWindow;
        QCOMPARE(session->excludesOwnApplication, excludesOwn);
        QCOMPARE(excludedApplications.count, NSUInteger(excludesOwn ? 1 : 0));
        if (excludesOwn)
            QVERIFY(excludedApplications.firstObject == own);
        int replies = 0;
        auto request = std::make_shared<RegionRequest>();
        request->callback = [&](QImage image, QString problem) {
            ++replies;
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
            QCOMPARE(image.size(), QSize(2, 2));
        };
        captureSessionRegion(session, {0, 0, 2, 2}, request);
        QVERIFY(pendingScreenshot != nil);
        QVERIFY(pendingFilter == session->filter);
        deliverScreenshot();
        QTRY_COMPARE(replies, 1);
        // Later controls must continue using Controller's hide-before-read
        // fallback when native application exclusion could not be installed.
        QCOMPARE(session->excludesOwnApplication, excludesOwn);
    }
    void invalidShareableContentStillFails_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("missing-display") << 0;
        QTest::newRow("changed-display-size") << 1;
        QTest::newRow("native-permission-error") << 2;
    }
    void invalidShareableContentStillFails() {
        QFETCH(int, failure);
        auto session = fullDisplaySession();
        EHTestShareableContent *content = contentFor(session->display);
        NSError *nativeError = nil;
        if (failure == 0) content.displays = @[];
        if (failure == 1) session->nativeGeometry.adjust(0, 0, 1, 0);
        if (failure == 2)
            nativeError = [NSError errorWithDomain:@"test-permission" code:1 userInfo:nil];
        QVERIFY(!prepareScrollFilter(session, (id)content, nativeError).isEmpty());
        QVERIFY(session->filter == nil);
        QVERIFY(!session->excludesOwnApplication);
        QVERIFY(pendingScreenshot == nil);
    }
    void delayedCompletionRetainsOwnersPastCallerScope() {
        int replies = 0;
        QImage received;
        QString error;
        auto request = queueFrame([&](QImage image, QString problem) {
            ++replies;
            received = std::move(image);
            error = std::move(problem);
        });
        QVERIFY(pendingScreenshot != nil);
        QVERIFY2(!request.expired(), "The escaping native block must own the request after queueFrame returns.");
        deliverScreenshot();
        QTRY_COMPARE(replies, 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(received.size(), QSize(2, 2));
        QTRY_VERIFY(request.expired());
    }
    void cancelledSessionReturnsAnErrorWithoutReplacingItsSuccessor() {
        int replies = 0;
        QString error;
        auto request = queueFrame([&](QImage image, QString problem) {
            ++replies;
            QVERIFY(image.isNull());
            error = std::move(problem);
        });
        QVERIFY(!request.expired());
        auto successor = std::make_shared<MacScrollSession>();
        scrollingSession = successor;
        deliverScreenshot();
        QTRY_COMPARE(replies, 1);
        QVERIFY(!error.isEmpty());
        QVERIFY(scrollingSession == successor);
    }
    void frameAfterTimeoutDoesNotCompleteTheRequestTwice() {
        int replies = 0;
        auto request = queueFrame([&](QImage, QString) { ++replies; });
        QVERIFY(!request.expired());
        request.lock()->finish({}, QStringLiteral("simulated timeout"));
        QCOMPARE(replies, 1);
        deliverScreenshot();
        QCoreApplication::sendPostedEvents();
        QCOMPARE(replies, 1);
        QTRY_VERIFY(request.expired());
    }
};
}

QTEST_MAIN(h2d::CocoaCaptureTests)
#include "cocoa_capture_test.moc"
