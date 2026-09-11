#pragma once
#include "model.h"
#include <QAbstractNativeEventFilter>
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
};
using CaptureCallback = std::function<void(QVector<ScreenFrame>, QString)>;
// Let transient tray UI close before reading any screen pixels.
void prepareScreenCapture(QObject *context, std::function<void()> ready);
void captureScreens(CaptureCallback callback);
QVector<Candidate> nativeElementsAt(QPoint nativePoint, qint64 excludedPid = 0);
bool requestAccessibility();
void configureNativeWindow(QWidget *window, bool overlay);
QString globalShortcutLabel();
class GlobalShortcut final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;
    bool start();
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void triggered();

  private:
    void *handle_ = nullptr;
    void *handler_ = nullptr;
};
} // namespace h2d
