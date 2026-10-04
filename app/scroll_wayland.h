#pragma once
#include "platform.h"
#include <QObject>
#include <QVariantMap>
#include <memory>

namespace h2d {
struct WaylandScreenGeometry {
    QString name;
    QRect logical;
};
// ScreenCast coordinates are compositor coordinates, not physical pixels.
// Require an unambiguous monitor mapping before showing a native selection.
bool mapWaylandMonitor(const QVariantMap &properties, const QVector<WaylandScreenGeometry> &screens,
                       QString *name, QRect *logical, QString *error = nullptr);
enum class WaylandPixelFormat { Bgrx, Bgra, Rgbx, Rgba, Rgb, Bgr };
qint64 waylandVideoClockNs();
qint64 mapWaylandVideoTimestamp(qint64 pts, qint64 streamClock, qint64 localClock);
QImage copyWaylandVideoFrame(const uchar *data, qsizetype bytes, QSize size, qsizetype stride,
                            WaylandPixelFormat format);

// The transport is injectable so the real Portal session lifecycle can be
// tested without asking the desktop to share a user's screen during tests.
class WaylandVideoSource : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool start(int ownedFd, quint32 node, quint64 serial, QString *error) = 0;
    virtual void stop() = 0;
    virtual void discardQueuedFrames() = 0;
  signals:
    void frameReady(QImage image, qint64 producedAt);
    void failed(QString error);
};

class WaylandCaptureSession final : public QObject {
    Q_OBJECT
  public:
    using SourceFactory = std::function<WaylandVideoSource *(QObject *)>;
    explicit WaylandCaptureSession(QObject *parent = nullptr, SourceFactory factory = {});
    ~WaylandCaptureSession() override;
    void start(CaptureCallback callback);
    void stop();
    void releaseSelection();
    bool active() const;
    bool begin(const ScreenFrame &frame, const QRect &nativeRegion, QString *error);
    ScrollCaptureTarget targetAt(QPoint nativePoint) const;
    void prepareRead();
    void capture(const QRect &nativeRegion, ScrollRegionCallback callback);
  private slots:
    void portalClosed(const QVariantMap &details);
  private:
    struct State;
    std::unique_ptr<State> state_;
    void selectSources();
    void startPortal();
    void openStream(const QVariantMap &result);
    void receiveFrame(QImage image, qint64 producedAt);
    void fail(const QString &error);
};

void waylandCaptureScreens(CaptureCallback callback);
bool waylandSupportsScrollingCapture(QString *reason = nullptr);
bool waylandBeginScrollingCapture(const ScreenFrame &frame, const QRect &nativeRegion, QString *error);
bool waylandSupportsAutomaticScrollInput(QString *reason = nullptr);
ScrollCaptureTarget waylandScrollTargetAt(QPoint nativePoint);
bool waylandScrollCaptureStep(const ScrollCaptureTarget &, QPoint, int, QString *error);
void waylandCaptureScrollRegion(const QRect &nativeRegion, ScrollRegionCallback callback);
void waylandPrepareScrollFrameRead();
void waylandEndCapture(bool retainScreenSession = false);
} // namespace h2d
