#include "scrollcapture.h"
#include "scrollstitch.h"
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QPainter>
#include <QTest>
#include <QTransform>
#include <QImageReader>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtEndian>
#include <cstring>
using namespace h2d;

class ScrollStitchTests : public QObject {
    Q_OBJECT
    static QImage page(int width = 640, int height = 2600) {
        QImage result(width, height, QImage::Format_ARGB32);
        result.fill(Qt::white);
        QPainter painter(&result);
        painter.setFont(QFont(QStringLiteral("Arial"), 12));
        for (int index = 0, y = 12; y < height - 40; ++index, y += 45 + index % 9) {
            painter.setPen(QColor("#222222"));
            painter.drawText(28, y + 18, QStringLiteral("%1. Research notes: item %2 / measurements %3")
                .arg(index).arg(index * 137).arg(index * 17 + 23));
            painter.fillRect(width - 120, y + 7, 24 + index % 61, 13, QColor::fromHsv(index * 71 % 360, 130, 180));
            painter.setPen(QColor("#e6e6e6"));
            painter.drawLine(24, y + 32, width - 25, y + 32);
        }
        return result;
    }
    static QImage frame(const QImage &source, int offset, int height = 480, int top = 0, int bottom = 0) {
        QImage image(source.width(), height, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.drawImage(0, top, source.copy(0, offset, source.width(), height - top - bottom));
        if (top) {
            painter.fillRect(0, 0, image.width(), top, QColor("#2f5b98"));
            painter.setPen(Qt::white);
            painter.drawText(20, top / 2 + 5, QStringLiteral("Fixed navigation"));
        }
        if (bottom) {
            painter.fillRect(0, height - bottom, image.width(), bottom, QColor("#dce2e8"));
            painter.setPen(Qt::black);
            painter.drawText(20, height - bottom / 2 + 5, QStringLiteral("Fixed toolbar"));
        }
        return image;
    }
    static QImage numberedRows() {
        QImage source(720, 1800, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QPainter painter(&source);
        QFont font(QStringLiteral("Arial"));
        for (int index = 0; index < 30; ++index) {
            const int y = index * 60;
            font.setPixelSize(24);
            painter.setFont(font);
            painter.setPen(QColor("#222222"));
            painter.drawText(100, y + 36, QStringLiteral("Document row: scrolling content"));
            font.setPixelSize(9);
            painter.setFont(font);
            painter.drawText(44, y + 32, QString::number(index));
            painter.fillRect(18, y + 25, 3, 4,
                QColor(index * 61 % 255, index * 7 % 255, index * 37 % 255));
            painter.setPen(QColor("#eceff3"));
            painter.drawLine(0, y + 59, 719, y + 59);
        }
        return source;
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        QVERIFY(QFontDatabase::addApplicationFont(fonts + "arial.ttf") >= 0);
#endif
    }
    void sparseTextStitchesPixelExactly() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0));
        for (int offset : {137, 321, 487, 660})
            QCOMPARE(stitcher.add(frame(source, offset)), offset - (offset == 137 ? 0 :
                offset == 321 ? 137 : offset == 487 ? 321 : 487));
        QCOMPARE(stitcher.picture(), source.copy(0, 0, source.width(), 1140));
        QCOMPARE(stitcher.add(frame(source, 660)), 0);
        QCOMPARE(stitcher.height(), 1140);
    }
    void startsInMiddleAndTracksRevisitsInBothDirections() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 620));
        QCOMPARE(stitcher.viewportRect(), QRect(0, 0, 640, 480));
        QCOMPARE(stitcher.add(frame(source, 470)), 150);
        QCOMPARE(stitcher.add(frame(source, 280)), 190);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 0, 640, 480));
        const QImage upward = stitcher.picture();
        QCOMPARE(upward, source.copy(0, 280, 640, 820));
        QCOMPARE(stitcher.add(frame(source, 470)), 0);
        QCOMPARE(stitcher.status(), ScrollStitcher::Status::Tracked);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 190, 640, 480));
        QCOMPARE(stitcher.picture(), upward);
        QCOMPARE(stitcher.add(frame(source, 620)), 0);
        QCOMPARE(stitcher.add(frame(source, 810)), 190);
        QCOMPARE(stitcher.add(frame(source, 980)), 170);
        for (int offset : {740, 520, 280})
            QCOMPARE(stitcher.add(frame(source, offset)), 0);
        QCOMPARE(stitcher.add(frame(source, 100)), 180);
        QCOMPARE(stitcher.add(frame(source, 0)), 100);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 0, 640, 480));
        QCOMPARE(stitcher.picture(), source.copy(0, 0, 640, 1460));
        QVERIFY(stitcher.matched());
    }
    void rejectedReverseFrameKeepsPositionAndRecovers() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 500));
        QCOMPARE(stitcher.add(frame(source, 650)), 150);
        const QImage accepted = stitcher.picture();
        const QRect position = stitcher.viewportRect();
        QImage unrelated(640, 480, QImage::Format_ARGB32);
        unrelated.fill(Qt::black);
        QCOMPARE(stitcher.add(unrelated), -1);
        QVERIFY(!stitcher.matched());
        QCOMPARE(stitcher.viewportRect(), position);
        QCOMPARE(stitcher.picture(), accepted);
        QCOMPARE(stitcher.add(frame(source, 500)), 0);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 0, 640, 480));
        QCOMPARE(stitcher.add(frame(source, 350)), 150);
        QCOMPARE(stitcher.picture(), source.copy(0, 350, 640, 780));
        QVERIFY(stitcher.matched());
    }
    void distantRevisitRecoversAcrossRetainedSlicesAndCanGrowAgain() {
        const QImage source = page(640, 3500);
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0));
        for (int offset = 180; offset <= 1260; offset += 180)
            QCOMPARE(stitcher.add(frame(source, offset)), 180);
        const QImage accepted = source.copy(0, 0, 640, 1740);
        QCOMPARE(stitcher.picture(), accepted);
        // There is no overlap with the last viewport. This frame spans several
        // earlier slices and must locate itself in their native content.
        QCOMPARE(stitcher.add(frame(source, 120)), 0);
        QCOMPARE(stitcher.status(), ScrollStitcher::Status::Tracked);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 120, 640, 480));
        QCOMPARE(stitcher.picture(), accepted);
        QCOMPARE(stitcher.previewRegion(QRect(40, 250, 200, 320), 200),
                 source.copy(40, 250, 200, 320));
        QCOMPARE(stitcher.add(frame(source, 1600)), 340);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 1600, 640, 480));
        QCOMPARE(stitcher.picture(), source.copy(0, 0, 640, 2080));
        QImage unrelated(640, 480, QImage::Format_ARGB32);
        unrelated.fill(Qt::black);
        QCOMPARE(stitcher.add(unrelated), -1);
        QCOMPARE(stitcher.add(frame(source, 600)), 0);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 600, 640, 480));
        QCOMPARE(stitcher.picture(), source.copy(0, 0, 640, 2080));
    }
    void distantRevisitPreservesFixedBarsAndRejectsRemovedContent() {
        const QImage source = page(640, 3500);
        ScrollStitcher stitcher;
        const QImage first = frame(source, 600, 480, 51, 37);
        stitcher.reset(first);
        for (int offset = 780; offset <= 1500; offset += 180)
            QCOMPARE(stitcher.add(frame(source, offset, 480, 51, 37)), 180);
        QCOMPARE(stitcher.add(frame(source, 650, 480, 51, 37)), 0);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 101, 640, 392));
        QCOMPARE(stitcher.add(frame(source, 1700, 480, 51, 37)), 200);
        const QImage output = stitcher.picture();
        QCOMPARE(output.copy(0, 0, 640, 51), first.copy(0, 0, 640, 51));
        QCOMPARE(output.copy(0, 51, 640, 1492), source.copy(0, 600, 640, 1492));
        QCOMPARE(output.copy(0, output.height() - 37, 640, 37), first.copy(0, 443, 640, 37));
        QVERIFY(stitcher.trimBeforeViewport());
        const QImage cropped = stitcher.picture();
        QCOMPARE(stitcher.add(frame(source, 650, 480, 51, 37)), -1);
        QCOMPARE(stitcher.picture(), cropped);
    }
    void distantRevisitToleratesSmallWholeFrameColourChanges() {
        const QImage source = page(640, 3500);
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0));
        for (int offset = 180; offset <= 1260; offset += 180)
            QCOMPARE(stitcher.add(frame(source, offset)), 180);
        const QImage accepted = stitcher.picture();
        for (const auto sample : {std::pair<int, int>{120, 1}, {1000, 2}, {260, 2}}) {
            QImage changed = frame(source, sample.first);
            for (int y = 0; y < changed.height(); ++y) {
                auto *scan = reinterpret_cast<QRgb *>(changed.scanLine(y));
                for (int x = 0; x < changed.width(); ++x)
                    scan[x] = qRgb(std::min(255, qRed(scan[x]) + sample.second),
                                   std::min(255, qGreen(scan[x]) + sample.second),
                                   std::min(255, qBlue(scan[x]) + sample.second));
            }
            QCOMPARE(stitcher.add(changed), 0);
            QCOMPARE(stitcher.viewportRect(), QRect(0, sample.first, 640, 480));
            QCOMPARE(stitcher.picture(), accepted);
            QVERIFY(stitcher.matched());
        }
    }
    void distantRevisitIgnoresFixedSidebarInLocationIndex() {
        const QImage source = page(960, 3500);
        const auto captured = [&](int offset, const QColor &color) {
            QImage image = frame(source, offset);
            QPainter painter(&image);
            painter.fillRect(0, 0, 180, 480, color);
            painter.setPen(Qt::white);
            painter.drawText(20, 80, QStringLiteral("Fixed sidebar"));
            painter.drawText(20, 170, QStringLiteral("Current account"));
            return image;
        };
        ScrollStitcher stitcher;
        stitcher.reset(captured(0, QColor("#5c677a")));
        for (int offset = 180; offset <= 1080; offset += 180)
            QCOMPARE(stitcher.add(captured(offset, QColor("#5c677a"))), 180);
        QCOMPARE(stitcher.add(captured(1260, QColor("#3869a8"))), 180);
        const QImage accepted = stitcher.picture();
        // The sidebar has a new colour but is stationary between the latest
        // frame and this revisit. Whole-row RGB hashes cannot locate old rows.
        QCOMPARE(stitcher.add(captured(120, QColor("#3869a8"))), 0);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 120, 960, 480));
        QCOMPARE(stitcher.picture(), accepted);
        QCOMPARE(stitcher.picture().copy(180, 0, 780, 1740), source.copy(180, 0, 780, 1740));
        QVERIFY(stitcher.matched());
    }
    void narrowTextOnWideWhitePageIsMovement() {
        QImage source(1920, 1500, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QPainter painter(&source);
        painter.setFont(QFont(QStringLiteral("Arial"), 12));
        painter.setPen(Qt::black);
        painter.drawText(28, 340, QStringLiteral("First"));
        painter.drawText(28, 800, QStringLiteral("Second"));
        painter.drawText(28, 1260, QStringLiteral("Third"));
        painter.end();
        const QImage first = frame(source, 0, 600);
        const QImage shortScroll = frame(source, 20, 600);
        QVERIFY(!equivalentScrollFrames(first, shortScroll));
        ScrollStitcher stitcher;
        stitcher.reset(first);
        int previous = 0;
        for (int offset : {20, 100, 280, 420, 590, 740}) {
            const QImage incoming = frame(source, offset, 600);
            QVERIFY(!equivalentScrollFrames(stitcher.lastFrame(), incoming));
            const auto match = matchVerticalOverlap(stitcher.lastFrame(), incoming, 120, 600, 8.0);
            QVERIFY2(match.found, qPrintable(QStringLiteral("offset %1, overlap %2, score %3, ambiguous %4")
                .arg(offset).arg(match.overlap).arg(match.score).arg(match.ambiguous)));
            QCOMPARE(stitcher.add(incoming), offset - previous);
            previous = offset;
        }
        QCOMPARE(stitcher.picture(), source.copy(0, 0, source.width(), 1340));
    }
    void paragraphSurroundedByWhiteSpaceStitches() {
        QImage source(640, 1500, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QPainter painter(&source);
        painter.setFont(QFont(QStringLiteral("Arial"), 12));
        painter.setPen(Qt::black);
        for (int section = 0; section < 3; ++section) {
            const int y = 340 + section * 460;
            painter.drawText(160, y, QStringLiteral("Section %1: short paragraph").arg(section));
            painter.drawText(160, y + 22, QStringLiteral("Value = %1").arg(section * 137 + 19));
        }
        painter.end();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0, 600));
        int previous = 0;
        for (int offset : {137, 280, 420, 590, 740}) {
            QCOMPARE(stitcher.add(frame(source, offset, 600)), offset - previous);
            previous = offset;
        }
        QCOMPARE(stitcher.picture(), source.copy(0, 0, source.width(), 1340));
    }
    void fixedBarsAppearOnce() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0, 480, 51, 37));
        QCOMPARE(stitcher.add(frame(source, 143, 480, 51, 37)), 143);
        QVERIFY(stitcher.bands().top >= 51);
        QCOMPARE(stitcher.bands().bottom, 37);
        QCOMPARE(stitcher.add(frame(source, 316, 480, 51, 37)), 173);
        QImage expected(640, 796, QImage::Format_ARGB32);
        QPainter painter(&expected);
        painter.drawImage(0, 0, frame(source, 0, 480, 51, 37).copy(0, 0, 640, 51));
        painter.drawImage(0, 51, source.copy(0, 0, 640, 708));
        painter.drawImage(0, 759, frame(source, 316, 480, 51, 37).copy(0, 443, 640, 37));
        painter.end();
        QCOMPARE(stitcher.picture(), expected);
    }
    void fixedBarsStayAtEdgesWhilePrependingAndChangingDirection() {
        const QImage source = page();
        ScrollStitcher stitcher;
        const QImage first = frame(source, 400, 480, 51, 37);
        stitcher.reset(first);
        QCOMPARE(stitcher.add(frame(source, 250, 480, 51, 37)), 150);
        QCOMPARE(stitcher.add(frame(source, 100, 480, 51, 37)), 150);
        QCOMPARE(stitcher.add(frame(source, 300, 480, 51, 37)), 0);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 251, 640, 392));
        QCOMPARE(stitcher.add(frame(source, 500, 480, 51, 37)), 100);
        QCOMPARE(stitcher.add(frame(source, 700, 480, 51, 37)), 200);
        QImage expected(640, 1080, QImage::Format_ARGB32);
        QPainter painter(&expected);
        painter.drawImage(0, 0, first.copy(0, 0, 640, 51));
        painter.drawImage(0, 51, source.copy(0, 100, 640, 992));
        painter.drawImage(0, 1043, first.copy(0, 443, 640, 37));
        painter.end();
        QCOMPARE(stitcher.picture(), expected);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 651, 640, 392));
        const QImage preview = stitcher.preview(160);
        QVERIFY(qAbs(preview.height() - expected.height() / 4) <= 4);
    }
    void localAnimationDoesNotStopOrMisalign() {
        const QImage source = page();
        const QImage first = frame(source, 0);
        QImage next = frame(source, 157);
        QPainter painter(&next);
        painter.fillRect(490, 180, 45, 24, Qt::red);
        painter.end();
        ScrollStitcher stitcher;
        stitcher.reset(first);
        QCOMPARE(stitcher.add(next), 157);
        QImage idle = next;
        QPainter idlePainter(&idle);
        idlePainter.fillRect(490, 180, 45, 24, Qt::blue);
        idlePainter.end();
        QVERIFY(equivalentScrollFrames(next, idle));
        QCOMPARE(stitcher.add(idle), 0);
        QCOMPARE(stitcher.height(), 637);
    }
    void fixedSidebarAndScrollbarDoNotDriveTheMatch() {
        const QImage source = page(960);
        QImage first = frame(source, 0);
        QImage next = frame(source, 177);
        for (QImage *image : {&first, &next}) {
            QPainter painter(image);
            painter.fillRect(0, 0, 130, 480, QColor("#e8ecf3"));
            painter.drawText(20, 80, QStringLiteral("Fixed sidebar"));
            painter.fillRect(948, 0, 12, 480, QColor("#d4d4d4"));
            painter.fillRect(950, image == &first ? 40 : 72, 8, 55, Qt::darkGray);
        }
        ScrollStitcher stitcher;
        stitcher.reset(first);
        QCOMPARE(stitcher.add(next), 177);
        QCOMPARE(stitcher.height(), 657);
    }
    void repeatPatternsAndBlankContentAreRejected() {
        QImage repeated(640, 1500, QImage::Format_ARGB32);
        repeated.fill(Qt::white);
        QPainter painter(&repeated);
        for (int y = 0; y < 1500; y += 40) {
            painter.fillRect(40, y + 5, 180, 10, Qt::black);
            painter.fillRect(430, y + 7, 70, 8, Qt::blue);
        }
        painter.end();
        const auto match = matchVerticalOverlap(frame(repeated, 0), frame(repeated, 137), 96, 479, 8.0);
        QVERIFY(!match.found);
        QVERIFY(match.ambiguous);
        ScrollStitcher stitcher;
        stitcher.reset(frame(repeated, 0));
        QCOMPARE(stitcher.add(frame(repeated, 137)), -1);
        QCOMPARE(stitcher.height(), 480);
        QImage blank(640, 480, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        QVERIFY(!matchVerticalOverlap(blank, blank, 96, 479).found);
    }
    void repeatedTextLinesRequireDistinctiveEvidence() {
        QImage source(640, 1500, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QPainter painter(&source);
        painter.setFont(QFont(QStringLiteral("Arial"), 12));
        painter.setPen(Qt::black);
        for (int y = 36; y < source.height(); y += 44)
            painter.drawText(140, y, QStringLiteral("The same repeated text line."));
        painter.end();
        ScrollStitcher stitcher;
        const QImage first = frame(source, 0);
        stitcher.reset(first);
        QCOMPARE(stitcher.add(frame(source, 137)), -1);
        QCOMPARE(stitcher.status(), ScrollStitcher::Status::Ambiguous);
        QCOMPARE(stitcher.picture(), first);
    }
    void repeatedRowsWithSmallUniqueDetailsStitch() {
        QVector<QImage> frames;
        const QString directory = qEnvironmentVariable("H2D_SCROLL_BROWSER_FRAMES");
        if (directory.isEmpty()) {
            QImage source(720, 1800, QImage::Format_ARGB32);
            source.fill(Qt::white);
            QPainter painter(&source);
            QFont font(QStringLiteral("Arial"));
            for (int index = 0; index < 30; ++index) {
                const int y = index * 60;
                font.setPixelSize(24);
                painter.setFont(font);
                painter.setPen(QColor("#222222"));
                painter.drawText(100, y + 36, QStringLiteral("Document row: scrolling content"));
                // Only a small number and icon distinguish otherwise identical
                // rows. Their evidence must not disappear in an averaged score.
                font.setPixelSize(9);
                painter.setFont(font);
                painter.drawText(44, y + 32, QString::number(index));
                painter.fillRect(18, y + 25, 3, 4,
                    QColor(index * 61 % 255, index * 7 % 255, index * 37 % 255));
                painter.setPen(QColor("#eceff3"));
                painter.drawLine(0, y + 59, 719, y + 59);
            }
            painter.end();
            for (int offset = 150; offset <= 750; offset += 150)
                frames.append(frame(source, offset, 660, 60, 60));
        } else {
            // A native browser run may supply its real Chromium raster frames.
            for (int index = 0; index < 10; index += 2)
                frames.append(QImage(directory + QStringLiteral("/browser-frame-%1.png").arg(index)));
        }
        for (const QImage &image : frames)
            QCOMPARE(image.size(), QSize(720, 660));
        QImage previous = frames.front();
        ScrollStitcher stitcher;
        stitcher.reset(previous);
        QImage expected(720, 1260, QImage::Format_ARGB32);
        QPainter painter(&expected);
        painter.drawImage(0, 0, previous.copy(0, 0, 720, 600));
        for (int index = 1; index < frames.size(); ++index) {
            const QImage next = frames[index];
            const auto bands = fixedBands(previous, next);
            QCOMPARE(bands.top, 60);
            QCOMPARE(bands.bottom, 60);
            const int body = previous.height() - bands.top - bands.bottom;
            const auto match = matchVerticalOverlap(previous.copy(0, bands.top, previous.width(), body),
                next.copy(0, bands.top, next.width(), body), std::max(32, body / 5), body, 8.0);
            qInfo() << index << "bands" << bands.top << bands.bottom << "match" << match.found
                << match.overlap << match.score << "ambiguous" << match.ambiguous;
            QCOMPARE(stitcher.add(next), 150);
            painter.drawImage(0, 600 + (index - 1) * 150, next.copy(0, 450, 720, 150));
            previous = next;
        }
        painter.drawImage(0, 1200, frames.back().copy(0, 600, 720, 60));
        painter.end();
        QCOMPARE(stitcher.picture(), expected);
    }
    void repeatedRowsWithSmallDetailsAndLocalAnimationStillAlign() {
        const QImage source = numberedRows();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0, 660, 60, 60));
        QImage animated = frame(source, 150, 660, 60, 60);
        QPainter painter(&animated);
        painter.fillRect(560, 220, 10, 14, Qt::red);
        painter.end();
        // The sparse grid sees several almost identical list displacements.
        // Native row numbers disambiguate even though this patch is not exact.
        QCOMPARE(stitcher.add(animated), 150);
        QCOMPARE(stitcher.add(frame(source, 300, 660, 60, 60)), 150);
        QCOMPARE(stitcher.picture().copy(0, 60, 720, 840), source.copy(0, 0, 720, 840));
        QCOMPARE(stitcher.viewportRect(), QRect(0, 360, 720, 540));
    }
    void unrelatedOrGeometryChangedFramesLeaveResultIntact() {
        const QImage source = page();
        ScrollStitcher stitcher;
        const QImage first = frame(source, 0);
        stitcher.reset(first);
        QImage unrelated(640, 480, QImage::Format_ARGB32);
        unrelated.fill(Qt::black);
        QCOMPARE(stitcher.add(unrelated), -1);
        QCOMPARE(stitcher.picture(), first);
        QCOMPARE(stitcher.add(QImage(639, 480, QImage::Format_RGB32)), -1);
        QCOMPARE(stitcher.status(), ScrollStitcher::Status::GeometryChanged);
        QCOMPARE(stitcher.add(frame(source, 137)), 137);
    }
    void captureRequiresProgressAndKeepsAcceptedFrameCount() {
        const QImage source = page();
        ScrollCapture capture;
        capture.begin(frame(source, 0));
        QVERIFY(capture.running());
        QVERIFY(!capture.hasProgress());
        QCOMPARE(capture.take(frame(source, 0)), ScrollCapture::Outcome::Repeat);
        QVERIFY(!capture.hasProgress());
        QCOMPARE(capture.take(frame(source, 137)), ScrollCapture::Outcome::Added);
        QVERIFY(capture.hasProgress());
        QCOMPARE(capture.frames(), 1);
        capture.stop();
        QCOMPARE(capture.frames(), 1);
        QVERIFY(!capture.running());
    }
    void horizontalCaptureSupportsPrependRevisitAndPreview() {
        const QImage source = page(320, 1600).transformed(QTransform(0, 1, 1, 0, 0, 0));
        ScrollCapture capture;
        QVERIFY(capture.begin(source.copy(500, 0, 480, 320), 1, Qt::Horizontal));
        QCOMPARE(capture.axis(), Qt::Horizontal);
        QCOMPARE(capture.take(source.copy(350, 0, 480, 320)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.size(), QSize(630, 320));
        QCOMPARE(capture.viewportRect(), QRect(0, 0, 480, 320));
        QCOMPARE(capture.take(source.copy(500, 0, 480, 320)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.frames(), 1);
        QCOMPARE(capture.viewportRect(), QRect(150, 0, 480, 320));
        QCOMPARE(capture.take(source.copy(680, 0, 480, 320)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.picture(), source.copy(350, 0, 810, 320));
        const QImage preview = capture.preview(160);
        QVERIFY(preview.width() <= 160);
        QVERIFY(qAbs(preview.height() - qRound(320.0 * preview.width() / 810)) <= 3);
        QVERIFY(capture.matched());
    }
    void horizontalDistantRevisitAndRegionPreviewUseNativeCoordinates() {
        const QImage source = page(320, 3000).transformed(QTransform(0, 1, 1, 0, 0, 0));
        ScrollCapture capture;
        QVERIFY(capture.begin(source.copy(0, 0, 480, 320), 1, Qt::Horizontal));
        for (int offset = 180; offset <= 1080; offset += 180)
            QCOMPARE(capture.take(source.copy(offset, 0, 480, 320)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(source.copy(120, 0, 480, 320)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.viewportRect(), QRect(120, 0, 480, 320));
        QCOMPARE(capture.previewRegion(QRect(250, 40, 320, 200), 320),
                 source.copy(250, 40, 320, 200));
        QCOMPARE(capture.take(source.copy(1400, 0, 480, 320)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.picture(), source.copy(0, 0, 1880, 320));
    }
    void movingSamplesAppendOnlyExactContinuations() {
        // Manual scrolling is sampled while the page moves. An exact overlap is
        // appended at once; a blurred or unrelated sample changes nothing.
        const QImage source = page();
        ScrollCapture capture;
        capture.begin(frame(source, 0));
        QVERIFY(capture.takeMoving(frame(source, 97)));
        QCOMPARE(capture.frames(), 1);
        QCOMPARE(capture.size(), QSize(640, 577));
        QImage blurred = frame(source, 190);
        blurred = blurred.scaled(640, 470).scaled(640, 480, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QVERIFY(!capture.takeMoving(blurred));
        QImage unrelated(640, 480, QImage::Format_ARGB32);
        unrelated.fill(Qt::black);
        QVERIFY(!capture.takeMoving(unrelated));
        QVERIFY(!capture.takeMoving(frame(source, 97)));
        QCOMPARE(capture.frames(), 1);
        QCOMPARE(capture.take(frame(source, 300)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.picture(), source.copy(0, 0, 640, 780));
    }
    void planDoesNotChangeStitchUntilCommitted() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0));
        const auto first = stitcher.plan(frame(source, 120));
        const auto second = stitcher.plan(frame(source, 240));
        QCOMPARE(first.added, 120);
        QCOMPARE(stitcher.height(), 480);
        QCOMPARE(stitcher.commit(first), 120);
        // A step planned against the older viewport is matched again on commit.
        QCOMPARE(stitcher.commit(second), 120);
        QCOMPARE(stitcher.picture(), source.copy(0, 0, 640, 720));
    }
    void stalePrependPlanIsMatchedAgainAndUpdatesViewport() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 500));
        const auto up = stitcher.plan(frame(source, 380));
        const auto down = stitcher.plan(frame(source, 620));
        QCOMPARE(up.displacement, -120);
        QCOMPARE(up.prepend, 120);
        QCOMPARE(stitcher.commit(up), 120);
        QCOMPARE(stitcher.commit(down), 120);
        QCOMPARE(stitcher.viewportRect(), QRect(0, 240, 640, 480));
        QCOMPARE(stitcher.picture(), source.copy(0, 380, 640, 720));
    }
    void livePreviewKeepsWholeResultAtPanelWidth() {
        const QImage source = page();
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0, 480, 40, 30));
        QVERIFY(stitcher.add(frame(source, 150, 480, 40, 30)) > 0);
        QVERIFY(stitcher.add(frame(source, 300, 480, 40, 30)) > 0);
        const QImage full = stitcher.picture();
        const QImage preview = stitcher.preview(160);
        QCOMPARE(preview.width(), 160);
        QVERIFY(qAbs(preview.height() - full.height() / 4) <= 4);
        // The fixed footer remains at the end of the preview, as in the result.
        QCOMPARE(QColor(preview.pixel(2, preview.height() - 2)).name(), QColor(full.pixel(8, full.height() - 4)).name());
        QCOMPARE(stitcher.preview(160).size(), preview.size());
    }
    void livePreviewDoesNotAccumulateRoundingOnSmallScrolls() {
        QImage source(128, 1000, QImage::Format_ARGB32);
        quint32 random = 19;
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x) {
                random = random * 1664525u + 1013904223u;
                source.setPixel(x, y, qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255));
            }
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 100, 240));
        for (int offset = 107; offset <= 198; offset += 7) {
            QCOMPARE(stitcher.add(frame(source, offset, 240)), 7);
            QCOMPARE(stitcher.preview(8).height(), qRound(stitcher.height() / 16.0));
        }
        QCOMPARE(stitcher.add(frame(source, 90, 240)), 10);
        QCOMPARE(stitcher.preview(8).height(), qRound(stitcher.height() / 16.0));
    }
    void trimmingUsesTheCurrentViewportAndAcceptsMoreScrolling() {
        const QImage source = page();
        ScrollCapture capture;
        QVERIFY(capture.begin(frame(source, 300)));
        QCOMPARE(capture.take(frame(source, 450)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(frame(source, 600)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(frame(source, 450)), ScrollCapture::Outcome::Repeat);
        QVERIFY(capture.cropBeforeViewport());
        QCOMPARE(capture.picture(), source.copy(0, 450, 640, 630));
        QVERIFY(capture.cropAfterViewport());
        QCOMPARE(capture.picture(), source.copy(0, 450, 640, 480));
        QCOMPARE(capture.take(frame(source, 750)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.picture(), source.copy(0, 450, 640, 780));
    }
    void autoCropActuallyRemovesReversedPixelsAndCanReverseDirection() {
        const QImage source = page();
        ScrollCapture capture;
        capture.setAutoCrop(true);
        QVERIFY(capture.begin(frame(source, 300)));
        QCOMPARE(capture.take(frame(source, 450)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(frame(source, 600)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(frame(source, 450)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.picture(), source.copy(0, 300, 640, 630));
        QCOMPARE(capture.take(frame(source, 300)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.picture(), source.copy(0, 300, 640, 480));
        QCOMPARE(capture.take(frame(source, 150)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.picture(), source.copy(0, 150, 640, 630));
    }
    void pixelAndDimensionLimitsAreCheckedBeforeAppend() {
        QImage tooWide(32768, 32, QImage::Format_ARGB32);
        ScrollStitcher stitcher;
        stitcher.reset(tooWide);
        QVERIFY(stitcher.picture().isNull());
        // At width 16000 the editor's 32M-pixel limit is reached at height 2000.
        QImage source(16000, 2300, QImage::Format_ARGB32);
        quint32 random = 19;
        for (int y = 0; y < source.height(); ++y) {
            random = random * 1664525u + 1013904223u;
            const QRgb color = qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255);
            auto scan = reinterpret_cast<QRgb *>(source.scanLine(y));
            std::fill(scan, scan + source.width(), color);
        }
        stitcher.reset(frame(source, 0, 1900));
        QCOMPARE(stitcher.add(frame(source, 137, 1900)), -1);
        QVERIFY(stitcher.atLimit());
        QCOMPARE(stitcher.height(), 1900);
    }
    void ultraLongStorageAndPngExportPreservePixelsPastPainterLimit() {
#ifdef Q_OS_WIN
        QImage source(16, 45000, QImage::Format_ARGB32);
        quint32 random = 19;
        for (int y = 0; y < source.height(); ++y) {
            random = random * 1664525u + 1013904223u;
            auto *scan = reinterpret_cast<QRgb *>(source.scanLine(y));
            for (int x = 0; x < source.width(); ++x)
                scan[x] = qRgb(random >> 24, ((random >> 16) + x * 31) & 255, (random >> 8) & 255);
        }
        ScrollCapture capture;
        capture.setUltraLong(true);
        QVERIFY(capture.begin(frame(source, 0, 200)));
        for (int offset = 64; offset <= 44800; offset += 64)
            QCOMPARE(capture.take(frame(source, offset, 200)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.frames(), 700);
        QCOMPARE(capture.size(), source.size());
        QVERIFY(!capture.atLimit());
        QVERIFY(!capture.fitsImageLimits());
        QVERIFY(capture.picture().isNull());
        const QImage preview = capture.preview(160);
        QVERIFY(!preview.isNull() && preview.height() <= 720);
        const QImage region = capture.previewRegion(QRect(0, 32070, 16, 320), 16);
        QCOMPARE(region.size(), QSize(16, 320));
        for (int y = 0; y < region.height(); ++y)
            QVERIFY(std::memcmp(region.constScanLine(y), source.constScanLine(32070 + y), 16 * 4) == 0);
        QTemporaryDir directory;
        const QString path = directory.filePath("vertical.png");
        QString error;
        QVERIFY2(capture.savePng(path, &error), qPrintable(error));
        QImageReader reader(path);
        QCOMPARE(reader.size(), source.size());
        const QImage decoded = reader.read().convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(!decoded.isNull(), qPrintable(reader.errorString()));
        for (int y = 0; y < source.height(); ++y)
            QVERIFY2(std::memcmp(decoded.constScanLine(y), source.constScanLine(y), size_t(source.width()) * 4) == 0,
                     qPrintable(QStringLiteral("PNG differs at row %1: %2 / %3")
                         .arg(y).arg(decoded.pixelColor(0, y).name(QColor::HexArgb))
                         .arg(source.pixelColor(0, y).name(QColor::HexArgb))));

        ScrollStitcher horizontal;
        horizontal.setUltraLong(true);
        horizontal.reset(frame(source, 0, 1600));
        for (int offset = 1200; offset <= 43200; offset += 1200)
            QVERIFY(horizontal.add(frame(source, offset, 1600)) > 0);
        QVERIFY(horizontal.add(frame(source, 43400, 1600)) > 0);
        const QString widePath = directory.filePath("horizontal.png");
        QVERIFY2(horizontal.savePng(widePath, true, &error), qPrintable(error));
        const QImage wide(widePath);
        QCOMPARE(wide.size(), QSize(45000, 16));
        for (const QPoint point : {QPoint(0, 0), QPoint(32000, 9), QPoint(44999, 15)})
            QCOMPARE(wide.pixelColor(point), source.pixelColor(point.y(), point.x()));
#else
        QSKIP("Streaming ultra-long PNG export currently uses the Windows capture platform.");
#endif
    }
    void twoMillionPixelCaptureReachesItsLimitAndExportsTheWholePng() {
#ifdef Q_OS_WIN
        if (!qEnvironmentVariableIsSet("H2D_ULTRA_LONG_TEST"))
            QSKIP("Set H2D_ULTRA_LONG_TEST=1 to run the full two-million-pixel capture.");
        QImage source(8, 2000200, QImage::Format_ARGB32);
        quint32 random = 19;
        for (int y = 0; y < source.height(); ++y) {
            random = random * 1664525u + 1013904223u;
            auto *scan = reinterpret_cast<QRgb *>(source.scanLine(y));
            std::fill(scan, scan + source.width(), qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255));
        }
        ScrollCapture capture;
        capture.setUltraLong(true);
        QVERIFY(capture.begin(source.copy(0, 0, 8, 8192)));
        for (int offset = 6000; offset <= 1986000; offset += 6000)
            QCOMPARE(capture.take(source.copy(0, offset, 8, 8192)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.take(source.copy(0, 1991808, 8, 8192)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.size(), QSize(8, 2000000));
        QVERIFY(capture.atLimit());
        const QImage region = capture.previewRegion(QRect(0, 1985900, 8, 360), 8);
        QCOMPARE(region.size(), QSize(8, 360));
        for (int y = 0; y < region.height(); ++y)
            QVERIFY(std::memcmp(region.constScanLine(y), source.constScanLine(1985900 + y), 8 * 4) == 0);
        QCOMPARE(capture.take(source.copy(0, 1992000, 8, 8192)), ScrollCapture::Outcome::Failed);
        QCOMPARE(capture.size(), QSize(8, 2000000));
        QTemporaryDir temporary;
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS", temporary.path());
        QVERIFY(QDir().mkpath(folder));
        const QString path = QDir(folder).filePath("longcapture-2000000.png");
        QString error;
        QVERIFY2(capture.savePng(path, &error), qPrintable(error));
        QFile png(path);
        QVERIFY(png.open(QIODevice::ReadOnly));
        const QByteArray header = png.read(24);
        QCOMPARE(qFromBigEndian<quint32>(header.constData() + 16), quint32(8));
        QCOMPARE(qFromBigEndian<quint32>(header.constData() + 20), quint32(2000000));
        QVERIFY(png.seek(png.size() - 12));
        QCOMPARE(png.read(12).mid(4, 4), QByteArray("IEND"));
#else
        QSKIP("Ultra-long capture currently uses the Windows capture platform.");
#endif
    }
    void sizeLimitStillAllowsResumeAndRevisitOfAcceptedContent() {
        QImage source(8, 32900, QImage::Format_ARGB32);
        quint32 random = 19;
        for (int y = 0; y < source.height(); ++y) {
            random = random * 1664525u + 1013904223u;
            const QRgb color = qRgb(random >> 24, (random >> 16) & 255, (random >> 8) & 255);
            auto scan = reinterpret_cast<QRgb *>(source.scanLine(y));
            std::fill(scan, scan + source.width(), color);
        }
        ScrollCapture capture;
        QVERIFY(capture.begin(frame(source, 0, 160)));
        for (int offset = 120; offset <= 32520; offset += 120)
            QCOMPARE(capture.take(frame(source, offset, 160)), ScrollCapture::Outcome::Added);
        QCOMPARE(capture.height(), 32680);
        QCOMPARE(capture.take(frame(source, 32640, 160)), ScrollCapture::Outcome::Failed);
        QVERIFY(capture.atLimit());
        const int frames = capture.frames();
        capture.pause();
        capture.resume();
        QVERIFY(capture.running());
        QCOMPARE(capture.take(frame(source, 32400, 160)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(capture.frames(), frames);
        QCOMPARE(capture.height(), 32680);
        QCOMPARE(capture.viewportRect(), QRect(0, 32400, 8, 160));
        QVERIFY(capture.matched());
    }
    void fullHdMatchingPerformance() {
        const QImage source = page(1920, 2400);
        ScrollStitcher stitcher;
        stitcher.reset(frame(source, 0, 1080));
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(stitcher.add(frame(source, 173, 1080)), 173);
        qInfo() << "1920x1080 sparse text matching:" << timer.elapsed() << "ms";
    }
};
QTEST_MAIN(ScrollStitchTests)
#include "scrollstitch_test.moc"
