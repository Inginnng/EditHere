#pragma once
#include "settings.h"
#include <QImage>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#ifdef Q_OS_APPLE
#include <QFutureWatcher>
#endif
class QProcess;
class QTemporaryDir;
namespace h2d {
// One recognised line of text. The box is relative to the recognised image: the
// four values are fractions of the image size with the origin in the top-left
// corner, so the same result can be drawn over any scaled copy of the picture.
struct OcrLine {
    QString text;
    QRectF box;
};
enum class OcrFailure {
    None,
    Unsupported, // This system has no recogniser this build can drive.
    NoEngine,    // No recogniser is installed for the language that was asked for.
    Unavailable, // The recogniser could not be started at all.
    Failed,      // The recogniser ran but reported an error.
};
struct OcrResult {
    bool ok = false;
    OcrFailure failure = OcrFailure::None;
    QString engineLanguage; // Language tag the recogniser used, e.g. "zh-Hans-CN".
    QVector<OcrLine> lines;
    QString message; // Ready to show; already translated.
    // The recognised lines joined by newlines, which is what the clipboard wants.
    QString text() const;
};
using OcrCallback = std::function<void(OcrResult)>;

// Language tag for a choice. Empty for "follow the system", which is what the
// recogniser wants to mean "pick the best installed language yourself".
QString ocrLanguageTag(OcrLanguageMode mode);

// Name of a choice as shown in a menu, written in the current interface language.
QString ocrLanguageLabel(OcrLanguageMode mode);

// The Windows recogniser refuses images larger than a documented maximum, so the
// engine stays below it and splits anything taller. Exposed for tests.
int ocrMaxImageDimension();

// Shrinks an image whose width is beyond what the recogniser accepts, keeping the
// aspect ratio. Because the scaling is uniform, the boxes reported for the small
// copy remain valid fractions of the original picture.
QImage scaleForOcr(const QImage &image, int maxDimension);

// Splits an image taller than the recogniser accepts into overlapping bands, so a
// line of text on a boundary is still read in full by one of them. The bands are
// in order from the top and together cover the whole image. When the image fits,
// a single band covering all of it is returned.
QVector<QRect> ocrBands(const QSize &size, int maxDimension, int overlap = 24);

// Moves a box reported for one band onto the whole image and turns it into
// fractions of the image size.
QRectF ocrBandBoxToImage(const QRectF &box, const QRect &band, const QSize &size);

// Reads the payload the Windows bridge writes:
//   {"engineLanguage":"<tag>","lines":[{"text":"..","x":0,"y":0,"w":0,"h":0}]}
// where a box is in pixels of the band that was read. When the bridge could not
// run at all it writes {"error":".."} instead. Both outputs are replaced, never
// appended to, so a caller can merge the bands itself. Exposed so the format can
// be covered by a test without a recogniser being installed.
bool parseOcrPayload(const QByteArray &payload, const QRect &band, const QSize &size,
                     QString *language, QVector<OcrLine> *lines, QString *error);
bool parseTesseractTsv(const QByteArray &payload, const QRect &band, const QSize &size,
                      QVector<OcrLine> *lines);

// Fills in the placeholders of the Windows bridge script. Two details matter and
// neither is visible from the caller: the paths have to reach the script as native
// Windows paths, because the file APIs of the Windows Runtime reject the forward
// slashes Qt works with, and they have to be safe inside a single quoted
// PowerShell string, because a user name can contain a quote.
QString prepareOcrBridge(const QString &script, const QString &image, const QString &output,
                         const QString &language);

#ifdef Q_OS_APPLE
// Implemented in ocr_mac.mm with the Vision framework. Vision runs its request
// synchronously, so the engine calls this from a worker thread.
struct VisionOutcome {
    bool ok = false;
    bool noEngine = false;
    QString language;
    QVector<OcrLine> lines;
    QString diagnostic; // Technical detail for the log, never shown on its own.
};
VisionOutcome visionRecognize(const QImage &image, const QString &preferred);
QStringList visionLanguageTags();
#endif

// Reads the text in an image. The work happens in a helper process or on a worker
// thread, so the call returns immediately; the callback runs once, on the thread
// that started the work. Calling it again cancels whatever is still running.
class OcrEngine final : public QObject {
    Q_OBJECT
  public:
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;
    // True when this build can read text on this system at all.
    static bool supported();
    // Language tags the installed recogniser offers. Empty when it is missing or
    // cannot be queried. On Windows this starts a helper process, so it is not
    // something to call for every repaint.
    static QStringList availableLanguages();
    void recognize(const QImage &image, OcrLanguageMode language, OcrCallback callback);
    bool busy() const;
    void cancel();
    // Language tag of the last successful run, empty before the first one.
    QString engineLanguage() const {
        return language_;
    }

  private:
    OcrResult failure(OcrFailure kind, const QString &message) const;
    void begin(const QImage &image, OcrLanguageMode language, OcrCallback callback);
    void finish(OcrResult result);
    // Starts the recogniser for the next band, or finishes when there is none left.
    void startNextBand();
    // Folds one band of results into the running total and moves on.
    void collectBand(const QString &payload, const QString &diagnostic);
    void reportFailure(int exitCode, bool timedOut, const QString &diagnostic);
    OcrCallback callback_;
    QImage sent_; // Exactly the picture that was handed to the recogniser.
    QVector<QRect> bands_;
    QVector<QString> bandPaths_;
    QVector<OcrLine> lines_;
    QString preferred_, language_;
    OcrLanguageMode request_ = OcrLanguageMode::System;
    int bandIndex_ = 0;
    QProcess *process_ = nullptr;
#ifdef Q_OS_APPLE
    QFutureWatcher<VisionOutcome> *watcher_ = nullptr;
#endif
    QTemporaryDir *scratch_ = nullptr;
};
} // namespace h2d
