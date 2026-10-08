#include "ocr.h"
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
    void ordinaryPicturesStayUntouchedAndWidePicturesFit() {
        // A small limit is used so the test does not have to allocate a picture the
        // size of a wall to exercise the same branch.
        const QImage wide(2000, 400, QImage::Format_ARGB32);
        const auto scaled = scaleForOcr(wide, 800);
        QCOMPARE(scaled.width(), 800);
        QCOMPARE(scaled.height(), 160);
        // A picture that already fits is handed on untouched rather than being
        // resampled, which would only cost accuracy.
        const QImage small(640, 480, QImage::Format_ARGB32);
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
