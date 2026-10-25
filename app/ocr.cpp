#include "ocr.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QLocale>
#include <QPainter>
#include <algorithm>
#ifdef Q_OS_APPLE
#include <QtConcurrent>
#endif

namespace h2d {
namespace {
// Free functions have no tr(); the enclosing "h2d" context groups them so the
// translation file stays easy to review.
inline QString tr(const char *text) {
    return QCoreApplication::translate("h2d", text);
}
// The recogniser that ships with Windows is reached through Windows PowerShell,
// which is present on every supported version and can use the Windows Runtime
// directly. The application itself cannot: it is built with MinGW GCC as well as
// MSVC and the official C++/WinRT headers only compile with MSVC.
const char *kPowerShell = "powershell.exe";
const char *kBridgeResource = ":/ocr/ocrbridge.ps1";
// CREATE_NO_WINDOW: a helper that reads a picture must never flash a console.
constexpr quint32 kCreateNoWindow = 0x08000000;
// Reading is driven from a menu, so a helper that hangs has to be given up on
// rather than waited for. A band of the largest size the engine accepts takes
// well under a second on a normal machine.
constexpr int kTimeoutMs = 60000;
constexpr int kLanguagesTimeoutMs = 5000;

// Empirical defaults for screenshot labels, kept together so changes can be
// compared against the small-label, faint-label and coordinate regressions.
struct OcrTuning {
    int sampleEdge = 320;        // Bound line-height sampling work on screenshots.
    int colorSampleEdge = 64;
    int inkDistance = 48;        // Ignore small background/antialias variations.
    int rowInkDivisor = 8;
    int minimumStrokeRun = 2;    // A single sampled row can be noise.
    int minimumShortEdge = 64;
    int targetTextHeight = 32;   // Comfortable glyph size for the existing engines.
    qreal maximumUpscale = 4.0;
    int minimumContrastRange = 160;
    int minimumPadding = 6;
    int sizePaddingDivisor = 10;
    int maximumSizePadding = 24;
    int maximumPadding = 48;
    qreal linePadding = 0.6;
    qreal maximumCjkGap = 0.25;  // A quarter glyph keeps ordinary word gaps apart.
};
constexpr OcrTuning kOcrTuning;

QColor ocrBackground(const QImage &image) {
    // QImage::scaled retains its storage format. Convert before reading QRgb:
    // native macOS captures are RGBA8888, and imported images can be grayscale.
    const QImage sample = image.scaled(kOcrTuning.colorSampleEdge, kOcrTuning.colorSampleEdge,
                                       Qt::KeepAspectRatio, Qt::FastTransformation)
                              .convertToFormat(QImage::Format_ARGB32);
    QHash<QRgb, int> colors;
    int opaquePixels = 0;
    bool transparent = false;
    qint64 luminance = 0, weight = 0;
    for (int y = 0; y < sample.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            transparent |= alpha < 255;
            if (alpha == 255) {
                ++opaquePixels;
                ++colors[qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel))];
            }
            luminance += qint64(qGray(pixel)) * alpha;
            weight += alpha;
        }
    }
    // A mostly transparent crop is commonly a glyph mask, not a white sheet.
    // Ignore invisible RGB values and choose the opposite of the visible ink;
    // white lettering must not disappear onto a white canvas.
    if (transparent && opaquePixels < sample.width() * sample.height() / 2)
        return weight > 0 && luminance / weight > 127 ? QColor(Qt::black) : QColor(Qt::white);
    QRgb dominant = qRgb(255, 255, 255);
    int best = 0;
    for (auto it = colors.cbegin(); it != colors.cend(); ++it)
        if (it.value() > best || (it.value() == best && it.key() < dominant)) {
            best = it.value();
            dominant = it.key();
        }
    return QColor::fromRgb(dominant);
}

QImage opaqueForOcr(const QImage &image) {
    if (image.isNull())
        return image;
    if (!image.hasAlphaChannel())
        return image.convertToFormat(QImage::Format_RGB32);
    QImage result(image.size(), QImage::Format_RGB32);
    result.fill(ocrBackground(image));
    QPainter painter(&result);
    // The target uses native pixels even if the source carries a screen DPR.
    painter.drawImage(result.rect(), image);
    painter.end();
    result.setDevicePixelRatio(image.devicePixelRatio());
    return result;
}

