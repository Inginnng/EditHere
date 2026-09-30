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

QImage scaleForOcr(const QImage &image, int maxDimension) {
    if (image.isNull() || maxDimension <= 0 || image.width() <= maxDimension)
        return image;
    return image.scaledToWidth(maxDimension, Qt::SmoothTransformation);
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

QRectF ocrBandBoxToImage(const QRectF &box, const QRect &band, const QSize &size) {
    if (size.isEmpty())
        return {};
    const QRectF placed(box.x() + band.x(), box.y() + band.y(), box.width(), box.height());
    return QRectF(placed.x() / size.width(), placed.y() / size.height(), placed.width() / size.width(),
                  placed.height() / size.height());
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
            const auto text = item.value(QStringLiteral("text")).toString();
            if (text.isEmpty())
                continue;
            const QRectF box(item.value(QStringLiteral("x")).toDouble(),
                             item.value(QStringLiteral("y")).toDouble(),
                             item.value(QStringLiteral("w")).toDouble(),
                             item.value(QStringLiteral("h")).toDouble());
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
    if (process_ != nullptr) {
        process_->disconnect(this);
        process_->kill();
        process_ = nullptr;
    }
    delete scratch_;
    scratch_ = nullptr;
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
    if (watcher_ != nullptr)
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
        process->kill();
        process->deleteLater();
    }
#ifdef Q_OS_APPLE
    if (watcher_ != nullptr) {
        QFutureWatcher<VisionOutcome> *watcher = watcher_;
        watcher_ = nullptr;
        watcher->disconnect(this);
        watcher->cancel();
        watcher->deleteLater();
    }
#endif
    delete scratch_;
    scratch_ = nullptr;
    bands_.clear();
    bandPaths_.clear();
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
    begin(image, language, std::move(callback));
}

void OcrEngine::begin(const QImage &image, OcrLanguageMode language, OcrCallback callback) {
    callback_ = std::move(callback);
    lines_.clear();
    language_.clear();
    preferred_ = ocrLanguageTag(language);
    request_ = language;
    sent_ = scaleForOcr(image, ocrMaxImageDimension());
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
    bands_ = ocrBands(sent_.size(), ocrMaxImageDimension());
    bandPaths_.clear();
    for (int index = 0; index < bands_.size(); ++index) {
        const QString path = scratch_->filePath(QStringLiteral("band-%1.png").arg(index));
        if (!sent_.copy(bands_.at(index)).save(path, "PNG")) {
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
    connect(watcher, &QFutureWatcher<VisionOutcome>::finished, this, [this, watcher] {
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
    watcher->setFuture(QtConcurrent::run([image = sent_, preferred = preferred_ ] {
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
    bands_ = ocrBands(sent_.size(), ocrMaxImageDimension());
    for (int i = 0; i < bands_.size(); ++i) {
        const auto path = scratch_->filePath(QString("band-%1.png").arg(i));
        if (!sent_.copy(bands_[i]).save(path)) { finish(failure(OcrFailure::Unavailable, tr("无法写入待识别图片。"))); return; }
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
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, output, logs](int code, QProcess::ExitStatus status) {
                const QString payload = readTextFile(output);
                if (code != 0) {
                    // A failed run explains itself in the output file, which is
                    // easier to read than the error stream's CLIXML.
                    reportFailure(code, status == QProcess::CrashExit,
                                  payload.isEmpty() ? readTextFile(logs + QStringLiteral(".err"))
                                                    : payload);
                    return;
                }
                collectBand(payload, readTextFile(logs + QStringLiteral(".err")));
            });
    process->start(QString::fromLatin1(kPowerShell),
                   {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                    QStringLiteral("-EncodedCommand"), encodeForPowerShell(script)});
    timeout->start();
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
            !parseTesseractTsv(file.readAll(), bands_[bandIndex_], sent_.size(), &lines)) {
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
    if (!parseOcrPayload(payload.toUtf8(), bands_.value(bandIndex_), sent_.size(), &language, &lines,
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
    OcrCallback callback = callback_;
    callback_ = nullptr;
    if (process_ != nullptr) {
        QProcess *process = process_;
        process_ = nullptr;
        process->disconnect(this);
        process->kill();
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
    // Whichever path produced it, the language the recogniser answered in is what the
    // next call reports. Vision hands it back in the outcome and the Windows bridge in
    // every band, so it is taken from the finished result rather than from either one.
    if (result.ok && !result.engineLanguage.isEmpty())
        language_ = result.engineLanguage;
    if (callback)
        callback(std::move(result));
}
} // namespace h2d
