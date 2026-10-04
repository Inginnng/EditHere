#pragma once
#include "model.h"
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <functional>
class QWidget;
namespace h2d {
struct ScreenFrame {
    QString name;
    QRect logicalGeometry;
    QRect nativeGeometry;
    QImage image;
    bool nativePixels = false;
    bool windowScopeAvailable = false;
    QVector<Candidate> frontWindows; // Front-to-back snapshot, in image pixels.
    // Accessibility APIs can use logical screen coordinates while capture uses
    // native pixels. Empty means use nativeGeometry, as on Windows and X11.
    QRect elementGeometry;
    bool elementProbingAllowed = true;
};
using CaptureCallback = std::function<void(QVector<ScreenFrame>, QString)>;
// A scrolling session keeps the original window and its owner, rather than
// retargeting the next wheel event if another application appears in the region.
struct ScrollCaptureTarget {
    quintptr window = 0;
    quint32 processId = 0;
    // Portal capture identifies an authorized monitor stream, not a window.
    quint64 captureSession = 0;
};
using ScrollRegionCallback = std::function<void(QImage, QString)>;
bool supportsScrollingCapture(QString *reason = nullptr);
bool beginScrollingCapture(const ScreenFrame &frame, const QRect &nativeRegion, QString *error = nullptr);
// Returning to the frozen selection can retain an authorized monitor stream;
// cancelling the whole capture or delivering a result releases it.
void endScrollingCapture(bool retainScreenSession = false);
bool supportsAutomaticScrollInput(QString *reason = nullptr);
void focusScrollingCaptureTarget(const ScrollCaptureTarget &target);
// Called immediately after transient controls are hidden, before the compositor
// settling delay. Event-driven screen streams can then accept this round's
// clean frame even when a static desktop does not produce another frame later.
void prepareScrollFrameRead();
ScrollCaptureTarget scrollCaptureTargetAt(QPoint nativePoint);
// Positive wheelSteps move down. Coordinates use the selected ScreenFrame's
// nativeGeometry; portal streams have their own native pixel origin.
// A successful send does not imply movement; the caller compares stable frames.
bool scrollCaptureStep(const ScrollCaptureTarget &target, QPoint nativePoint, int wheelSteps,
                       QString *error = nullptr);
void captureScrollRegion(const QRect &nativeRegion, ScrollRegionCallback callback);
// Let transient tray UI close before reading any screen pixels.
void prepareScreenCapture(QObject *context, std::function<void()> ready, bool fromTray = false);
quintptr captureForegroundWindow();
void restoreCaptureForegroundWindow(quintptr window);
void captureScreens(CaptureCallback callback);
QVector<Candidate> nativeElementsAt(QPoint nativePoint, qint64 excludedPid = 0);
bool requestAccessibility();
void configureNativeWindow(QWidget *window, bool overlay);
// Whether screen reads skip this window, so it can stay visible over a capture.
bool excludedFromCapture(const QWidget *window);
QString globalShortcutLabel();
class GlobalShortcut final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;
    bool start(const QKeySequence &sequence = QKeySequence("Alt+Shift+2", QKeySequence::PortableText));
    void stop();
    QKeySequence sequence() const {
        return sequence_;
    }
    QString lastError() const {
        return lastError_;
    }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void triggered();

  private:
    QKeySequence sequence_;
    QString lastError_;
    quint32 shortcutId_ = 0;
    void *handle_ = nullptr;
    void *handler_ = nullptr;
};
} // namespace h2d
