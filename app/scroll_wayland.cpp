#include "scroll_wayland.h"
#include "linuxportal.h"
#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <limits>
#include <unistd.h>
#ifdef EDITHERE_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#endif

namespace h2d {
namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto path = "/org/freedesktop/portal/desktop";
constexpr auto screenCast = "org.freedesktop.portal.ScreenCast";
constexpr auto sessionInterface = "org.freedesktop.portal.Session";
QString automaticReason() {
    return QCoreApplication::translate("h2d", "Wayland 长截图使用已授权的屏幕共享流。请手动滚动；自动滚动需要额外的远程桌面输入授权，此版本未启用。");
}
bool pair(const QVariant &value, int *first, int *second) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto argument = value.value<QDBusArgument>();
        if (argument.currentSignature() != "(ii)") return false;
        argument.beginStructure(); argument >> *first >> *second; argument.endStructure();
        return true;
    }
    const auto values = value.toList();
    if (values.size() != 2) return false;
    bool a = false, b = false;
    *first = values[0].toInt(&a); *second = values[1].toInt(&b);
    return a && b;
}
QVector<WaylandScreenGeometry> currentScreens() {
    QVector<WaylandScreenGeometry> result;
    for (auto *screen : QGuiApplication::screens())
        result.append({screen->name(), screen->geometry()});
    return result;
}
struct PortalStream { quint32 node = 0; QVariantMap properties; };
QVector<PortalStream> streams(const QVariant &value) {
    QVector<PortalStream> result;
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return result;
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != "a(ua{sv})") return result;
    argument.beginArray();
    while (!argument.atEnd()) {
        PortalStream stream;
        argument.beginStructure(); argument >> stream.node >> stream.properties; argument.endStructure();
        result.append(stream);
    }
    argument.endArray();
    return result;
}
QString portalError(uint code, const QVariantMap &result) {
    return code == 1 ? QCoreApplication::translate("h2d", "屏幕共享已取消。") :
        QCoreApplication::translate("h2d", "屏幕共享授权失败，请确认桌面支持 ScreenCast Portal 和 PipeWire。") +
        (result.value("error").toString().isEmpty() ? QString() : "\n" + result.value("error").toString());
}
#ifdef EDITHERE_HAVE_PIPEWIRE
class PipeWireVideoSource final : public WaylandVideoSource {
  public:
    using WaylandVideoSource::WaylandVideoSource;
    ~PipeWireVideoSource() override { stop(); }
    bool start(int ownedFd, quint32 node, quint64 serial, QString *error) override {
        stop();
        static const bool initialized = [] { pw_init(nullptr, nullptr); return true; }();
        Q_UNUSED(initialized);
        loop_ = pw_thread_loop_new("edithere-screen", nullptr);
        if (!loop_) { close(ownedFd); *error = QCoreApplication::translate("h2d", "无法创建 PipeWire 屏幕流。"); return false; }
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (!context_) { close(ownedFd); stop(); *error = QCoreApplication::translate("h2d", "无法创建 PipeWire 屏幕流。"); return false; }
        // PipeWire takes ownership of the FD, including its failure path.
        core_ = pw_context_connect_fd(context_, ownedFd, nullptr, 0);
        if (!core_) { stop(); *error = QCoreApplication::translate("h2d", "无法连接已授权的 PipeWire 屏幕流。"); return false; }
        pw_core_add_listener(core_, &coreHook_, &coreEvents(), this);
        auto *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                              PW_KEY_MEDIA_ROLE, "Screen", nullptr);
        if (serial) pw_properties_set(properties, PW_KEY_TARGET_OBJECT, QByteArray::number(serial).constData());
        stream_ = pw_stream_new(core_, "EditHere screen capture", properties);
        if (!stream_) { stop(); *error = QCoreApplication::translate("h2d", "无法创建 PipeWire 屏幕流。"); return false; }
        pw_stream_add_listener(stream_, &streamHook_, &streamEvents(), this);
        uchar storage[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        const spa_pod *formats[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(7, SPA_VIDEO_FORMAT_BGRx,
                SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx,
                SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_RGB, SPA_VIDEO_FORMAT_BGR)))};
        const auto flags = pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
        if (pw_stream_connect(stream_, PW_DIRECTION_INPUT, serial ? PW_ID_ANY : node, flags, formats, 1) < 0 ||
            pw_thread_loop_start(loop_) < 0) {
            stop(); *error = QCoreApplication::translate("h2d", "无法连接已授权的 PipeWire 屏幕流。"); return false;
        }
        running_ = true;
        return true;
    }
    void stop() override {
        if (loop_ && running_) pw_thread_loop_stop(loop_);
        running_ = false;
        if (stream_) { spa_hook_remove(&streamHook_); pw_stream_destroy(stream_); stream_ = nullptr; }
        if (core_) { spa_hook_remove(&coreHook_); pw_core_disconnect(core_); core_ = nullptr; }
        if (context_) { pw_context_destroy(context_); context_ = nullptr; }
        if (loop_) { pw_thread_loop_destroy(loop_); loop_ = nullptr; }
        format_ = {};
    }
    void discardQueuedFrames() override {
        if (!loop_ || !running_) return;
        pw_thread_loop_lock(loop_);
        while (auto *buffer = pw_stream_dequeue_buffer(stream_))
            pw_stream_queue_buffer(stream_, buffer);
        pw_thread_loop_unlock(loop_);
    }
  private:
    pw_thread_loop *loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_stream *stream_ = nullptr;
    spa_hook coreHook_ = {}, streamHook_ = {};
    spa_video_info_raw format_ = {};
    bool running_ = false;
    QMutex frameMutex_;
    QImage pendingImage_;
    qint64 pendingProducedAt_ = 0;
    bool deliveryQueued_ = false;
    void report(QString error) {
        QMetaObject::invokeMethod(this, [this, error] { emit failed(error); }, Qt::QueuedConnection);
    }
    static const pw_core_events &coreEvents() {
        static const pw_core_events events = [] {
            pw_core_events value{}; value.version = PW_VERSION_CORE_EVENTS;
            value.error = [](void *data, uint32_t, int, int, const char *message) {
                auto *self = static_cast<PipeWireVideoSource *>(data);
                self->report(QCoreApplication::translate("h2d", "屏幕共享流已断开。") + (message ? "\n" + QString::fromUtf8(message) : QString()));
            };
            return value;
        }();
        return events;
    }
    static const pw_stream_events &streamEvents() {
        static const pw_stream_events events = [] {
            pw_stream_events value{}; value.version = PW_VERSION_STREAM_EVENTS;
            value.state_changed = [](void *data, pw_stream_state previous, pw_stream_state next, const char *error) {
                auto *self = static_cast<PipeWireVideoSource *>(data);
                if (next == PW_STREAM_STATE_ERROR ||
                    (next == PW_STREAM_STATE_UNCONNECTED && previous != PW_STREAM_STATE_UNCONNECTED))
                    self->report(QCoreApplication::translate("h2d", "屏幕共享流已断开。") + (error ? "\n" + QString::fromUtf8(error) : QString()));
            };
            value.param_changed = [](void *data, uint32_t id, const spa_pod *parameter) {
                auto *self = static_cast<PipeWireVideoSource *>(data);
                if (!parameter || id != SPA_PARAM_Format) return;
                if (spa_format_video_raw_parse(parameter, &self->format_) < 0) {
                    self->report(QCoreApplication::translate("h2d", "屏幕共享流的视频格式不受支持。")); return;
                }
                // Request CPU-addressable buffers. A compositor may otherwise
                // negotiate GPU-only DMA buffers with no readable pixel memory.
                uchar storage[256];
                spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
                const spa_pod *parameters[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(&builder,
                    SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
                    SPA_PARAM_BUFFERS_dataType,
                    SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)))),
                    static_cast<const spa_pod *>(spa_pod_builder_add_object(&builder,
                        SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
                        SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))))};
                pw_stream_update_params(self->stream_, parameters, 2);
            };
            value.process = [](void *data) { static_cast<PipeWireVideoSource *>(data)->process(); };
            return value;
        }();
        return events;
    }
    void process() {
        QImage latest;
        qint64 producedAt = 0;
        while (auto *buffer = pw_stream_dequeue_buffer(stream_)) {
            const auto *pixels = buffer->buffer;
            const auto *header = pixels ? static_cast<const spa_meta_header *>(
                spa_buffer_find_meta_data(pixels, SPA_META_Header, sizeof(spa_meta_header))) : nullptr;
            // Capture PTS is supplied by the compositor, not our dequeue time.
            // Map its stream-clock time to the local monotonic clock. Require
            // it to be in the current clock domain; unknown timestamps fail
            // freshness checks instead of silently reusing a queued old frame.
            pw_time streamTime{};
            const qint64 streamNow = pw_stream_get_nsec(stream_);
            const qint64 localNow = waylandVideoClockNs();
            const qint64 mappedTime = header ? mapWaylandVideoTimestamp(header->pts, streamNow, localNow) : 0;
            const bool validTime = mappedTime > 0 &&
                !(header->flags & (SPA_META_HEADER_FLAG_CORRUPTED | SPA_META_HEADER_FLAG_GAP)) &&
                pw_stream_get_time_n(stream_, &streamTime, sizeof(streamTime)) == 0 &&
                streamTime.now > 0;
            if (pixels && pixels->n_datas == 1) {
                const auto &plane = pixels->datas[0];
                if (plane.data && plane.chunk && plane.chunk->offset <= plane.maxsize &&
                    plane.chunk->size <= plane.maxsize - plane.chunk->offset) {
                    WaylandPixelFormat pixelFormat;
                    bool recognized = true;
                    switch (format_.format) {
                    case SPA_VIDEO_FORMAT_BGRx: pixelFormat = WaylandPixelFormat::Bgrx; break;
                    case SPA_VIDEO_FORMAT_BGRA: pixelFormat = WaylandPixelFormat::Bgra; break;
                    case SPA_VIDEO_FORMAT_RGBx: pixelFormat = WaylandPixelFormat::Rgbx; break;
                    case SPA_VIDEO_FORMAT_RGBA: pixelFormat = WaylandPixelFormat::Rgba; break;
                    case SPA_VIDEO_FORMAT_RGB: pixelFormat = WaylandPixelFormat::Rgb; break;
                    case SPA_VIDEO_FORMAT_BGR: pixelFormat = WaylandPixelFormat::Bgr; break;
                    default: recognized = false; break;
                    }
                    if (recognized && validTime) {
                        auto image = copyWaylandVideoFrame(
                            static_cast<const uchar *>(plane.data) + plane.chunk->offset, plane.chunk->size,
                            QSize(format_.size.width, format_.size.height), plane.chunk->stride, pixelFormat);
                        if (!image.isNull()) {
                            latest = image;
                            producedAt = mappedTime;
                        }
                    }
                }
            }
            pw_stream_queue_buffer(stream_, buffer);
        }
        if (!latest.isNull()) {
            QMutexLocker lock(&frameMutex_);
            pendingImage_ = latest; pendingProducedAt_ = producedAt;
            if (deliveryQueued_) return;
            deliveryQueued_ = true;
            QMetaObject::invokeMethod(this, [this] {
                QImage image; qint64 time;
                {
                    QMutexLocker lock(&frameMutex_);
                    image = std::move(pendingImage_); time = pendingProducedAt_;
                    deliveryQueued_ = false;
                }
                emit frameReady(image, time);
            }, Qt::QueuedConnection);
        }
    }
};
#endif
WaylandVideoSource *createSource(QObject *parent) {
#ifdef EDITHERE_HAVE_PIPEWIRE
    return new PipeWireVideoSource(parent);
#else
    Q_UNUSED(parent);
    return nullptr;
#endif
}
QPointer<WaylandCaptureSession> currentSession;
quint64 currentStartGeneration = 0;
void screenshotFallback(CaptureCallback callback) {
    auto *request = new LinuxPortalRequest(qApp);
    request->start("org.freedesktop.portal.Screenshot", "Screenshot", {QString()}, {{"interactive", true}},
        [callback = std::move(callback)](uint code, QVariantMap result) {
            if (code != 0) {
                callback({}, code == 1 ? QCoreApplication::translate("h2d", "截图已取消。") : QCoreApplication::translate("h2d", "截图授权失败，请确认桌面已安装截图 Portal。")); return;
            }
            const QUrl uri(result.value("uri").toString());
            QImageReader reader(uri.toLocalFile());
            if (!uri.isLocalFile() || !reader.size().isValid() ||
                qint64(reader.size().width()) * reader.size().height() > MaxPixels) {
                callback({}, QCoreApplication::translate("h2d", "截图文件无效或过大。")); return;
            }
            auto image = reader.read();
            auto *screen = QGuiApplication::primaryScreen();
            if (image.isNull() || !screen) { callback({}, QCoreApplication::translate("h2d", "无法读取屏幕图像。")); return; }
            image.setDevicePixelRatio(1);
            ScreenFrame frame;
            frame.name = "portal"; frame.image = image;
            frame.elementProbingAllowed = false;
            const auto size = image.size().scaled(screen->availableGeometry().size(), Qt::KeepAspectRatio);
            frame.logicalGeometry = QRect(screen->availableGeometry().topLeft(), size);
            // Screenshot has no monitor identity/origin. Never label this
            // review image as native or use it for repeated screen capture.
            callback({frame}, {});
        });
}
} // namespace