QString bridgeScript() {
    QFile file(QString::fromLatin1(kBridgeResource));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

// PowerShell takes -EncodedCommand as base64 of UTF-16LE without a byte order
// mark. Encoding by hand keeps this independent of the host's byte order and
// avoids having to quote a script that is full of quotes and newlines.
QString encodeForPowerShell(const QString &script) {
    QByteArray raw;
    raw.reserve(script.size() * 2);
    for (const QChar character : script) {
        const ushort value = character.unicode();
        raw.append(char(value & 0xff));
        raw.append(char((value >> 8) & 0xff));
    }
    return QString::fromLatin1(raw.toBase64());
}

// Each placeholder sits inside a single quoted PowerShell string, so a quote in a
// path - which a user name can contain - has to be doubled.
QString quoteForPowerShell(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return escaped;
}

QStringList runPowerShell(const QString &script, int timeoutMs, const QString &outputPath) {
    QProcess process;
    // The answer goes to files rather than pipes. That is one less pair of handles
    // per run and it also means the helper keeps working where pipes are scarce.
    process.setStandardOutputFile(outputPath);
    process.setStandardErrorFile(outputPath + QStringLiteral(".err"));
    // The helper reads nothing, so its standard input is forwarded instead of being
    // turned into another pipe.
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= kCreateNoWindow; });
#endif
    process.start(QString::fromLatin1(kPowerShell),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                   QStringLiteral("-EncodedCommand"), encodeForPowerShell(script)});
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitCode() != 0)
        return {};
    QFile file(outputPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString output = QString::fromLocal8Bit(file.readAll()).trimmed();
    return output.isEmpty() ? QStringList{} : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString readTextFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}
} // namespace

QString ocrLanguageTag(OcrLanguageMode mode) {
    switch (mode) {
    case OcrLanguageMode::SimplifiedChinese:
        return QStringLiteral("zh-Hans-CN");
    case OcrLanguageMode::English:
        return QStringLiteral("en-US");
    case OcrLanguageMode::System:
        return {};
    }
    return {};
}

QString ocrLanguageLabel(OcrLanguageMode mode) {
    switch (mode) {
    case OcrLanguageMode::SimplifiedChinese:
        return tr("简体中文");
    case OcrLanguageMode::English:
        return QStringLiteral("English");
    case OcrLanguageMode::System:
        return tr("跟随系统");
    }
    return {};
}

int ocrMaxImageDimension() {
    // The engine documents a maximum edge and refuses anything larger, so stay
    // below it. Nothing is lost either way: a wider picture is scaled down and a
    // taller one is read in bands, so the layout the boxes describe does not move.
    return 8000;
}

int estimateOcrTextHeight(const QImage &image) {
    if (image.isNull())
        return 0;
    // Only the rhythm of the strokes matters, so the picture is measured small.
    const int sampleEdge = kOcrTuning.sampleEdge;
    QImage sample = image;
    if (std::max(image.width(), image.height()) > sampleEdge)
        sample = image.scaled(sampleEdge, sampleEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (sample.width() < 4 || sample.height() < 4)
        return 0;
    const QImage gray = sample.convertToFormat(QImage::Format_Grayscale8);
    int histogram[256] = {0};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[row[x]];
    }
    int background = 0;
    for (int value = 1; value < 256; ++value)
        if (histogram[value] > histogram[background])
            background = value;
    // Ink is whatever clearly differs from the dominant colour: dark text on a
    // dark theme then counts exactly like dark text on white.
    const auto ink = [background](int value) {
        return std::abs(value - background) > kOcrTuning.inkDistance;
    };
    QVector<int> counts(gray.height(), 0);
    int busiest = 0;
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        int count = 0;
        for (int x = 0; x < gray.width(); ++x)
            if (ink(row[x]))
                ++count;
        counts[y] = count;
        busiest = std::max(busiest, count);
    }
    if (busiest < 2)
        return 0;
    // A row belongs to a line when it carries a noticeable share of the busiest
    // row, so sparse text in a wide crop still counts and a stray pixel does not.
    const int minimumInk = std::max(2, busiest / kOcrTuning.rowInkDivisor);
    QVector<int> runs;
    int run = 0;
    for (int y = 0; y < gray.height(); ++y) {
        if (counts[y] >= minimumInk) {
            ++run;
        } else if (run > 0) {
            if (run >= kOcrTuning.minimumStrokeRun)
                runs.append(run);
            run = 0;
        }
    }
    if (run >= kOcrTuning.minimumStrokeRun)
        runs.append(run);
    if (runs.isEmpty())
        return 0;
    // Text lines are the common case, so the median height is the line height.
    std::sort(runs.begin(), runs.end());
    const int median = runs.at(runs.size() / 2);
    return std::max(1, qRound(qreal(median) * image.height() / sample.height()));
}

