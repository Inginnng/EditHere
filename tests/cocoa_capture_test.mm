// Compile the production backend in this test target, without h2d_ui, so the
// asynchronous callback can be exercised before it crosses ScreenCaptureKit.
// Only the native submission method is replaced; no recording permission or
// screenshot of the CI desktop is needed.
#include "../app/platform_mac.mm"
#include <QTest>
#import <objc/runtime.h>

namespace {
typedef void (^ScreenshotCompletion)(CGImageRef, NSError *);
__strong ScreenshotCompletion pendingScreenshot = nil;

void deferScreenshot(id, SEL, SCContentFilter *, SCStreamConfiguration *, ScreenshotCompletion completion) {
    pendingScreenshot = [completion copy];
}

CGImageRef testImage() {
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(nullptr, 2, 2, 8, 8, space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!context)
        return nullptr;
    CGContextSetRGBFillColor(context, 0.2, 0.4, 0.8, 1);
    CGContextFillRect(context, CGRectMake(0, 0, 2, 2));
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    return image;
}

void deliverScreenshot() {
    ScreenshotCompletion completion = pendingScreenshot;
    pendingScreenshot = nil;
    CGImageRef image = testImage();
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
    }
    void cleanupTestCase() {
        if (originalSubmit_)
            method_setImplementation(submit_, originalSubmit_);
    }
    void cleanup() {
        pendingScreenshot = nil;
        scrollingSession.reset();
        QCoreApplication::sendPostedEvents();
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