qint64 waylandVideoClockNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
qint64 mapWaylandVideoTimestamp(qint64 pts, qint64 streamClock, qint64 localClock) {
    if (pts <= 0 || streamClock < pts || localClock <= 0 ||
        streamClock - pts >= 10'000'000'000LL || localClock <= streamClock - pts) return 0;
    return localClock - (streamClock - pts);
}

bool mapWaylandMonitor(const QVariantMap &properties, const QVector<WaylandScreenGeometry> &screens,
                       QString *name, QRect *logical, QString *error) {
    int x = 0, y = 0, width = 0, height = 0;
    if (properties.value("source_type").toUInt() != 1 || !pair(properties.value("position"), &x, &y) ||
        !pair(properties.value("size"), &width, &height) || width <= 0 || height <= 0) {
        if (error) *error = QCoreApplication::translate("h2d", "桌面没有提供共享屏幕的位置和尺寸，无法可靠定位长截图选区。");
        return false;
    }
    const QRect geometry(x, y, width, height);
    const WaylandScreenGeometry *match = nullptr;
    for (const auto &screen : screens) {
        if (screen.logical != geometry) continue;
        if (match) {
            if (error) *error = QCoreApplication::translate("h2d", "共享屏幕的位置对应多个显示器，无法可靠定位长截图选区。");
            return false;
        }
        match = &screen;
    }
    if (!match) {
        if (error) *error = QCoreApplication::translate("h2d", "共享屏幕的位置与当前显示布局不一致，请重新选择屏幕。");
        return false;
    }
    if (name) *name = match->name;
    if (logical) *logical = match->logical;
    return true;
}

QImage copyWaylandVideoFrame(const uchar *data, qsizetype bytes, QSize size, qsizetype stride,
                            WaylandPixelFormat format) {
    const int channels = (format == WaylandPixelFormat::Rgb || format == WaylandPixelFormat::Bgr) ? 3 : 4;
    const qint64 rowBytes = qint64(size.width()) * channels;
    if (!data || size.width() <= 0 || size.height() <= 0 ||
        qint64(size.width()) * size.height() > MaxPixels || stride < rowBytes ||
        stride > std::numeric_limits<qsizetype>::max() / size.height() ||
        bytes < qint64(size.height() - 1) * stride + rowBytes) return {};
    QImage image(size, QImage::Format_RGB32);
    if (image.isNull()) return {};
    const bool bgr = format == WaylandPixelFormat::Bgrx || format == WaylandPixelFormat::Bgra ||
                     format == WaylandPixelFormat::Bgr;
    for (int y = 0; y < size.height(); ++y) {
        const auto *source = data + y * stride;
        auto *destination = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            destination[x] = bgr ? qRgb(source[2], source[1], source[0]) : qRgb(source[0], source[1], source[2]);
            source += channels;
        }
    }
    return image;
}