QImage scaleForOcr(const QImage &image, int maxDimension) {
    if (image.isNull() || maxDimension <= 0)
        return image;
    QImage result = image;
    if (result.width() > maxDimension)
        result = result.scaledToWidth(maxDimension, Qt::SmoothTransformation);
    // Two reasons to give the recogniser more pixels: a crop can be small in
    // absolute terms (a 380 x 19 address bar), or it can be large while the text
    // inside it is small (a 500 x 120 card holding 10 px labels). The short edge
    // alone only catches the first, so the measured line height decides as well
    // and the larger of the two wins. Both stay bounded.
    const qreal shortEdgeScale =
        qreal(kOcrTuning.minimumShortEdge) / std::min(result.width(), result.height());
    const int textHeight = estimateOcrTextHeight(result);
    const qreal textScale = textHeight > 0 ? qreal(kOcrTuning.targetTextHeight) / textHeight : 1.0;
    const qreal scale = std::min({std::max({1.0, shortEdgeScale, textScale}), kOcrTuning.maximumUpscale,
                                  qreal(maxDimension) / result.width()});
    if (scale <= 1.0)
        return result;
    QImage scaled = result.scaledToWidth(qRound(result.width() * scale), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(result.devicePixelRatio());
    return scaled;
}

int ocrPaddingFor(const QSize &size) {
    if (size.isEmpty())
        return 0;
    // Recognisers drop characters that touch the border, so every crop gets a
    // margin proportional to its short edge and bounded for tiny and huge crops.
    return std::clamp(std::min(size.width(), size.height()) / kOcrTuning.sizePaddingDivisor,
                      kOcrTuning.minimumPadding, kOcrTuning.maximumSizePadding);
}

QImage stretchOcrContrast(const QImage &image) {
    if (image.isNull())
        return image;
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int histogram[256] = {0};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar *row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            ++histogram[row[x]];
    }
    // The extremes are the text itself: a one-line label covers far less than a
    // percent of the crop, so a fixed percentile could discard every glyph.
    // Keep the observed extrema; robust outlier clipping needs a labelled corpus
    // showing a recognition benefit before adding a second threshold policy.
    const qint64 edge = 1;
    int low = 0, high = 255;
    for (qint64 seen = 0, value = 0; value < 256; ++value) {
        seen += histogram[value];
        if (seen >= edge) { low = int(value); break; }
    }
    for (qint64 seen = 0, value = 255; value >= 0; --value) {
        seen += histogram[value];
        if (seen >= edge) { high = int(value); break; }
    }
    // A crop that already uses most of the range needs nothing; one that does not
    // (grey labels on a dark toolbar) is stretched, which is what lifts faint text
    // far enough for the recogniser to see it at all.
    if (high - low >= kOcrTuning.minimumContrastRange || high <= low)
        return image;
    const int span = high - low;
    QImage result = image.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < result.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const auto stretch = [&](int channel) {
                return std::clamp((channel - low) * 255 / span, 0, 255);
            };
            row[x] = qRgb(stretch(qRed(row[x])), stretch(qGreen(row[x])), stretch(qBlue(row[x])));
        }
    }
    return result;
}

QImage padForOcr(const QImage &image, int padding) {
    if (image.isNull())
        return image;
    const QImage opaque = opaqueForOcr(image);
    if (padding <= 0)
        return opaque;
    QImage padded(image.width() + padding * 2, image.height() + padding * 2, QImage::Format_RGB32);
    padded.fill(ocrBackground(opaque));
    QPainter painter(&padded);
    painter.drawImage(QRect(QPoint(padding, padding), image.size()), opaque);
    painter.end();
    padded.setDevicePixelRatio(image.devicePixelRatio());
    return padded;
}

PreparedOcrImage prepareForOcr(const QImage &image, int maxDimension) {
    if (image.isNull() || maxDimension <= 0)
        return {};
    // Flatten alpha before measuring contrast, then make faint strokes visible
    // before estimating their height. Reserve room for the largest allowed margin
    // so the final image, rather than just its content, respects the width cap.
    const int marginBudget = std::min(kOcrTuning.maximumPadding, (maxDimension - 1) / 2);
    QImage content = scaleForOcr(stretchOcrContrast(opaqueForOcr(image)),
                                 maxDimension - marginBudget * 2);
    content.setDevicePixelRatio(1);
    const int padding = std::min(marginBudget,
        std::clamp(std::max(ocrPaddingFor(content.size()),
                            qRound(estimateOcrTextHeight(content) * kOcrTuning.linePadding)),
                   kOcrTuning.minimumPadding, kOcrTuning.maximumPadding));
    return {padForOcr(content, padding), QRect(QPoint(padding, padding), content.size())};
}

