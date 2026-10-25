#include "ocr.h"
#include "model.h"
#include "ocrdialog.h"
#include <QComboBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace h2d;
class OcrTests : public QObject {
    Q_OBJECT
  private slots:
    void aReusedDialogShowsOnlyTheNewPictureAndLanguage() {
        QImage first(200, 100, QImage::Format_ARGB32);
        first.fill(Qt::red);
        OcrDialog dialog(first, OcrLanguageMode::English);
        dialog.show();
        QTest::qWait(30);
        auto *preview = dialog.findChild<QWidget *>("ocrPreview");
        QVERIFY(preview);
        const auto before = preview->grab().toImage();
        QCOMPARE(before.pixelColor(before.rect().center()), QColor(Qt::red));
        OcrResult oldResult;
        oldResult.ok = true;
        oldResult.lines = {{"old picture", {}}};
        dialog.setResult(oldResult);
        QCOMPARE(dialog.text(), QString("old picture"));
        QImage second(100, 200, QImage::Format_ARGB32);
        second.fill(Qt::green);
        dialog.setImage(second, OcrLanguageMode::SimplifiedChinese);
        const auto after = preview->grab().toImage();
        QCOMPARE(after.pixelColor(after.rect().center()), QColor(Qt::green));
        QVERIFY(dialog.text().isEmpty());
        QCOMPARE(dialog.findChild<QListWidget *>()->count(), 0);
        auto *language = dialog.findChild<QComboBox *>();
        QVERIFY(language);
        QCOMPARE(language->currentData().toInt(), int(OcrLanguageMode::SimplifiedChinese));
        QVERIFY(!language->isEnabled());
    }
    void tesseractLinesKeepBandCoordinates() {
        const QByteArray header = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n";
        QVector<OcrLine> lines;
        QVERIFY(parseTesseractTsv(header + "5\t1\t1\t1\t1\t1\t10\t20\t30\t15\t90\tHello\n"
                                           "5\t1\t1\t1\t1\t2\t45\t20\t40\t15\t91\tworld\n",
                                  QRect(0, 100, 200, 100), QSize(200, 400), &lines));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].text, QString("Hello world"));
        QCOMPARE(lines[0].box, QRectF(0.05, 0.30, 0.375, 0.0375));
        QVERIFY(!parseTesseractTsv(header + "5\t1\t1\t1\t1\t1\t-1\t20\t30\t15\t90\tBad\n",
                                   QRect(0, 0, 200, 100), QSize(200, 100), &lines));
        QVERIFY(!parseTesseractTsv("not a TSV", QRect(0, 0, 200, 100), QSize(200, 100), &lines));
    }
  private:
    // Rows of a five by seven pixel font, one string per row. Qt ships no fonts for
    // the offscreen platform, so drawing real text there would produce a blank
    // picture and a test that passes for the wrong reason.
    static QVector<QString> glyph(char letter) {
        switch (letter) {
        case 'D':
            return {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
        case 'E':
            return {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
        case 'H':
            return {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
        case 'I':
            return {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
        case 'R':
            return {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
        case 'T':
            return {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
        default:
            return {};
        }
    }
    // A picture with one big word on it, which is about the easiest thing a
    // recogniser can be asked to read.
    static QImage sampleImage(const QString &text) {
        constexpr int scale = 14; // A glyph is therefore 70 by 98 pixels.
        constexpr int columnsPerGlyph = 5, rowsPerGlyph = 7, gap = 1, margin = 4;
        const int glyphs = text.size();
        const QSize size((glyphs * (columnsPerGlyph + gap) - gap + 2 * margin) * scale,
                         (rowsPerGlyph + 2 * margin) * scale);
        QImage image(size, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        int cursor = margin;
        for (const QChar letter : text) {
            const auto rows = glyph(letter.toUpper().toLatin1());
            for (int row = 0; row < rows.size(); ++row) {
                for (int column = 0; column < rows.at(row).size(); ++column) {
                    if (rows.at(row).at(column) != QLatin1Char('1'))
                        continue;
                    painter.fillRect(QRect((cursor + column) * scale, (margin + row) * scale, scale, scale),
                                     Qt::black);
                }
            }
            cursor += columnsPerGlyph + gap;
        }
        painter.end();
        return image;
    }
    // recognize() answers through a callback, so the test waits for it instead of
    // blocking the thread the answer is delivered on.
    static OcrResult recognizeAndWait(OcrEngine &engine, const QImage &image,
                                      OcrLanguageMode language, bool *answered) {
        OcrResult captured;
        bool done = false;
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        guard.setInterval(60000);
        connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        engine.recognize(image, language, [&](OcrResult result) {
            captured = std::move(result);
            done = true;
            loop.quit();
        });
        guard.start();
        if (!done)
            loop.exec();
        if (answered)
            *answered = done;
        return captured;
    }
  private slots:
    void languageChoiceMapsToATag() {
        QCOMPARE(ocrLanguageTag(OcrLanguageMode::System), QString());
        QCOMPARE(ocrLanguageTag(OcrLanguageMode::SimplifiedChinese), QStringLiteral("zh-Hans-CN"));
        QCOMPARE(ocrLanguageTag(OcrLanguageMode::English), QStringLiteral("en-US"));
        // Every choice has something to show in a menu, and the two that name a
        // language keep that name in every interface language.
        for (const auto mode : {OcrLanguageMode::System, OcrLanguageMode::SimplifiedChinese,
                                OcrLanguageMode::English})
            QVERIFY(!ocrLanguageLabel(mode).isEmpty());
        QCOMPARE(ocrLanguageLabel(OcrLanguageMode::English), QStringLiteral("English"));
        QCOMPARE(ocrLanguageLabel(OcrLanguageMode::SimplifiedChinese), QString::fromUtf8("简体中文"));
    }
    void bandsCoverTheWholePictureWithoutOversizedOnes() {
        const QSize size(1200, 25000);
        const int maximum = ocrMaxImageDimension();
        const auto bands = ocrBands(size, maximum);
        QVERIFY(bands.size() > 1);
        QCOMPARE(bands.first().top(), 0);
        // The last band has to reach the bottom, otherwise the reading silently
        // stops half way down a long page.
        QCOMPARE(bands.last().bottom() + 1, size.height());
        int covered = 0;
        for (int index = 0; index < bands.size(); ++index) {
            const auto &band = bands.at(index);
            QVERIFY2(band.height() <= maximum, "a band is taller than the recogniser accepts");
            QVERIFY(band.width() <= size.width());
            QCOMPARE(band.left(), 0);
            QVERIFY(band.bottom() < size.height());
            // Consecutive bands overlap, so a line that straddles a boundary is
            // still complete somewhere.
            if (index > 0)
                QVERIFY(band.top() < bands.at(index - 1).bottom() + 1);
            if (index == 0)
                covered = band.height();
            else
                covered += band.bottom() - bands.at(index - 1).bottom();
        }
        QCOMPARE(covered, size.height());
    }
    void pictureThatFitsIsStillOneBand() {
        const auto bands = ocrBands(QSize(640, 480), ocrMaxImageDimension());
        QCOMPARE(bands.size(), 1);
        QCOMPARE(bands.first(), QRect(0, 0, 640, 480));
        QVERIFY(ocrBands(QSize(), ocrMaxImageDimension()).isEmpty());
    }
    void smallTextIsMeasuredAndEnlargedEvenOnALargeCrop() {
        // A card of 500 x 120 whose labels are ten pixels tall: the sheet is not
        // tiny, so a short-edge rule alone would hand the recogniser unreadable
        // glyphs. The measured line height is what decides here.
        QImage card(500, 120, QImage::Format_ARGB32);
        card.fill(Qt::white);
        {
            QPainter painter(&card);
            painter.setPen(Qt::black);
            QFont font = painter.font();
            font.setPixelSize(10);
            painter.setFont(font);
            painter.drawText(20, 40, "Name");
            painter.drawText(20, 80, "Value");
        }
        const int measured = estimateOcrTextHeight(card);
        QVERIFY2(measured >= 6 && measured <= 16, qPrintable(QString::number(measured)));
        const auto scaled = scaleForOcr(card, ocrMaxImageDimension());
        QVERIFY(scaled.width() > card.width());
        QVERIFY(scaled.width() <= card.width() * 4);
        QVERIFY(estimateOcrTextHeight(scaled) >= 24);
        // A blank sheet has nothing to measure and stays as it is.
        QImage blank(500, 400, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        QCOMPARE(estimateOcrTextHeight(blank), 0);
    }
    void faintTextGainsContrastBeforeRecognition() {
        const auto grayRange = [](const QImage &image) {
            const auto gray = image.convertToFormat(QImage::Format_Grayscale8);
            int low = 255, high = 0;
            for (int y = 0; y < gray.height(); ++y) {
                const uchar *row = gray.constScanLine(y);
                for (int x = 0; x < gray.width(); ++x) {
                    low = std::min(low, int(row[x]));
                    high = std::max(high, int(row[x]));
                }
            }
            return QPair<int, int>(low, high);
        };
        // Grey labels on a dark toolbar: legible to a person, too faint for the
        // recogniser once it has been enlarged.
        QImage faint(200, 60, QImage::Format_ARGB32);
        faint.fill(QColor("#22252b"));
        {
            QPainter painter(&faint);
            painter.setPen(QColor("#6d727a"));
            QFont font = painter.font();
            font.setPixelSize(14);
            painter.setFont(font);
            painter.drawText(20, 38, "123");
        }
        const auto before = grayRange(faint);
        QVERIFY(before.second - before.first < 160);
        const auto stretched = stretchOcrContrast(faint);
        const auto after = grayRange(stretched);
        QVERIFY2(after.second - after.first >= 240, qPrintable(QStringLiteral("%1..%2").arg(after.first).arg(after.second)));
        // A crop that already spans the range is handed on untouched.
        QImage strong(120, 40, QImage::Format_ARGB32);
        strong.fill(Qt::white);
        {
            QPainter painter(&strong);
            painter.setPen(Qt::black);
            painter.fillRect(QRect(10, 10, 30, 20), Qt::black);
        }
        const auto unchanged = stretchOcrContrast(strong);
        QCOMPARE(unchanged.pixelColor(2, 2), strong.pixelColor(2, 2));
        QCOMPARE(grayRange(unchanged), grayRange(strong));
    }
    void cropsGetAPaddedOpaqueMarginThatBoxesMapBackThrough() {
        QCOMPARE(ocrPaddingFor(QSize()), 0);
        QCOMPARE(ocrPaddingFor(QSize(40, 40)), 6);
        QCOMPARE(ocrPaddingFor(QSize(100, 60)), 6);
        QCOMPARE(ocrPaddingFor(QSize(400, 400)), 24);
        QImage crop(200, 100, QImage::Format_ARGB32_Premultiplied);
        crop.fill(Qt::transparent);
        {
            QPainter painter(&crop);
            painter.fillRect(QRect(80, 40, 20, 10), Qt::black);
        }
        crop.setDevicePixelRatio(2);
        const int padding = ocrPaddingFor(crop.size());
        const auto padded = padForOcr(crop, padding);
        QCOMPARE(padded.size(), QSize(200 + padding * 2, 100 + padding * 2));
        QCOMPARE(padded.devicePixelRatio(), 2.0);
        // Transparent pixels read as black to the recogniser, so the canvas is opaque.
        QVERIFY(padded.format() == QImage::Format_RGB32);
        QCOMPARE(padded.pixelColor(padding / 2, padding / 2), QColor(Qt::white));
        QCOMPARE(padded.pixelColor(padding + 85, padding + 45), QColor(Qt::black));
        // The engine reports boxes in the padded picture, so a glyph at (20, 10)
        // of the crop arrives at (padding + 20, padding + 10).
        const QRectF box(padding + 20, padding + 10, 60, 30);
        const PreparedOcrImage prepared{padded, QRect(QPoint(padding, padding), crop.size())};
        const QRectF relative = prepared.sourceBox(ocrBandBoxToImage(box, padded.rect(), padded.size()));
        QVERIFY(qAbs(relative.x() - 20.0 / 200) < 1e-9);
        QVERIFY(qAbs(relative.y() - 10.0 / 100) < 1e-9);
        QVERIFY(qAbs(relative.width() - 60.0 / 200) < 1e-9);
        QVERIFY(qAbs(relative.height() - 30.0 / 100) < 1e-9);
    }
    void preparedImageMapsBothNativeAndNormalisedBoxesToTheSource() {
        QImage crop(200, 100, QImage::Format_RGB32);
        crop.fill(Qt::white);
        const auto prepared = prepareForOcr(crop, ocrMaxImageDimension());
        const auto area = prepared.contentRect;
        const QRectF source(0.1, 0.2, 0.3, 0.4);
        const QRectF native(area.x() + source.x() * area.width(),
                            area.y() + source.y() * area.height(),
                            source.width() * area.width(), source.height() * area.height());
        // Windows/Tesseract first map band-local pixel boxes to the prepared
        // image; Vision already returns these same image fractions.
        const QRect band(0, 10, prepared.image.width(), prepared.image.height() - 10);
        const QRectF bandLocal = native.translated(0, -10);
        const QRectF windows = ocrBandBoxToImage(bandLocal, band, prepared.image.size());
        const QRectF vision(native.x() / prepared.image.width(), native.y() / prepared.image.height(),
                             native.width() / prepared.image.width(), native.height() / prepared.image.height());
        const auto compareBox = [](const QRectF &actual, const QRectF &expected) {
            return qAbs(actual.x() - expected.x()) < 1e-9 && qAbs(actual.y() - expected.y()) < 1e-9 &&
                   qAbs(actual.width() - expected.width()) < 1e-9 &&
                   qAbs(actual.height() - expected.height()) < 1e-9;
        };
        QVERIFY(compareBox(prepared.sourceBox(windows), source));
        QVERIFY(compareBox(prepared.sourceBox(vision), source));
        // Clip the rectangle as a whole, rather than clamping x and width
        // independently and shifting an out-of-bounds part back into the image.
        const QRectF crossing((area.x() - 10.0) / prepared.image.width(),
                               qreal(area.y()) / prepared.image.height(),
                               20.0 / prepared.image.width(), 10.0 / prepared.image.height());
        const auto clipped = prepared.sourceBox(crossing);
        QVERIFY(qAbs(clipped.x()) < 1e-9);
        QVERIFY(qAbs(clipped.width() - 10.0 / area.width()) < 1e-9);
    }
    void opaquePaddingAcceptsNativeAndCompactImageFormats() {
        for (const auto format : {QImage::Format_RGBA8888_Premultiplied,
                                  QImage::Format_RGB888, QImage::Format_RGB32}) {
            QImage image(40, 20, format);
            const QColor blue(20, 60, 180);
            image.fill(blue);
            const auto padded = padForOcr(image, 6);
            QCOMPARE(padded.pixelColor(1, 1), blue);
            QCOMPARE(padded.pixelColor(20, 15), blue);
        }
        QImage gray(40, 20, QImage::Format_Grayscale8);
        gray.fill(90);
        const auto paddedGray = padForOcr(gray, 6);
        QCOMPARE(paddedGray.pixelColor(1, 1), QColor(90, 90, 90));
    }
    void transparentWhiteAndDarkGlyphsKeepContrastingBackgrounds() {
        for (const QColor ink : {QColor(Qt::white), QColor(Qt::black)}) {
            QImage mask(80, 40, QImage::Format_ARGB32_Premultiplied);
            mask.fill(Qt::transparent);
            {
                QPainter painter(&mask);
                painter.fillRect(QRect(20, 10, 20, 15), ink);
            }
            const auto padded = padForOcr(mask, 6);
            const QColor background = ink == QColor(Qt::white) ? QColor(Qt::black) : QColor(Qt::white);
            QCOMPARE(padded.pixelColor(1, 1), background);
            QCOMPARE(padded.pixelColor(7, 7), background);
            QCOMPARE(padded.pixelColor(31, 21), ink);
        }
        QImage white(80, 40, QImage::Format_RGBA8888_Premultiplied);
        white.fill(Qt::transparent);
        {
            QPainter painter(&white);
            painter.fillRect(QRect(20, 10, 20, 15), QColor(255, 255, 255, 128));
        }
        const auto padded = padForOcr(white, 6);
        QCOMPARE(padded.pixelColor(1, 1), QColor(Qt::black));
        QCOMPARE(padded.pixelColor(31, 21), QColor(128, 128, 128));
    }
    void completePreparationIncludesTheMarginInTheWidthLimit() {
        QImage wide(1000, 60, QImage::Format_RGB32);
        wide.fill(Qt::white);
        const auto prepared = prepareForOcr(wide, 800);
        QVERIFY(prepared.image.width() <= 800);
        QVERIFY(prepared.contentRect.width() < prepared.image.width());
        QVERIFY(prepared.image.rect().contains(prepared.contentRect));
    }
    void contrastStretchRetainsInkSmallerThanAPercentile() {
        QImage sparse(500, 100, QImage::Format_RGB32);
        sparse.fill(QColor(100, 100, 100));
        sparse.setPixelColor(250, 50, QColor(130, 130, 130));
        const auto contrasted = stretchOcrContrast(sparse);
        QCOMPARE(contrasted.pixelColor(250, 50), QColor(Qt::white));
        QCOMPARE(contrasted.pixelColor(1, 1), QColor(Qt::black));
    }
    void ordinaryPicturesStayUntouchedAndWidePicturesFit() {
        // A small limit is used so the test does not have to allocate a picture the
        // size of a wall to exercise the same branch. Both pictures are filled
        // explicitly: an unfilled QImage holds whatever the allocator left
        // behind, and stray pixels can read as small text, which legitimately
        // triggers the enlargement path on one platform and not another.
        QImage wide(2000, 400, QImage::Format_ARGB32);
        wide.fill(Qt::white);
        const auto scaled = scaleForOcr(wide, 800);
        QCOMPARE(scaled.width(), 800);
        QCOMPARE(scaled.height(), 160);
        // A picture that already fits is handed on untouched rather than being
        // resampled, which would only cost accuracy.
        QImage small(640, 480, QImage::Format_ARGB32);
        small.fill(Qt::white);
        QCOMPARE(scaleForOcr(small, 800).size(), small.size());
        QVERIFY(scaleForOcr(QImage(), 800).isNull());
    }
    void narrowCropsAreEnlargedWithoutMovingRelativeBoxes() {
        QImage small(380, 19, QImage::Format_ARGB32);
        small.fill(Qt::white);
        small.setDevicePixelRatio(2);
        const auto scaled = scaleForOcr(small, ocrMaxImageDimension());
        QVERIFY(scaled.height() >= 64);
        QCOMPARE(scaled.width() / scaled.height(), 20);
        QCOMPARE(scaled.devicePixelRatio(), small.devicePixelRatio());
        const QRectF originalBox(10, 3, 350, 12);
        const qreal scale = qreal(scaled.width()) / small.width();
        const QRectF scaledBox(originalBox.x() * scale, originalBox.y() * scale,
                               originalBox.width() * scale, originalBox.height() * scale);
        const auto originalRelative = ocrBandBoxToImage(originalBox, small.rect(), small.size());
        const auto scaledRelative = ocrBandBoxToImage(scaledBox, scaled.rect(), scaled.size());
        QVERIFY(qAbs(originalRelative.x() - scaledRelative.x()) < 0.000001);
        QVERIFY(qAbs(originalRelative.y() - scaledRelative.y()) < 0.000001);
        QVERIFY(qAbs(originalRelative.width() - scaledRelative.width()) < 0.000001);
        QVERIFY(qAbs(originalRelative.height() - scaledRelative.height()) < 0.000001);
    }
    void enlargingSmallPicturesRespectsTheEngineAndMemoryLimits() {
        const QImage narrow(380, 19, QImage::Format_ARGB32);
        const auto limited = scaleForOcr(narrow, 800);
        QCOMPARE(limited.width(), 800);
        QCOMPARE(limited.height(), 40);
        const QImage tiny(8, 4, QImage::Format_ARGB32);
        const auto enlarged = scaleForOcr(tiny, ocrMaxImageDimension());
        QVERIFY(enlarged.width() <= tiny.width() * 4);
        QVERIFY(enlarged.height() <= tiny.height() * 4);
        QCOMPARE(scaleForOcr(narrow, 0).size(), narrow.size());
        const QImage tall(19, 4000, QImage::Format_ARGB32);
        const auto enlargedTall = scaleForOcr(tall, ocrMaxImageDimension());
        const auto bands = ocrBands(enlargedTall.size(), ocrMaxImageDimension());
        QVERIFY(bands.size() > 1);
        QCOMPARE(bands.last().bottom() + 1, enlargedTall.height());
        for (const auto &band : bands) {
            QVERIFY(band.width() <= ocrMaxImageDimension());
            QVERIFY(band.height() <= ocrMaxImageDimension());
        }
    }
    void cjkWordGeometryKeepsRealSeparators() {
        // Close CJK words are per-character OCR segmentation, but a wider gap is
        // meaningful: a menu's "设置    帮助" must not turn into one word.
        const QVector<OcrLine> tight{{QStringLiteral("设"), {0, 0, 16, 20}},
                                    {QStringLiteral("置"), {18, 0, 16, 20}}};
        QCOMPARE(joinOcrWords(tight), QStringLiteral("设置"));
        auto separated = tight;
        separated[1].box.moveLeft(28);
        QCOMPARE(joinOcrWords(separated), QStringLiteral("设 置"));
        QCOMPARE(joinOcrWords({{QStringLiteral("中文"), {0, 0, 32, 20}},
                               {QStringLiteral("English"), {34, 0, 60, 20}}}),
                 QStringLiteral("中文 English"));
        QCOMPARE(joinOcrWords({{QStringLiteral("Ａ"), {0, 0, 16, 20}},
                               {QStringLiteral("Ｂ"), {18, 0, 16, 20}}}), QStringLiteral("Ａ Ｂ"));
        QCOMPARE(joinOcrWords({{QStringLiteral("甲 乙"), {0, 0, 50, 20}}}), QStringLiteral("甲 乙"));
        QCOMPARE(joinOcrWords({{QStringLiteral("甲\u3000乙"), {0, 0, 50, 20}}}),
                 QStringLiteral("甲\u3000乙"));
        QCOMPARE(joinOcrWords({}), QString());
    }
    void windowsWordBoxesGuideCjkJoiningAndLegacyTextStaysIntact() {
        QVector<OcrLine> lines;
        QString language, error;
        const QByteArray payload = QStringLiteral(R"({"engineLanguage":"zh-Hans-CN","lines":[
            {"text":"设 置 帮 助","x":0,"y":0,"w":100,"h":20,"words":[
                {"text":"设","x":0,"y":0,"w":16,"h":20},
                {"text":"置","x":18,"y":0,"w":16,"h":20},
                {"text":"帮","x":60,"y":0,"w":16,"h":20},
                {"text":"助","x":78,"y":0,"w":16,"h":20}]},
            {"text":"甲 乙\u3000丙","x":0,"y":30,"w":100,"h":20}]})").toUtf8();
        QVERIFY2(parseOcrPayload(payload, QRect(0, 0, 120, 80), QSize(120, 80),
                                 &language, &lines, &error), qPrintable(error));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].text, QStringLiteral("设置 帮助"));
        // Text without word geometry is not evidence that a space is synthetic.
        QCOMPARE(lines[1].text, QStringLiteral("甲 乙\u3000丙"));
    }
    void tesseractCjkWordBoxesKeepMenuGaps() {
        const QByteArray header = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n";
        const QByteArray words = QStringLiteral(
            "5\t1\t1\t1\t1\t1\t0\t0\t16\t20\t90\t设\n"
            "5\t1\t1\t1\t1\t2\t18\t0\t16\t20\t90\t置\n"
            "5\t1\t1\t1\t1\t3\t60\t0\t16\t20\t90\t帮\n"
            "5\t1\t1\t1\t1\t4\t78\t0\t16\t20\t90\t助\n").toUtf8();
        QVector<OcrLine> lines;
        QVERIFY(parseTesseractTsv(header + words, QRect(0, 0, 120, 40), QSize(120, 40), &lines));
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].text, QStringLiteral("设置 帮助"));
    }
    void boxesOfABandLandOnTheWholePicture() {
        const QSize size(1000, 4000);
        const QRect band(0, 800, 1000, 800);
        // A line 100 pixels into the band is 900 pixels into the picture once the
        // band is put back where it came from.
        const auto box = ocrBandBoxToImage(QRectF(200, 100, 400, 50), band, size);
        QCOMPARE(box.x(), 0.2);
        QCOMPARE(box.y(), 0.225);
        QCOMPARE(box.width(), 0.4);
        QCOMPARE(box.height(), 0.0125);
        QVERIFY(ocrBandBoxToImage(QRectF(0, 0, 1, 1), band, QSize()).isEmpty());
    }
    void bridgeIsGivenNativePathsAndSafeQuoting() {
        const QString script = QStringLiteral("$i='__EDITHERE_IMAGE__'\n$o='__EDITHERE_OUTPUT__'\n$l='__EDITHERE_LANGUAGE__'");
        const QString prepared = prepareOcrBridge(
            script, QStringLiteral("C:/Users/小王's/AppData/Local/Temp/band-0.png"),
            QStringLiteral("C:/Temp/result.json"), QStringLiteral("zh-Hans-CN"));
#ifdef Q_OS_WIN
        // Qt hands out forward slashes everywhere; the Windows Runtime rejects them
        // with "the given path's format is not supported", so the script must never
        // see one. Only Windows turns them around, so this is asserted where it is
        // true rather than everywhere.
        QVERIFY2(!prepared.contains(QStringLiteral("C:/")), qPrintable(prepared));
        QVERIFY(prepared.contains(QStringLiteral("C:\\Users\\")));
#endif
        // A single quote in a path would end the PowerShell string literal early.
        QVERIFY2(!prepared.contains(QStringLiteral("小王's")), qPrintable(prepared));
        QVERIFY(prepared.contains(QStringLiteral("小王''s")));
        QVERIFY(prepared.contains(QStringLiteral("zh-Hans-CN")));
        for (const auto placeholder : {"__EDITHERE_IMAGE__", "__EDITHERE_OUTPUT__", "__EDITHERE_LANGUAGE__"})
            QVERIFY(!prepared.contains(QLatin1String(placeholder)));
        // With no language chosen the argument is empty, not a stray placeholder.
        const QString automatic = prepareOcrBridge(script, QStringLiteral("a.png"),
                                                   QStringLiteral("b.json"), QString());
        QVERIFY(automatic.contains(QStringLiteral("$l=''")));
    }
    void payloadIsReadInPictureCoordinates() {
        const QSize size(1000, 4000);
        const QRect band(0, 800, 1000, 800);
        const QByteArray payload = R"({"engineLanguage":"zh-Hans-CN","lines":[
            {"text":"第一行","x":0,"y":0,"w":500,"h":40},
            {"text":"second line","x":100,"y":100,"w":400,"h":50}]})";
        QString language;
        QVector<OcrLine> lines;
        QString error;
        QVERIFY2(parseOcrPayload(payload, band, size, &language, &lines, &error), qPrintable(error));
        QCOMPARE(language, QStringLiteral("zh-Hans-CN"));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.first().text, QString::fromUtf8("第一行"));
        QCOMPARE(lines.first().box.y(), 0.2);
        QCOMPARE(lines.last().box.y(), 0.225);
        // A blank band still reports the language, which is what the result window
        // names, so an empty list of lines is a valid answer.
        QVERIFY(parseOcrPayload(R"({"engineLanguage":"en-US","lines":[]})", band, size, &language, &lines,
                               &error));
        QVERIFY(lines.isEmpty());
        QCOMPARE(language, QStringLiteral("en-US"));
    }
    void payloadThatCannotBeTrustedIsRejected() {
        const QSize size(100, 100);
        const QRect band(0, 0, 100, 100);
        QString language, error;
        QVector<OcrLine> lines;
        QVERIFY(!parseOcrPayload(QByteArray("not json at all"), band, size, &language, &lines, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseOcrPayload(QByteArray("[]"), band, size, &language, &lines, &error));
        QVERIFY(!parseOcrPayload(R"({"engineLanguage":"en-US"})", band, size, &language, &lines, &error));
        // Without a picture size there is nothing to make the boxes relative to.
        QVERIFY(!parseOcrPayload(R"({"lines":[]})", band, QSize(), &language, &lines, &error));
        // An empty line is dropped rather than shown as a blank row.
        QVERIFY(parseOcrPayload(R"({"engineLanguage":"en-US","lines":[{"text":"","x":0,"y":0,"w":1,"h":1}]})",
                                band, size, &language, &lines, &error));
        QVERIFY(lines.isEmpty());
    }
    void resultsReadBackAsLinesOfText() {
        OcrResult result;
        result.lines = {{QString::fromUtf8("第一行"), {}}, {QStringLiteral("second"), {}}};
        QCOMPARE(result.text(), QString::fromUtf8("第一行\nsecond"));
        QVERIFY(OcrResult().text().isEmpty());
    }
    void rapidRequestsAnswerOnlyTheLatestPicture() {
        if (!OcrEngine::supported())
            QSKIP("this build has no recogniser");
#ifdef Q_OS_LINUX
        const QString english = QStringLiteral("eng");
#else
        const QString english = QStringLiteral("en-US");
#endif
        if (!OcrEngine::availableLanguages().contains(english, Qt::CaseInsensitive))
            QSKIP("no English recogniser is installed for this session");
        QTest::failOnWarning(QRegularExpression("^(QTemporaryDir: Unable to remove|QProcess: Destroyed while process)"));
        OcrEngine engine;
        const auto image = sampleImage(QStringLiteral("EDITHERE"));
        int abandoned = 0, latest = 0;
        for (int request = 0; request < 5; ++request)
            engine.recognize(image, OcrLanguageMode::English,
                             [&](OcrResult) { ++abandoned; });
        QImage blank(400, 200, QImage::Format_RGB32);
        blank.fill(Qt::white);
        OcrResult result;
        engine.recognize(blank, OcrLanguageMode::English,
                         [&](OcrResult answer) { ++latest; result = std::move(answer); });
        QVERIFY(engine.busy());
#ifdef Q_OS_APPLE
        // No event processing has delivered a completion yet. Replacements must
        // queue behind the original native request, even if it has just finished
        // on its worker thread. Deleting cancelled watchers hid parallel work.
        QCOMPARE(engine.findChildren<QFutureWatcherBase *>().size(), 1);
#endif
        QTRY_COMPARE_WITH_TIMEOUT(latest, 1, 60000);
        QCOMPARE(abandoned, 0);
        QVERIFY(!engine.busy());
        QTRY_VERIFY_WITH_TIMEOUT(engine.findChildren<QProcess *>().isEmpty(), 1000);
#ifdef Q_OS_APPLE
        QTRY_VERIFY_WITH_TIMEOUT(engine.findChildren<QFutureWatcherBase *>().isEmpty(), 1000);
#endif
        if (!result.ok && result.failure == OcrFailure::NoEngine)
            QSKIP("no English recogniser is installed for this session");
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.lines.isEmpty());
    }