struct WaylandCaptureSession::State {
    SourceFactory factory;
    QPointer<LinuxPortalRequest> request;
    QPointer<WaylandVideoSource> source;
    CaptureCallback starting;
    ScrollRegionCallback reading;
    QString session, screenName, frameName;
    QRect logical, native, requestedRegion;
    QSize pixelSize;
    QImage latest;
    quint64 identity = 0, generation = 0;
    quint64 readSequence = 0;
    qint64 requestedAfter = 0;
    qint64 preparedAfter = 0, latestProducedAt = 0;
    bool locked = false, initialDelivered = false;
};
WaylandCaptureSession::WaylandCaptureSession(QObject *parent, SourceFactory factory)
    : QObject(parent), state_(std::make_unique<State>()) {
    state_->factory = factory ? std::move(factory) : createSource;
}
WaylandCaptureSession::~WaylandCaptureSession() { stop(); }
bool WaylandCaptureSession::active() const {
    return !state_->session.isEmpty() && state_->source && state_->initialDelivered && !state_->latest.isNull();
}
void WaylandCaptureSession::start(CaptureCallback callback) {
    stop();
    state_->starting = std::move(callback);
    state_->source = state_->factory(this);
    if (!state_->source) { fail(QCoreApplication::translate("h2d", "此安装未包含 PipeWire 支持，Wayland 仅支持普通截图。")); return; }
    connect(state_->source, &WaylandVideoSource::frameReady, this, &WaylandCaptureSession::receiveFrame);
    connect(state_->source, &WaylandVideoSource::failed, this, &WaylandCaptureSession::fail);
    const QString token = "edithere_scroll_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = QDBusConnection::sessionBus().baseService().mid(1); sender.replace('.', '_');
    // Subscribe before CreateSession so a backend can close the session without
    // racing our first observer, including while its source picker is open.
    state_->session = "/org/freedesktop/portal/desktop/session/" + sender + "/" + token;
    QDBusConnection::sessionBus().connect(service, state_->session, sessionInterface, "Closed", this,
        SLOT(portalClosed(QVariantMap)));
    const quint64 generation = state_->generation;
    state_->request = new LinuxPortalRequest(this);
    state_->request->start(screenCast, "CreateSession", {}, {{"session_handle_token", token}},
        [this, generation](uint code, QVariantMap result) {
            if (generation != state_->generation) return;
            state_->request = nullptr;
            if (code != 0) { fail(portalError(code, result)); return; }
            if (result.value("session_handle").toString() != state_->session) {
                fail(QCoreApplication::translate("h2d", "屏幕共享返回了不同的会话，已取消采集。")); return;
            }
            selectSources();
        });
}
void WaylandCaptureSession::selectSources() {
    const quint64 generation = state_->generation;
    state_->request = new LinuxPortalRequest(this);
    state_->request->start(screenCast, "SelectSources", {QVariant::fromValue(QDBusObjectPath(state_->session))},
        {{"types", quint32(1)}, {"multiple", false}},
        [this, generation](uint code, QVariantMap result) {
            if (generation != state_->generation) return;
            state_->request = nullptr;
            if (code != 0) { fail(portalError(code, result)); return; }
            startPortal();
        });
}
void WaylandCaptureSession::startPortal() {
    const quint64 generation = state_->generation;
    state_->request = new LinuxPortalRequest(this);
    state_->request->start(screenCast, "Start", {QVariant::fromValue(QDBusObjectPath(state_->session)), QString()}, {},
        [this, generation](uint code, QVariantMap result) {
            if (generation != state_->generation) return;
            state_->request = nullptr;
            if (code != 0) { fail(portalError(code, result)); return; }
            openStream(result);
        });
}
void WaylandCaptureSession::openStream(const QVariantMap &result) {
    const auto selected = streams(result.value("streams"));
    QString error;
    if (selected.size() != 1 || !selected[0].node) {
        fail(QCoreApplication::translate("h2d", "屏幕共享没有返回单个显示器视频流，请重新选择一个屏幕。")); return;
    }
    if (!mapWaylandMonitor(selected[0].properties, currentScreens(), &state_->screenName, &state_->logical, &error)) {
        fail(error); return;
    }
    state_->identity = QRandomGenerator::global()->generate64();
    if (!state_->identity) state_->identity = 1;
    state_->frameName = "wayland:" + state_->session + ":" + selected[0].properties.value("id").toString();
    auto call = QDBusMessage::createMethodCall(service, path, screenCast, "OpenPipeWireRemote");
    call.setArguments({QVariant::fromValue(QDBusObjectPath(state_->session)), QVariantMap()});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    const quint64 generation = state_->generation;
    const quint32 node = selected[0].node;
    const quint64 serial = selected[0].properties.value("pipewire-serial").toULongLong();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, node, serial] {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        watcher->deleteLater();
        if (generation != state_->generation) return;
        if (reply.isError() || !reply.value().isValid()) {
            fail(QCoreApplication::translate("h2d", "无法打开已授权的 PipeWire 屏幕流。") +
                 (reply.isError() ? "\n" + reply.error().message() : QString())); return;
        }
        const int fd = dup(reply.value().fileDescriptor());
        QString error;
        if (fd < 0 || !state_->source->start(fd, node, serial, &error)) {
            fail(error.isEmpty() ? QCoreApplication::translate("h2d", "无法打开已授权的 PipeWire 屏幕流。") : error); return;
        }
        QTimer::singleShot(5000, this, [this, generation] {
            if (generation == state_->generation && !state_->initialDelivered)
                fail(QCoreApplication::translate("h2d", "屏幕共享没有提供可读取的像素，请检查桌面的 PipeWire 支持。"));
        });
    });
}
void WaylandCaptureSession::receiveFrame(QImage image, qint64 producedAt) {
    if (state_->session.isEmpty() || image.isNull()) return;
    QString name; QRect logical;
    const QVariantMap properties{{"source_type", quint32(1)},
        {"position", QVariantList{state_->logical.x(), state_->logical.y()}},
        {"size", QVariantList{state_->logical.width(), state_->logical.height()}}};
    QString error;
    if (!mapWaylandMonitor(properties, currentScreens(), &name, &logical, &error) || name != state_->screenName) {
        fail(QCoreApplication::translate("h2d", "共享屏幕的布局已改变，请重新截图。")); return;
    }
    if (qint64(image.width()) * image.height() > MaxPixels ||
        (!state_->pixelSize.isEmpty() && image.size() != state_->pixelSize) ||
        std::abs(qint64(image.width()) * logical.height() - qint64(image.height()) * logical.width()) >
            std::max(logical.width(), logical.height())) {
        fail(QCoreApplication::translate("h2d", "共享屏幕的像素尺寸已改变或与显示器不一致，请重新截图。")); return;
    }
    image.setDevicePixelRatio(1);
    state_->pixelSize = image.size();
    // Wayland exposes no global physical-pixel coordinate space. Native
    // coordinates for this one authorized stream are explicitly stream-local;
    // logicalGeometry is the compositor's verified monitor position.
    state_->native = QRect(QPoint(), image.size());
    state_->latest = image;
    state_->latestProducedAt = producedAt;
    if (!state_->initialDelivered) {
        state_->initialDelivered = true;
        ScreenFrame frame;
        frame.name = state_->frameName; frame.image = image;
        frame.logicalGeometry = state_->logical; frame.nativeGeometry = state_->native;
        frame.nativePixels = true;
        frame.elementProbingAllowed = false;
        auto callback = std::move(state_->starting); state_->starting = {};
        if (callback) callback({frame}, {});
        return;
    }
    if (state_->reading && producedAt > state_->requestedAfter) {
        auto callback = std::move(state_->reading); state_->reading = {};
        // capture() only accepts an image delivered after that request. It
        // never substitutes the cached frame containing our own overlay UI.
        callback(image.copy(state_->requestedRegion), {});
    }
}
void WaylandCaptureSession::portalClosed(const QVariantMap &) { fail(QCoreApplication::translate("h2d", "屏幕共享已结束，请重新截图。")); }
void WaylandCaptureSession::fail(const QString &error) {
    auto starting = std::move(state_->starting); state_->starting = {};
    auto reading = std::move(state_->reading); state_->reading = {};
    stop();
    if (starting) starting({}, error);
    if (reading) reading({}, error);
}
void WaylandCaptureSession::stop() {
    ++state_->generation;
    if (state_->request) { delete state_->request; state_->request = nullptr; }
    if (state_->source) {
        state_->source->stop(); delete state_->source; state_->source = nullptr;
    }
    if (!state_->session.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(service, state_->session, sessionInterface, "Closed", this,
                                                 SLOT(portalClosed(QVariantMap)));
        auto call = QDBusMessage::createMethodCall(service, state_->session, sessionInterface, "Close");
        QDBusConnection::sessionBus().asyncCall(call);
    }
    state_->session.clear(); state_->latest = {}; state_->logical = {}; state_->native = {};
    state_->pixelSize = {}; state_->frameName.clear(); state_->identity = 0;
    state_->initialDelivered = false; state_->locked = false;
    state_->preparedAfter = state_->latestProducedAt = 0;
    auto starting = std::move(state_->starting); state_->starting = {};
    auto reading = std::move(state_->reading); state_->reading = {};
    if (starting) starting({}, QCoreApplication::translate("h2d", "屏幕共享已取消。"));
    if (reading) reading({}, QCoreApplication::translate("h2d", "屏幕共享已取消。"));
}
bool WaylandCaptureSession::begin(const ScreenFrame &frame, const QRect &nativeRegion, QString *error) {
    if (!active() || frame.name != state_->frameName || !frame.nativePixels ||
        frame.nativeGeometry != state_->native || frame.logicalGeometry != state_->logical ||
        frame.image.size() != state_->pixelSize || !state_->native.contains(nativeRegion) || nativeRegion.isEmpty()) {
        if (error) *error = QCoreApplication::translate("h2d", "屏幕共享会话或选区已改变，请重新截图。");
        return false;
    }
    state_->locked = true;
    return true;
}
void WaylandCaptureSession::releaseSelection() {
    ++state_->readSequence;
    state_->locked = false; state_->preparedAfter = 0;
    auto callback = std::move(state_->reading); state_->reading = {};
    if (callback) callback({}, QCoreApplication::translate("h2d", "画面读取已取消。"));
}
ScrollCaptureTarget WaylandCaptureSession::targetAt(QPoint nativePoint) const {
    if (!active() || !state_->locked || !state_->native.contains(nativePoint)) return {};
    return {0, 0, state_->identity};
}
void WaylandCaptureSession::prepareRead() {
    if (!active() || !state_->locked) return;
    state_->source->discardQueuedFrames();
    state_->preparedAfter = waylandVideoClockNs();
}
void WaylandCaptureSession::capture(const QRect &nativeRegion, ScrollRegionCallback callback) {
    if (!active() || !state_->locked || nativeRegion.isEmpty() || !state_->native.contains(nativeRegion)) {
        callback({}, QCoreApplication::translate("h2d", "屏幕共享会话或选区已改变，请重新截图。")); return;
    }
    auto previous = std::move(state_->reading); state_->reading = {};
    if (previous) previous({}, QCoreApplication::translate("h2d", "画面读取已取消。"));
    const quint64 sequence = ++state_->readSequence;
    const qint64 cutoff = state_->preparedAfter;
    state_->preparedAfter = 0;
    if (!cutoff) state_->source->discardQueuedFrames();
    state_->requestedRegion = nativeRegion;
    state_->requestedAfter = cutoff ? cutoff : waylandVideoClockNs();
    if (cutoff && state_->latestProducedAt > cutoff) {
        callback(state_->latest.copy(nativeRegion), {});
        return;
    }
    state_->reading = std::move(callback);
    const quint64 generation = state_->generation;
    QTimer::singleShot(1500, this, [this, generation, sequence] {
        if (generation != state_->generation || sequence != state_->readSequence || !state_->reading) return;
        auto callback = std::move(state_->reading); state_->reading = {};
        callback({}, QCoreApplication::translate("h2d", "屏幕共享没有提供新的画面，请确认共享仍在进行。"));
    });
}
void waylandCaptureScreens(CaptureCallback callback) {
    waylandEndCapture();
#ifdef EDITHERE_HAVE_PIPEWIRE
    const quint64 generation = currentStartGeneration;
    auto call = QDBusMessage::createMethodCall(service, path, "org.freedesktop.DBus.Properties", "Get");
    call.setArguments({QString(screenCast), QString("AvailableSourceTypes")});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), qApp);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [watcher, generation, callback = std::move(callback)]() mutable {
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        watcher->deleteLater();
        if (generation != currentStartGeneration) {
            callback({}, QCoreApplication::translate("h2d", "屏幕共享已取消。")); return;
        }
        if (reply.isError() || !(reply.value().variant().toUInt() & 1)) {
            screenshotFallback(std::move(callback)); return;
        }
        auto *session = new WaylandCaptureSession(qApp);
        currentSession = session;
        session->start(std::move(callback));
    });