QRectF PreparedOcrImage::sourceBox(const QRectF &box) const {
    if (image.isNull() || contentRect.isEmpty())
        return {};
    const QRectF pixels(box.x() * image.width() - contentRect.x(),
                        box.y() * image.height() - contentRect.y(),
                        box.width() * image.width(), box.height() * image.height());
    return QRectF(pixels.x() / contentRect.width(), pixels.y() / contentRect.height(),
                  pixels.width() / contentRect.width(), pixels.height() / contentRect.height())
        .intersected(QRectF(0, 0, 1, 1));
}

QVector<QRect> ocrBands(const QSize &size, int maxDimension, int overlap) {
    QVector<QRect> bands;
    if (size.isEmpty())
        return bands;
    if (maxDimension <= 0 || size.height() <= maxDimension) {
        bands.append(QRect(QPoint(0, 0), size));
        return bands;
    }
    // Consecutive bands share a few rows so a line of text that straddles a
    // boundary is still read in full by the band that contains all of it.
    const int step = std::max(1, maxDimension - std::max(0, overlap));
    for (int top = 0; top < size.height(); top += step) {
        const int height = std::min(maxDimension, size.height() - top);
        bands.append(QRect(0, top, size.width(), height));
        if (top + height >= size.height())
            break;
    }
    return bands;
}

namespace {
// Scripts written without spaces between words. The recogniser splits them into
// one "word" per character anyway and joins those words with spaces, which is
// what makes Chinese arrive as "前 担 心 的".
bool isCjk(const QChar character) {
    const auto code = character.unicode();
    return (code >= 0x2E80 && code <= 0x9FFF)     // radicals, punctuation, kana, CJK
           || (code >= 0xF900 && code <= 0xFAFF)  // compatibility ideographs
           || (code >= 0xFE30 && code <= 0xFE4F)  // compatibility forms
           || (code >= 0xFF01 && code <= 0xFF0F)  // fullwidth punctuation, not Latin/digits
           || (code >= 0xFF1A && code <= 0xFF20)
           || (code >= 0xFF3B && code <= 0xFF40)
           || (code >= 0xFF5B && code <= 0xFF65);
}
} // namespace

QString joinOcrWords(const QVector<OcrLine> &words) {
    QString result;
    const OcrLine *previous = nullptr;
    for (const auto &word : words) {
        if (word.text.isEmpty())
            continue;
        if (previous != nullptr) {
            const qreal height = std::max(previous->box.height(), word.box.height());
            const qreal overlap = std::min(previous->box.bottom(), word.box.bottom()) -
                                  std::max(previous->box.top(), word.box.top());
            const qreal gap = word.box.left() - previous->box.right();
            const bool adjacentCjk = isCjk(previous->text.back()) && isCjk(word.text.front()) &&
                height > 0 && overlap >= std::min(previous->box.height(), word.box.height()) / 2 &&
                gap >= -height / 2 && gap <= height * kOcrTuning.maximumCjkGap;
            if (!adjacentCjk)
                result += QLatin1Char(' ');
        }
        result += word.text;
        previous = &word;
    }
    return result;
}

QRectF ocrBandBoxToImage(const QRectF &box, const QRect &band, const QSize &size) {
    if (size.isEmpty())
        return {};
    const QRectF placed(box.x() + band.x(), box.y() + band.y(), box.width(), box.height());
    return QRectF(placed.x() / size.width(), placed.y() / size.height(),
                  placed.width() / size.width(), placed.height() / size.height());
}

