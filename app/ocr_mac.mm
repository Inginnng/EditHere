#include "ocr.h"
#include <QBuffer>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Vision/Vision.h>

namespace h2d {
namespace {
// Vision reads a CGImage, so the picture is handed over through the same PNG
// encoding a file based path would use. Doing it in memory keeps temporary files,
// and with them the extra permission prompts, out of the macOS build.
CGImageRef createCGImage(const QImage &image) {
    QByteArray encoded;
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return nullptr;
    buffer.close();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault,
                                  reinterpret_cast<const UInt8 *>(encoded.constData()), encoded.size());
    if (data == nullptr)
        return nullptr;
    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (source == nullptr)
        return nullptr;
    CGImageRef picture = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    return picture;
}
// Vision names Chinese without a region ("zh-Hans"), while the Windows recogniser
// wants the full tag, so the shared tag is narrowed to what this platform knows.
NSString *visionLanguage(const QString &preferred) {
    if (preferred.isEmpty())
        return nil;
    if (preferred.startsWith(QStringLiteral("zh")))
        return @"zh-Hans";
    return preferred.toNSString();
}
} // namespace

QStringList visionLanguageTags() {
    QStringList tags;
    @autoreleasepool {
        VNRecognizeTextRequest *probe = [[VNRecognizeTextRequest alloc] init];
        probe.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        NSError *error = nil;
        NSArray<NSString *> *languages = [probe supportedRecognitionLanguagesAndReturnError:&error];
        if (languages == nil)
            return tags;
        for (NSString *tag in languages)
            tags.append(QString::fromNSString(tag));
    }
    return tags;
}

VisionOutcome visionRecognize(const QImage &image, const QString &preferred) {
    VisionOutcome outcome;
    if (image.isNull()) {
        outcome.diagnostic = QStringLiteral("no image");
        return outcome;
    }
    @autoreleasepool {
        CGImageRef picture = createCGImage(image);
        if (picture == nullptr) {
            outcome.diagnostic = QStringLiteral("the picture could not be encoded");
            return outcome;
        }
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = YES;
        NSString *language = visionLanguage(preferred);
        if (language == nil) {
            // Reading Chinese and Latin together keeps a mixed screenshot usable
            // without the user having to say which one it is.
            request.recognitionLanguages = @[ @"zh-Hans", @"en-US" ];
        } else {
            NSError *supportedError = nil;
            NSArray<NSString *> *supported = [request supportedRecognitionLanguagesAndReturnError:&supportedError];
            if (supported != nil && ![supported containsObject:language]) {
                // Vision cannot fall back silently here: the caller asked for a
                // language so a wrong answer would be worse than no answer.
                outcome.noEngine = true;
                outcome.diagnostic = QStringLiteral("the recogniser does not offer the requested language");
                CGImageRelease(picture);
                return outcome;
            }
            request.recognitionLanguages = @[ language ];
        }
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:picture options:@{}];
        NSError *error = nil;
        const BOOL performed = [handler performRequests:@[ request ] error:&error];
        CGImageRelease(picture);
        if (!performed) {
            outcome.diagnostic = error == nil ? QStringLiteral("performRequests failed")
                                              : QString::fromNSString(error.localizedDescription);
            return outcome;
        }
        // The observations arrive in reading order. Vision puts the origin of a box
        // in the bottom left corner, so y is flipped to the top-left origin the rest
        // of the application uses; the values are already fractions of the picture.
        for (VNRecognizedTextObservation *observation in request.results) {
            VNRecognizedText *candidate = [observation topCandidates:1].firstObject;
            if (candidate == nil || candidate.string.length == 0)
                continue;
            const CGRect box = observation.boundingBox;
            outcome.lines.append({QString::fromNSString(candidate.string),
                                  QRectF(box.origin.x, 1.0 - box.origin.y - box.size.height,
                                         box.size.width, box.size.height)});
        }
        // Vision answers with the language it was asked for, so the first entry is
        // what the result window names.
        NSString *used = request.recognitionLanguages.firstObject;
        outcome.language = used == nil ? QString() : QString::fromNSString(used);
        outcome.ok = true;
    }
    return outcome;
}
} // namespace h2d
