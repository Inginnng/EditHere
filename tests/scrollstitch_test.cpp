#include "scrollcapture.h"
#include "scrollstitch.h"
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QPainter>
#include <QTest>
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