bool parseOcrPayload(const QByteArray &payload, const QRect &band, const QSize &size, QString *language,
                     QVector<OcrLine> *lines, QString *error) {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (size.isEmpty())
        return fail(QStringLiteral("no image size to normalise against"));
    // The outputs describe this payload alone; the caller merges the bands, which
    // keeps a failed band from leaving half of an earlier answer behind.
    if (language)
        language->clear();
    if (lines)
        lines->clear();
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("payload is not a JSON object: ") + parseError.errorString());
    const auto object = document.object();
    // The bridge answers with an error object instead of lines when it could not
    // run, so the reason it gives is what the log should carry.
    if (object.contains(QStringLiteral("error")))
        return fail(QStringLiteral("the bridge reported: ") +
                    object.value(QStringLiteral("error")).toString());
    // A band of blank paper still answers with the language, which is what the
    // result window shows, so the tag is read before the lines are.
    if (language && object.contains(QStringLiteral("engineLanguage")))
        *language = object.value(QStringLiteral("engineLanguage")).toString();
    if (!object.contains(QStringLiteral("lines")) || !object.value(QStringLiteral("lines")).isArray())
        return fail(QStringLiteral("payload has no lines array"));
    if (lines) {
        for (const auto &entry : object.value(QStringLiteral("lines")).toArray()) {
            const auto item = entry.toObject();
            auto text = item.value(QStringLiteral("text")).toString();
            if (item.contains(QStringLiteral("words"))) {
                if (!item.value(QStringLiteral("words")).isArray())
                    return fail(QStringLiteral("line words is not an array"));
                QVector<OcrLine> words;
                for (const auto &value : item.value(QStringLiteral("words")).toArray()) {
                    const auto word = value.toObject();
                    words.append({word.value(QStringLiteral("text")).toString(),
                                  QRectF(word.value(QStringLiteral("x")).toDouble(),
                                         word.value(QStringLiteral("y")).toDouble(),
                                         word.value(QStringLiteral("w")).toDouble(),
                                         word.value(QStringLiteral("h")).toDouble())});
                }
                text = joinOcrWords(words);
            }
            if (text.isEmpty())
                continue;
            const QRectF box(item.value(QStringLiteral("x")).toDouble(),
                             item.value(QStringLiteral("y")).toDouble(),
                             item.value(QStringLiteral("w")).toDouble(),
                             item.value(QStringLiteral("h")).toDouble());
            // Old payloads without words and Vision's text keep their real spaces.
            lines->append({text, ocrBandBoxToImage(box, band, size)});
        }
    }
    if (error)
        error->clear();
    return true;
}

QString prepareOcrBridge(const QString &script, const QString &image, const QString &output,
                         const QString &language) {
    // The Windows Runtime file APIs take native paths only; a forward slash makes
    // GetFileFromPathAsync fail with "the given path's format is not supported".
    QString prepared = script;
    prepared.replace(QStringLiteral("__EDITHERE_IMAGE__"), quoteForPowerShell(QDir::toNativeSeparators(image)));
    prepared.replace(QStringLiteral("__EDITHERE_OUTPUT__"), quoteForPowerShell(QDir::toNativeSeparators(output)));
    prepared.replace(QStringLiteral("__EDITHERE_LANGUAGE__"), quoteForPowerShell(language));
    return prepared;
}

QString OcrResult::text() const {
    QStringList parts;
    parts.reserve(lines.size());
    for (const auto &line : lines)
        parts.append(line.text);
    return parts.join(QLatin1Char('\n'));
}

OcrEngine::OcrEngine(QObject *parent) : QObject(parent) {}

OcrEngine::~OcrEngine() {
    cancel();
}

bool OcrEngine::supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_APPLE)
    return true;
#elif defined(Q_OS_LINUX)
    return !QStandardPaths::findExecutable("tesseract").isEmpty();
#else
    return false;
#endif
}

QStringList OcrEngine::availableLanguages() {
#ifdef Q_OS_WIN
    if (!supported())
        return {};
    const QDir scratch(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                           .filePath(QStringLiteral("EditHere-ocr-languages")));
    if (!QDir().mkpath(scratch.path()))
        return {};
    // The property is a static one, so the type has to be named before it can be
    // read; the tags are ASCII, which keeps the console encoding out of the way.
    return runPowerShell(
        QStringLiteral("$t=[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = "
                       "WindowsRuntime];[Console]::Out.Write((($t::AvailableRecognizerLanguages | "
                       "ForEach-Object { $_.LanguageTag }) -join \"`n\"))"),
        kLanguagesTimeoutMs, scratch.filePath(QStringLiteral("languages.txt")));
#elif defined(Q_OS_APPLE)
    return visionLanguageTags();
#elif defined(Q_OS_LINUX)
    QProcess process;
    process.start(QStandardPaths::findExecutable("tesseract"), {"--list-langs"});
    if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(1000); return {}; }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    QStringList languages;
    const auto rows = QString::fromUtf8(process.readAllStandardOutput()).split('\n');
    for (const auto &row : rows) {
        const auto value = row.trimmed();
        if (!value.isEmpty() && !value.startsWith("List of available languages")) languages.append(value);
    }
    return languages;
#else
    return {};
#endif
}

bool OcrEngine::busy() const {
    if (process_ != nullptr)
        return true;
#ifdef Q_OS_APPLE
    if (pending_ || (watcher_ != nullptr && !watcherAbandoned_))
        return true;
#endif
    return false;
}

void OcrEngine::cancel() {
    callback_ = nullptr;
    if (process_ != nullptr) {
        QProcess *process = process_;
        process_ = nullptr;
        process->disconnect(this);
        if (process->state() == QProcess::Starting)
            process->waitForStarted(1000);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
    }
#ifdef Q_OS_APPLE
    pending_.reset();
    // A basic QtConcurrent run does not interrupt synchronous Vision work.
    // Discard its answer, but retain the watcher so a replacement cannot start
    // another recogniser before this one actually exits.
    if (watcher_ != nullptr)
        watcherAbandoned_ = true;
#endif
    delete scratch_;
    scratch_ = nullptr;
    bands_.clear();
    bandPaths_.clear();
    prepared_ = {};
    bandIndex_ = 0;
}