#else
    screenshotFallback(std::move(callback));
#endif
}
bool waylandSupportsScrollingCapture(QString *reason) {
    const bool available = currentSession && currentSession->active();
    if (!available && reason) {
#ifdef EDITHERE_HAVE_PIPEWIRE
        *reason = QCoreApplication::translate("h2d", "长截图需要正在共享的显示器。请重新截图并在系统授权窗口中选择一个屏幕。");
#else
        *reason = QCoreApplication::translate("h2d", "此安装未包含 PipeWire 支持，Wayland 仅支持普通截图。");
#endif
    }
    return available;
}
bool waylandBeginScrollingCapture(const ScreenFrame &frame, const QRect &nativeRegion, QString *error) {
    if (!waylandSupportsScrollingCapture(error)) return false;
    return currentSession->begin(frame, nativeRegion, error);
}
bool waylandSupportsAutomaticScrollInput(QString *reason) { if (reason) *reason = automaticReason(); return false; }
ScrollCaptureTarget waylandScrollTargetAt(QPoint nativePoint) {
    return currentSession ? currentSession->targetAt(nativePoint) : ScrollCaptureTarget{};
}
bool waylandScrollCaptureStep(const ScrollCaptureTarget &, QPoint, int, QString *error) {
    if (error) *error = automaticReason();
    return false;
}
void waylandCaptureScrollRegion(const QRect &nativeRegion, ScrollRegionCallback callback) {
    if (!currentSession) { callback({}, QCoreApplication::translate("h2d", "屏幕共享已结束，请重新截图。")); return; }
    currentSession->capture(nativeRegion, std::move(callback));
}
void waylandPrepareScrollFrameRead() { if (currentSession) currentSession->prepareRead(); }
void waylandEndCapture(bool retainScreenSession) {
    if (retainScreenSession && currentSession && currentSession->active()) {
        currentSession->releaseSelection(); return;
    }
    ++currentStartGeneration;
    if (currentSession) { auto *session = currentSession.data(); currentSession = nullptr; delete session; }
}
} // namespace h2d
