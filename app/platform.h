#pragma once
#include "model.h"
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
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
};
using CaptureCallback = std::function<void(QVector<ScreenFrame>, QString)>;
// Let transient tray UI close before reading any screen pixels.
void prepareScreenCapture(QObject *context, std::function<void()> ready);
void captureScreens(CaptureCallback callback);
QVector<Candidate> nativeElementsAt(QPoint nativePoint, qint64 excludedPid = 0);
bool requestAccessibility();
// Sends wheel steps to whatever window sits under a native point. This is what
// drives a page while a long picture is being captured: the caller takes one grab,
// scrolls, takes the next one and stitches them together. Positive steps scroll the
// way a wheel pushed away from the user does, or towards the right when the axis is
// horizontal. Returns false only when there is nothing to send the wheel to — whether
// the window moves is left to the frame that comes back, because no cheap question
// about it is reliable (most pages carry no scroll-bar style at all).
bool scrollAt(QPoint nativePoint, int steps, Qt::Orientation axis = Qt::Vertical);
void configureNativeWindow(QWidget *window, bool overlay);
QString globalShortcutLabel();
class GlobalShortcut final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;
    bool start(const QKeySequence &sequence = QKeySequence("Ctrl+Shift+2", QKeySequence::PortableText));
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