OcrResult OcrEngine::failure(OcrFailure kind, const QString &message) const {
    OcrResult result;
    result.ok = false;
    result.failure = kind;
    result.message = message;
    return result;
}

void OcrEngine::recognize(const QImage &image, OcrLanguageMode language, OcrCallback callback) {
    // Asking twice - a second press of the button while the first one runs - means
    // the first answer is no longer wanted.
    cancel();
    if (image.isNull()) {
        if (callback)
            callback(failure(OcrFailure::Failed, tr("没有可以识别的图像。")));
        return;
    }
    if (!supported()) {
        if (callback)
            callback(failure(OcrFailure::Unsupported, tr("当前平台不支持文字识别。")));
        return;
    }
#ifdef Q_OS_APPLE
    if (watcher_ != nullptr) {
        pending_ = PendingRequest{image, language, std::move(callback)};
        return;
    }
#endif
    begin(image, language, std::move(callback));
}

void OcrEngine::begin(const QImage &image, OcrLanguageMode language, OcrCallback callback) {
    callback_ = std::move(callback);
    lines_.clear();
    language_.clear();
    preferred_ = ocrLanguageTag(language);
    request_ = language;
    prepared_ = prepareForOcr(image, ocrMaxImageDimension());
#ifdef Q_OS_WIN
    const QString script = bridgeScript();
    if (script.isEmpty()) {
        finish(failure(OcrFailure::Unavailable, tr("安装包缺少文字识别组件，请重新安装 EditHere。")));
        return;
    }
    scratch_ = new QTemporaryDir(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                     .filePath(QStringLiteral("EditHere-ocr-XXXXXX")));
    if (scratch_ == nullptr || !scratch_->isValid()) {
        finish(failure(OcrFailure::Unavailable, tr("无法创建临时目录，请检查磁盘空间与访问权限。")));
        return;
    }
    bands_ = ocrBands(prepared_.image.size(), ocrMaxImageDimension());
    bandPaths_.clear();
    for (int index = 0; index < bands_.size(); ++index) {
        const QString path = scratch_->filePath(QStringLiteral("band-%1.png").arg(index));
        if (!prepared_.image.copy(bands_.at(index)).save(path, "PNG")) {
            finish(failure(OcrFailure::Unavailable, tr("无法写入临时图片，请检查磁盘空间与访问权限。")));
            return;
        }
        bandPaths_.append(path);
    }
    bandIndex_ = 0;
    startNextBand();
#elif defined(Q_OS_APPLE)
    auto *watcher = new QFutureWatcher<VisionOutcome>(this);
    watcher_ = watcher;
    watcherAbandoned_ = false;
    connect(watcher, &QFutureWatcher<VisionOutcome>::finished, this, [this, watcher] {
        if (watcher_ != watcher)
            return;
        if (watcherAbandoned_) {
            watcher_ = nullptr;
            watcherAbandoned_ = false;
            watcher->deleteLater();
            auto pending = std::move(pending_);
            pending_.reset();
            if (pending)
                begin(pending->image, pending->language, std::move(pending->callback));
            return;
        }
        const VisionOutcome outcome = watcher->result();
        OcrResult result;
        if (outcome.ok) {
            result.ok = true;
            result.engineLanguage = outcome.language;
            result.lines = outcome.lines;
            finish(result);
            return;
        }
        qWarning("ocr: Vision failed: %s", qUtf8Printable(outcome.diagnostic));
        // The recogniser is installed per language as an optional feature, so a
        // Chinese system without the Chinese pack is a normal thing to hit.
        if (outcome.noEngine)
            result = failure(OcrFailure::NoEngine,
                             request_ == OcrLanguageMode::System
                                 ? tr("系统没有安装文字识别语言包。请在「系统设置 → 通用 → 语言与地区」中"
                                      "添加语言后重试。")
                                 : tr("系统没有安装「%1」的文字识别语言包。请在「系统设置 → 通用 → 语言"
                                      "与地区」中添加该语言后重试。")
                                       .arg(ocrLanguageLabel(request_)));
        else
            result = failure(OcrFailure::Failed, tr("文字识别失败，请重试。"));
        finish(result);
    });
    // Vision reads a request synchronously, so it runs on a worker thread; only the
    // finished outcome crosses back, and the wording it is turned into is built on
    // the thread that owns the translation.
    watcher->setFuture(QtConcurrent::run([image = prepared_.image, preferred = preferred_ ] {
        return visionRecognize(image, preferred);
    }));
#elif defined(Q_OS_LINUX)
    const auto available = availableLanguages();
    QString desired = preferred_.startsWith("zh-Hant") ? "chi_tra" : preferred_.startsWith("zh") ? "chi_sim" : "eng";
    if (request_ == OcrLanguageMode::System) {
        desired = QLocale().language() == QLocale::Chinese ? "chi_sim" : "eng";
        if (!available.contains(desired) && !available.isEmpty()) desired = available.first();
    }
    if (!available.contains(desired)) {
        finish(failure(OcrFailure::NoEngine, tr("未安装所需的 Tesseract 语言包：%1").arg(desired))); return;
    }
    preferred_ = desired;
    language_ = desired;
    scratch_ = new QTemporaryDir;
    if (!scratch_->isValid()) { finish(failure(OcrFailure::Unavailable, tr("无法创建文字识别临时目录。"))); return; }
    bands_ = ocrBands(prepared_.image.size(), ocrMaxImageDimension());
    for (int i = 0; i < bands_.size(); ++i) {
        const auto path = scratch_->filePath(QString("band-%1.png").arg(i));
        if (!prepared_.image.copy(bands_[i]).save(path)) { finish(failure(OcrFailure::Unavailable, tr("无法写入待识别图片。"))); return; }
        bandPaths_.append(path);
    }
    startNextBand();
#else
    Q_UNUSED(language);
    finish(failure(OcrFailure::Unsupported, tr("当前平台不支持文字识别。")));
#endif
}