#ifdef Q_OS_APPLE
    void cancellingQueuedVisionWorkLeavesNoReplacementOrCallback() {
        OcrEngine engine;
        const auto image = sampleImage(QStringLiteral("EDITHERE"));
        int replies = 0;
        engine.recognize(image, OcrLanguageMode::English, [&](OcrResult) { ++replies; });
        engine.recognize(image, OcrLanguageMode::English, [&](OcrResult) { ++replies; });
        engine.cancel();
        QVERIFY(!engine.busy());
        QCOMPARE(engine.findChildren<QFutureWatcherBase *>().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(engine.findChildren<QFutureWatcherBase *>().isEmpty(), 60000);
        QCOMPARE(replies, 0);
        QVERIFY(!engine.busy());
    }
#endif
#ifdef Q_OS_WIN
    void missingHelperReportsFailureAndStopsBeingBusy() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previousPath = qgetenv("PATH");
        const bool hadPath = qEnvironmentVariableIsSet("PATH");
        qputenv("PATH", directory.path().toLocal8Bit());
        OcrEngine engine;
        int replies = 0;
        OcrResult result;
        engine.recognize(sampleImage("EDITHERE"), OcrLanguageMode::English,
                         [&](OcrResult answer) { ++replies; result = std::move(answer); });
        QTest::qWait(500);
        if (hadPath) qputenv("PATH", previousPath);
        else qunsetenv("PATH");
        QCOMPARE(replies, 1);
        QVERIFY(!engine.busy());
        QVERIFY(!result.ok);
        QCOMPARE(result.failure, OcrFailure::Unavailable);
        QVERIFY(!result.message.isEmpty());
        QTest::qWait(100);
        QCOMPARE(replies, 1);
    }
    void smallTextOnALargeCropIsEnlargedBeforeRecognition() {
        if (!OcrEngine::supported())
            QSKIP("no recogniser on this host");
        // A 600 x 200 area holding fourteen-pixel text: the sheet is far from tiny,
        // so only an enlargement driven by the measured line height can read it.
        QImage large(600, 200, QImage::Format_ARGB32);
        large.fill(Qt::white);
        {
            QPainter painter(&large);
            painter.drawImage(40, 80, sampleImage("EDITHERE").scaled(
                                         110, 30, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
        QVERIFY(estimateOcrTextHeight(large) <= 20);
        OcrEngine engine;
        bool answered = false;
        const auto result = recognizeAndWait(engine, large, OcrLanguageMode::English, &answered);
        QVERIFY(answered);
        QVERIFY(result.ok);
        // The blocky test font can lose one letter to the resampling, so the tail is
        // what the assertion pins down: the crop was read at all.
        QVERIFY2(result.text().toUpper().contains("THERE"), qPrintable(result.text()));
    }
    void cancellingAnActiveHelperDoesNotAnswerAnEarlierRequest() {
        QTest::failOnWarning(QRegularExpression("^(QTemporaryDir: Unable to remove|QProcess: Destroyed while process)"));
        OcrEngine engine;
        int abandoned = 0, replacement = 0;
        engine.recognize(sampleImage("EDITHERE"), OcrLanguageMode::English,
                         [&](OcrResult) { ++abandoned; });
        QVERIFY(engine.busy());
        auto *helper = engine.findChild<QProcess *>();
        QVERIFY(helper);
        const auto encoded = QByteArray::fromBase64(helper->arguments().last().toLatin1());
        const auto script = QString::fromUtf16(reinterpret_cast<const char16_t *>(encoded.constData()),
                                               encoded.size() / 2);
        const auto imagePath = QRegularExpression(R"(\$path = '((?:[^']|'')*)')").match(script);
        QVERIFY(imagePath.hasMatch());
        auto path = imagePath.captured(1);
        path.replace("''", "'");
        const auto scratch = QFileInfo(path).absolutePath();
        QVERIFY(QFileInfo::exists(path));
        engine.cancel();
        QVERIFY2(!QFileInfo::exists(scratch), qPrintable(scratch));
        engine.recognize(QImage(), OcrLanguageMode::English,
                         [&](OcrResult) { ++replacement; });
        QTest::qWait(100);
        QCOMPARE(abandoned, 0);
        QCOMPARE(replacement, 1);
        QVERIFY(!engine.busy());
    }
    void theBridgeScriptIsEmbedded() {
        QFile file(QStringLiteral(":/ocr/ocrbridge.ps1"));
        QVERIFY2(file.open(QIODevice::ReadOnly), "the PowerShell bridge is missing from the build");
        const QByteArray script = file.readAll();
        QVERIFY(script.contains("__EDITHERE_IMAGE__"));
        QVERIFY(script.contains("__EDITHERE_OUTPUT__"));
        QVERIFY(script.contains("__EDITHERE_LANGUAGE__"));
        // PowerShell reads the script from a command line, so a byte outside ASCII
        // would be decoded with whatever the console code page happens to be.
        bool ascii = true;
        for (const char character : script)
            ascii = ascii && uchar(character) < 0x80;
        QVERIFY2(ascii, "the bridge script is not ASCII-only");
        QVERIFY2(!script.startsWith("\xef\xbb\xbf"), "the bridge script must not carry a byte order mark");
    }
#endif
    void completedBandsLeaveNoHelperObjects() {
#ifndef Q_OS_WIN
        QSKIP("the helper lifetime regression concerns the Windows bridge");
#else
        OcrEngine engine;
        QImage blank(120, ocrMaxImageDimension() + 100, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        QVERIFY(ocrBands(blank.size(), ocrMaxImageDimension()).size() > 1);
        bool answered = false;
        const auto result = recognizeAndWait(engine, blank, OcrLanguageMode::System, &answered);
        QVERIFY2(answered, "the recogniser never answered");
        if (!result.ok && result.failure == OcrFailure::NoEngine)
            QSKIP("no recogniser language is installed for this session");
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.lines.isEmpty());
        QVERIFY(!engine.busy());
        QTRY_VERIFY_WITH_TIMEOUT(engine.findChildren<QProcess *>().isEmpty(), 1000);
#endif
    }
    void textIsReadBackFromAPicture() {
        if (!OcrEngine::supported())
            QSKIP("this build has no recogniser");
        const auto installed = OcrEngine::availableLanguages();
        qInfo().noquote() << "installed recogniser languages:" << installed.join(QStringLiteral(", "));
        if (installed.isEmpty())
            QSKIP("no recogniser language is installed on this machine");
        OcrEngine engine;
        QVERIFY(!engine.busy());
        bool answered = false;
        // English is asked for by name: the recogniser the system profile picks may
        // be one that reads Latin block letters far less well, and this test is
        // about the bridge carrying an answer back, not about how well it reads.
        const OcrResult result = recognizeAndWait(engine, sampleImage(QStringLiteral("EDITHERE")),
                                                 OcrLanguageMode::English, &answered);
        QVERIFY2(answered, "the recogniser never answered");
        QVERIFY(!engine.busy());
        if (!result.ok && result.failure == OcrFailure::NoEngine)
            QSKIP("no English recogniser is installed for this session");
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.message.isEmpty());
        QVERIFY(!result.engineLanguage.isEmpty());
        QCOMPARE(engine.engineLanguage(), result.engineLanguage);
        qInfo().noquote() << "read back:" << result.text();
        QVERIFY2(!result.lines.isEmpty(), "the recogniser found no text in a picture full of it");
        // Line breaks are a matter of taste, so the letters are what is compared.
        QString squashed = result.text();
        squashed.remove(QLatin1Char(' '));
        squashed.remove(QLatin1Char('\n'));
        QVERIFY2(squashed.length() >= 6, qPrintable(result.text()));
        // The tail of the word is unambiguous even in a five by seven block font,
        // which is what makes it worth asserting on.
        QVERIFY2(squashed.contains(QStringLiteral("THERE"), Qt::CaseInsensitive), qPrintable(result.text()));
        // The single word sits in the middle of the picture, which is what proves
        // the boxes were mapped back to the whole image and not left band-local.
        const auto box = result.lines.first().box;
        QVERIFY(box.center().x() > 0.2 && box.center().x() < 0.8);
        QVERIFY(box.center().y() > 0.2 && box.center().y() < 0.8);
        QVERIFY(box.width() > 0.0 && box.width() <= 1.0);
        QVERIFY(box.height() > 0.0 && box.height() <= 1.0);
    }
    void aNineteenPixelHighUrlIsReadBack_data() {
        QTest::addColumn<int>("languageMode");
        QTest::newRow("english") << int(OcrLanguageMode::English);
        QTest::newRow("system") << int(OcrLanguageMode::System);
        QTest::newRow("simplified-chinese") << int(OcrLanguageMode::SimplifiedChinese);
    }
    void aNineteenPixelHighUrlIsReadBack() {
#ifndef Q_OS_WIN
        QSKIP("this regression uses the Windows recogniser");
#else
        const QImage small(QStringLiteral(":/ocr-tests/ocr-small-url.png"));
        QCOMPARE(small.size(), QSize(380, 19));
        QFETCH(int, languageMode);
        const auto language = OcrLanguageMode(languageMode);
        const auto installed = OcrEngine::availableLanguages();
        const QString requestedTag = ocrLanguageTag(language);
        if (installed.isEmpty() || (!requestedTag.isEmpty() &&
                                   !installed.contains(requestedTag, Qt::CaseInsensitive)))
            QSKIP("the requested recogniser language is not installed for this session");
        OcrEngine engine;
        bool answered = false;
        const auto result = recognizeAndWait(engine, small, language, &answered);
        QVERIFY2(answered, "the recogniser never answered");
        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(result.text().trimmed(),
                 QStringLiteral("https://tianchi.aliyun.com/competition/entrance/532500"));
        QVERIFY(!engine.busy());
        for (const auto &line : result.lines) {
            QVERIFY(QRectF(0, 0, 1, 1).contains(line.box));
            QVERIFY(line.box.width() > 0.8);
            QVERIFY(line.box.height() > 0.3);
        }
#endif
    }
    void faintTextIsContrastedBeforeItsHeightChoosesTheScale() {
        QImage faint(500, 120, QImage::Format_RGB32);
        faint.fill(QColor(100, 100, 100));
        {
            QPainter painter(&faint);
            // A deterministic 8px ink band, below the raw ink-distance threshold.
            painter.fillRect(QRect(30, 30, 50, 8), QColor(130, 130, 130));
        }
        QCOMPARE(estimateOcrTextHeight(faint), 0);
        const auto prepared = prepareForOcr(faint, ocrMaxImageDimension());
        QCOMPARE(prepared.contentRect.size(), QSize(2000, 480));
        QCOMPARE(prepared.image.pixelColor(prepared.contentRect.topLeft() + QPoint(140, 132)),
                 QColor(Qt::white));
        QCOMPARE(prepared.image.pixelColor(prepared.contentRect.topLeft() + QPoint(10, 10)),
                 QColor(Qt::black));
    }
    void aPictureWithNoTextReadsEmpty() {
        if (!OcrEngine::supported())
            QSKIP("this build has no recogniser");
        OcrEngine engine;
        QImage blank(400, 200, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        bool answered = false;
        const OcrResult result = recognizeAndWait(engine, blank, OcrLanguageMode::System, &answered);
        QVERIFY2(answered, "the recogniser never answered");
        if (!result.ok && (result.failure == OcrFailure::NoEngine || result.failure == OcrFailure::Unsupported))
            QSKIP("no recogniser language is installed for this session");
        // Blank paper is a success with nothing in it, not a failure: the result
        // window says "no text" rather than showing an error.
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(result.lines.isEmpty());
    }
};
QTEST_MAIN(OcrTests)
#include "ocr_test.moc"
