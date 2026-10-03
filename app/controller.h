#pragma once
#include "capturesession.h"
#include "editor.h"
#include "ocr.h"
#include "overlay.h"
#include "settings.h"
#include <QPointer>
#include <QSystemTrayIcon>
#include <QElapsedTimer>
namespace h2d {
class OcrDialog;
class PinWindow;
class ScrollCapture;
class ScrollCaptureProgress;
class ScrollCaptureRegion;
class ScrollCaptureShade;
class Controller final : public QObject {
    Q_OBJECT
  public:
    explicit Controller(QObject *parent = nullptr, const AppSettings &settings = loadSettings(),
                        const QString &settingsFile = {});
    void start(bool demo, const QString &path = {}, bool background = false, bool firstUse = false);
    void showGuide();
    void capture();
    void activate();
    void quit();
    void openSettings(bool updates = false, bool toolbar = false);
    QJsonObject handleAgentRequest(const QJsonObject &request);
    void cancelAgentSession(const QString &id, const QString &code, const QString &message);
  signals:
    void agentSessionFinished(const QString &id, const QJsonObject &result);

  private:
    friend class ScrollControllerTests;
    void beginCapture(bool fromTray);
    void restoreAfterCapture();
    quintptr captureForeground_ = 0;
    void finishAgentSession();
    // Rebuilds the tray menu and tooltip; called on startup and on a language change.
    void retranslate();
    void clearOverlays();
    void cancelCapture();
    void raiseEditor();
    void completeCapture(Overlay *source, QRect pixels, QVector<Candidate> candidates);
    void updateTrayShortcut();
    // What the toolbar beside a finished region can do with it.
    void copyRegion(Overlay *source);
    void pinRegion(Overlay *source);
    void saveRegion(Overlay *source);
    void recognizeRegion(Overlay *source, OcrLanguageMode language);
    void startScrollCapture(Overlay *source);
    void beginScrollSettle();
    void armScrollTimeout(quint64 generation, quint64 round);
    void stepScrollCapture();
    void setAutoScrollCapture(bool automatic);
    void readScrollFrame();
    void placeScrollFrame(const QImage &frame);
    void showScrollProgress();
    void pauseScrollCapture(const QString &message, bool allowEmpty = false);
    void stopScrollCapture();
    void resumeScrollCapture();
    void changeScrollDirection();
    void changeScrollRegion(const QRect &logicalRegion);
    QImage scrollResult() const;
    void exportScrollCapture(int action);
    void finishScrollCapture();
    void abortScrollCapture(const QString &message = {});
    void discardScrollRun();
    struct ScrollRun {
        QRect nativeRegion;
        QRect logicalRegion;
        ScrollCaptureTarget target;
        QImage held;
        QElapsedTimer settleDeadline;
        quint64 settleRound = 0;
        int unchanged = 0;
        bool initial = true;
        bool paused = false;
        bool automatic = false;
        bool mismatched = false;
        Qt::Orientation axis = Qt::Vertical;
        QString notice;
    };
    ScrollRun scrollRun_;
    quint64 scrollGeneration_ = 0;
    QPointer<Overlay> scrollSource_;
    QPointer<ScrollCaptureProgress> scrollProgress_;
    QPointer<ScrollCaptureRegion> scrollRegion_;
    QPointer<ScrollCaptureShade> scrollShade_;
    ScrollCapture *scroller_ = nullptr;
    // The same asynchronous paths are exercised with deterministic frames by the
    // lifecycle tests, including callbacks arriving after cancellation.
    struct ScrollIo {
        std::function<void(QObject *, std::function<void()>)> prepare =
            [](QObject *context, std::function<void()> ready) {
                prepareScreenCapture(context, std::move(ready));
            };
        std::function<bool(QString *)> supported = supportsScrollingCapture;
        std::function<ScrollCaptureTarget(QPoint)> targetAt = scrollCaptureTargetAt;
        std::function<void(quintptr)> focus = restoreCaptureForegroundWindow;
        std::function<bool(const ScrollCaptureTarget &, QPoint, int, QString *)> step = scrollCaptureStep;
        std::function<void(const QRect &, std::function<void(QImage, QString)>)> grab = captureScrollRegion;
        int settleTimeoutMs = 2200;
    } scrollIo_;
    // The screen is covered while a region is being picked, so anything that opens a
    // window of its own has to take the covers off first and put them back if the user
    // changes their mind.
    void uncoverForDialog();
    void recoverAfterDialog();
    // Returns the window it made, so the caller can say how much of the picture is
    // shadow, which is the part the editor must not be handed.
    PinWindow *pinImage(const QImage &image, QRect placement = {});
    bool saveImage(const QImage &image);
    void recognize(const QImage &image, OcrLanguageMode language);
    void runRecognition(OcrLanguageMode language);
    void remember(const QImage &image, const QRect &selection);
    // Hands an overlay the list of earlier captures and selections that its menu offers,
    // and opens the one the user picks.
    void applyCaptureHistory(Overlay *overlay);
    void openHistory(int index);
    // Moves through the earlier captures while the capture window is still up. Step
    // backwards is towards older pictures, forwards is back towards the screen.
    void stepHistory(int delta);
    // The style every later capture should start from, as the user asked for it to be
    // remembered. Written out on the spot, because a preference that is lost on exit is
    // not a preference.
    void rememberCaptureStyle(const CaptureStyle &style);
    AppSettings settings_;
    QString settingsFile_;
    bool guidePending_ = false;
    QAction *captureAction_ = nullptr;
    QAction *openAction_ = nullptr;
    // Opens the annotation window with nothing in it, which is how a picture that is
    // already on disk gets annotated: drop it on the window rather than screenshot it
    // again.
    QAction *annotateAction_ = nullptr;
    QAction *accessibilityAction_ = nullptr;
    QAction *settingsAction_ = nullptr, *updatesAction_ = nullptr, *quitAction_ = nullptr;
    Editor editor_;
    QSystemTrayIcon tray_;
    GlobalShortcut shortcut_;
    QVector<Overlay *> overlays_;
    QString agentSessionId_, agentOutput_;
    bool agentEmbed_ = true;
    bool startupUpdateChecked_ = false;
    bool capturing_ = false, wasVisible_ = false;
    CaptureHistory history_;
    // Which earlier capture the capture windows are showing, or -1 for the screen.
    int historyIndex_ = -1;
    // Pinned pictures are owned by nobody but this list: a pin outlives the region it
    // came from, which is the whole point of pinning it.
    QVector<PinWindow *> pins_;
    OcrEngine *ocr_ = nullptr;
    QPointer<OcrDialog> ocrDialog_;
    QImage ocrPicture_;
};
} // namespace h2d