void OcrEngine::startNextBand() {
    if (bandIndex_ >= bandPaths_.size()) {
        if (lines_.isEmpty())
            qWarning("ocr: no text found in %dx%d using \"%s\" for %d", prepared_.image.width(), prepared_.image.height(),
                     qUtf8Printable(language_), int(request_));
        OcrResult result;
        result.ok = true;
        result.engineLanguage = language_;
        result.lines = lines_;
        finish(std::move(result));
        return;
    }
#ifdef Q_OS_WIN
    QString script = bridgeScript();
    if (script.isEmpty()) {
        finish(failure(OcrFailure::Unavailable, tr("安装包缺少文字识别组件，请重新安装 EditHere。")));
        return;
    }
    const QString output = scratch_->filePath(QStringLiteral("result-%1.json").arg(bandIndex_));
    script = prepareOcrBridge(script, bandPaths_.at(bandIndex_), output, preferred_);
    QProcess *process = new QProcess(this);
    process_ = process;
    // The bridge writes its answer to a file, so neither stream needs a pipe: what
    // the helper prints is kept beside it purely as a diagnostic.
    const QString logs = scratch_->filePath(QStringLiteral("band-%1").arg(bandIndex_));
    process->setStandardOutputFile(logs + QStringLiteral(".out"));
    process->setStandardErrorFile(logs + QStringLiteral(".err"));
    // The bridge reads nothing from the application, so there is no reason to give
    // it a pipe for its standard input.
    process->setInputChannelMode(QProcess::ForwardedInputChannel);
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= kCreateNoWindow; });
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setInterval(kTimeoutMs);
    connect(timeout, &QTimer::timeout, this, [this, process] {
        if (process_ == process)
            finish(failure(OcrFailure::Failed, tr("文字识别超时，请重试。")));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process_ == process && error == QProcess::FailedToStart)
            finish(failure(OcrFailure::Unavailable, tr("文字识别失败，请重试。")));
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, output, logs](int code, QProcess::ExitStatus status) {
                if (process_ != process)
                    return;
                process_ = nullptr;
                process->disconnect(this);
                process->deleteLater();
                const QString payload = readTextFile(output);
                if (code != 0 || status != QProcess::NormalExit) {
                    // A failed run explains itself in the output file, which is
                    // easier to read than the error stream's CLIXML.
                    reportFailure(code == 0 ? 1 : code, false,
                                  payload.isEmpty() ? readTextFile(logs + QStringLiteral(".err"))
                                                    : payload);
                    return;
                }
                collectBand(payload, readTextFile(logs + QStringLiteral(".err")));
            });
    timeout->start();
    process->start(QString::fromLatin1(kPowerShell),
                   {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                    QStringLiteral("-EncodedCommand"), encodeForPowerShell(script)});
