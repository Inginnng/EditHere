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
// horizontal. Returns false when there is nothing under the point that can be
// scrolled that way.
bool scrollAt(QPoint nativePoint, int steps, Qt::Orientation axis = Qt::Vertical);
// Whether the window under a native point can scroll the way an axis asks. A long
// capture asks before it starts, because scrolling a page that cannot move either
// produces one frame and a misleading finished picture, or nothing at all.
bool scrollableAt(QPoint nativePoint, Qt::Orientation axis);
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