#elif defined(Q_OS_LINUX)
    const auto output = scratch_->filePath(QString("result-%1.tsv").arg(bandIndex_));
    auto *process = new QProcess(this);
    process_ = process;
    process->setStandardOutputFile(output);
    const auto diagnostic = scratch_->filePath("diagnostic.txt");
    process->setStandardErrorFile(diagnostic);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, this, [this, process] {
        if (process_ == process) finish(failure(OcrFailure::Failed, tr("文字识别超时，请重试。")));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process_ == process && error == QProcess::FailedToStart)
            finish(failure(OcrFailure::Unavailable, tr("无法启动 Tesseract。")));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, output](int code, QProcess::ExitStatus status) {
        if (process_ != process) return;
        if (code != 0 || status != QProcess::NormalExit) { finish(failure(OcrFailure::Failed, tr("文字识别失败，请检查 Tesseract 语言包。"))); return; }
        QFile file(output);
        QVector<OcrLine> lines;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024 ||
            !parseTesseractTsv(file.readAll(), bands_[bandIndex_], prepared_.image.size(), &lines)) {
            finish(failure(OcrFailure::Failed, tr("无法读取文字识别结果，请重试。"))); return;
        }
        lines_ += lines;
        process_ = nullptr;
        process->deleteLater();
        ++bandIndex_;
        startNextBand();
    });
    process->start(QStandardPaths::findExecutable("tesseract"),
                   {bandPaths_[bandIndex_], "stdout", "-l", preferred_, "tsv"});
    timer->start();
#endif
}

void OcrEngine::collectBand(const QString &payload, const QString &diagnostic) {
    Q_UNUSED(diagnostic);
    QString language;
    QVector<OcrLine> lines;
    QString error;
    if (!parseOcrPayload(payload.toUtf8(), bands_.value(bandIndex_), prepared_.image.size(), &language, &lines,
                         &error)) {
        qWarning("ocr: unusable result from the bridge: %s", qUtf8Printable(error));
        finish(failure(OcrFailure::Failed, tr("无法读取文字识别结果，请重试。")));
        return;
    }
    if (language_.isEmpty())
        language_ = language;
    lines_ += lines;
    ++bandIndex_;
    startNextBand();
}

void OcrEngine::reportFailure(int exitCode, bool timedOut, const QString &diagnostic) {
    if (timedOut) {
        finish(failure(OcrFailure::Failed, tr("文字识别超时，请重试。")));
        return;
    }
    if (exitCode == 2) {
        finish(failure(OcrFailure::Unavailable, tr("无法读取待识别的图片。")));
        return;
    }
    if (exitCode == 3) {
        // The recogniser is installed per language as an optional feature, so a
        // Chinese system without the Chinese pack is a normal thing to hit.
        if (request_ == OcrLanguageMode::System)
            finish(failure(OcrFailure::NoEngine,
                           tr("系统没有安装文字识别语言包。请在「设置 → 时间和语言 → 语言和区域」中为需要"
                              "的语言添加「光学字符识别」可选功能。")));
        else
            finish(failure(OcrFailure::NoEngine,
                           tr("系统没有安装「%1」的文字识别语言包。请在「设置 → 时间和语言 → 语言和"
                              "区域」中为该语言添加「光学字符识别」可选功能。")
                               .arg(ocrLanguageLabel(request_))));
        return;
    }
    qWarning("ocr: the bridge failed with %d: %s", exitCode, qUtf8Printable(diagnostic));
    finish(failure(OcrFailure::Failed, tr("文字识别失败，请重试。")));
}

void OcrEngine::finish(OcrResult result) {
    // All engines describe the prepared image. Correct their boxes here once so
    // adding a platform never requires remembering a separate padding argument.
    if (result.ok)
        for (auto &line : result.lines)
            line.box = prepared_.sourceBox(line.box);
    OcrCallback callback = callback_;
    callback_ = nullptr;
    if (process_ != nullptr) {
        QProcess *process = process_;
        process_ = nullptr;
        process->disconnect(this);
        if (process->state() == QProcess::Starting)
            process->waitForStarted(1000);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
    }
#ifdef Q_OS_APPLE
    if (watcher_ != nullptr) {
        QFutureWatcher<VisionOutcome> *watcher = watcher_;
        watcher_ = nullptr;
        watcher->disconnect(this);
        watcher->deleteLater();
    }
#endif
    delete scratch_;
    scratch_ = nullptr;
    bands_.clear();
    bandPaths_.clear();
    prepared_ = {};
    // Whichever path produced it, the language the recogniser answered in is what the
    // next call reports. Vision hands it back in the outcome and the Windows bridge in
    // every band, so it is taken from the finished result rather than from either one.
    if (result.ok && !result.engineLanguage.isEmpty())
        language_ = result.engineLanguage;
    if (callback)
        callback(std::move(result));
}
} // namespace h2d
