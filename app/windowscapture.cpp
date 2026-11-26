#include "windowscapture.h"
#include "model.h"
#include "diagnostics.h"
#include <QColorSpace>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QTransform>
#include <qfloat16.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

namespace h2d::windowscapture {
using Microsoft::WRL::ComPtr;
namespace {
std::mutex captureSettingsMutex;
std::unordered_set<HMONITOR> bypassedMonitors;
int watchdogTimeoutForTesting = 0;
std::function<void()> acquireFrameHookForTesting;

QString failure(const char *operation, HRESULT hr) {
    return QString("%1: 0x%2").arg(QLatin1String(operation),
        QString::number(quint32(hr), 16).rightJustified(8, '0'));
}
QRect rectangle(const RECT &rect) {
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}
}

namespace testing {
void setWatchdogTimeoutForTesting(int timeoutMs) {
    std::lock_guard lock(captureSettingsMutex);
    watchdogTimeoutForTesting = timeoutMs;
}

void setAcquireFrameHookForTesting(std::function<void()> hook) {
    std::lock_guard lock(captureSettingsMutex);
    acquireFrameHookForTesting = std::move(hook);
}

void resetBypassedMonitorsForTesting() {
    std::lock_guard lock(captureSettingsMutex);
    bypassedMonitors.clear();
}
}

DisplayColor displayColor(HMONITOR monitor) {
    DisplayColor result;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return result;
    // The topology may change between the size and data calls. Retry only that
    // documented race, rather than caching HDR settings across monitor changes.
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return result;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                                &modeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (status != ERROR_SUCCESS)
            return result;
        for (UINT32 i = 0; i < pathCount; ++i) {
            const auto &path = paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                             path.sourceInfo.adapterId, path.sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                wcscmp(source.viewGdiDeviceName, info.szDevice) != 0)
                continue;
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
            color.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(color),
                            path.targetInfo.adapterId, path.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS || !color.advancedColorEnabled)
                continue;
            result.advancedColor = true;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
            white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white),
                            path.targetInfo.adapterId, path.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS &&
                white.SDRWhiteLevel > 0)
                result.sdrWhiteLevel = float(white.SDRWhiteLevel) / 1000.0f;
        }
        break;
    }
    return result;
}

QImage toSrgb(const void *data, qsizetype byteCount, QSize size, qsizetype rowPitch,
              PixelFormat format, float sdrWhiteLevel, int rotation) {
    const int bytesPerPixel = format == PixelFormat::ScRgb16 ? 8 : 4;
    if (!data || size.isEmpty() || qint64(size.width()) * size.height() > MaxPixels ||
        rowPitch < qsizetype(size.width()) * bytesPerPixel ||
        rowPitch > std::numeric_limits<qsizetype>::max() / size.height() ||
        byteCount < rowPitch * size.height() ||
        !std::isfinite(sdrWhiteLevel) || sdrWhiteLevel <= 0 ||
        (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270))
        return {};
    QImage image(size, QImage::Format_RGB32);
    if (image.isNull())
        return {};
    // A small LUT avoids millions of pow() calls for every full-resolution
    // frame. It is keyed by the monitor's current SDR white setting.
    struct Lut {
        float white = 0;
        std::array<uchar, 65536> values{};
    };
    static thread_local Lut lut;
    if (format == PixelFormat::ScRgb16 && lut.white != sdrWhiteLevel) {
        for (quint32 bits = 0; bits < lut.values.size(); ++bits) {
            const quint16 word = quint16(bits);
            qfloat16 half;
            std::memcpy(&half, &word, sizeof(half));
            const float linear = float(half) / sdrWhiteLevel;
            float srgb = 0;
            if (linear > 0)
                srgb = linear >= 1 ? 1 : linear <= 0.0031308f ? linear * 12.92f :
                    1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            lut.values[bits] = uchar(qRound(srgb * 255));
        }
        lut.white = sdrWhiteLevel;
    }
    for (int y = 0; y < size.height(); ++y) {
        const auto *source = static_cast<const uchar *>(data) + y * rowPitch;
        auto *target = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            if (format == PixelFormat::ScRgb16) {
                quint16 rgba[4];
                std::memcpy(rgba, source + x * 8, sizeof(rgba));
                target[x] = qRgb(lut.values[rgba[0]], lut.values[rgba[1]], lut.values[rgba[2]]);
            } else {
                const auto *pixel = source + x * 4;
                target[x] = qRgb(pixel[2], pixel[1], pixel[0]);
            }
        }
    }
    if (rotation)
        image = image.transformed(QTransform().rotate(rotation), Qt::FastTransformation);
    image.setColorSpace(QColorSpace::SRgb);
    image.setDevicePixelRatio(1);
    return image;
}

struct DesktopCapture::State {
    static std::unique_ptr<State> open(HMONITOR monitor, QString *error);
    QImage readFrame(float sdrWhiteLevel, int timeoutMs, const std::function<void()> &beforeAcquire,
                     QString *error, QJsonObject *conversion);

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    QRect geometry;
    QString adapter;
    int rotation = 0;
    bool hdr = false;
    QImage lastImage;
};

bool isBlankFrame(const QImage &image) {
    if (image.isNull())
        return true;
    // A frame that never received the desktop is exactly zero, which is far
    // below any real picture: the darkest UI text still reaches a few levels.
    // The threshold stays low so a genuinely dark screen is not misread.
    constexpr int darkest = 3;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            if (qRed(pixel) > darkest || qGreen(pixel) > darkest || qBlue(pixel) > darkest)
                return false;
        }
    }
    return true;
}
std::unique_ptr<DesktopCapture::State> DesktopCapture::State::open(HMONITOR monitor, QString *error) {
    auto fail = [&](const char *operation, HRESULT hr) -> std::unique_ptr<State> {
        if (error) *error = failure(operation, hr);
        return {};
    };
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return fail("CreateDXGIFactory1", hr);
    for (UINT adapterIndex = 0; ; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return fail("EnumAdapters1", hr);
        for (UINT outputIndex = 0; ; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, output.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) return fail("EnumOutputs", hr);
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop || desc.Monitor != monitor)
                continue;
            auto state = std::make_unique<State>();
            state->geometry = rectangle(desc.DesktopCoordinates);
            switch (desc.Rotation) {
            case DXGI_MODE_ROTATION_UNSPECIFIED:
            case DXGI_MODE_ROTATION_IDENTITY: break;
            case DXGI_MODE_ROTATION_ROTATE90: state->rotation = 90; break;
            case DXGI_MODE_ROTATION_ROTATE180: state->rotation = 180; break;
            case DXGI_MODE_ROTATION_ROTATE270: state->rotation = 270; break;
            default: return fail("Output rotation", E_INVALIDARG);
            }
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc1{};
            if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc1)))
                state->hdr = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            // D3D_DRIVER_TYPE_UNKNOWN is required when selecting an explicit
            // adapter, including laptops whose monitors use different GPUs.
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                state->device.GetAddressOf(), nullptr, state->context.GetAddressOf());
            if (FAILED(hr)) return fail("D3D11CreateDevice", hr);
            // A virtual display adapter can own an output that reports the same
            // monitor and still never paints, so the adapter that served the
            // duplication has to be identifiable in the diagnostics.
            DXGI_ADAPTER_DESC adapterDesc{};
            if (SUCCEEDED(adapter->GetDesc(&adapterDesc)))
                state->adapter = QString::fromWCharArray(adapterDesc.Description);
            ComPtr<IDXGIOutput5> output5;
            hr = output.As(&output5);
            if (FAILED(hr)) return fail("IDXGIOutput5", hr);
            const DXGI_FORMAT hdrFormats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
            const DXGI_FORMAT sdrFormats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
            hr = output5->DuplicateOutput1(state->device.Get(), 0,
                                          state->hdr ? UINT(std::size(hdrFormats)) : UINT(std::size(sdrFormats)),
                                          state->hdr ? hdrFormats : sdrFormats,
                                          state->duplication.GetAddressOf());
            if (FAILED(hr)) return fail("DuplicateOutput1", hr);
            return state;
        }
    }
    return fail("Monitor output unavailable", DXGI_ERROR_NOT_FOUND);
}

struct DesktopCapture::Worker {
    explicit Worker(HMONITOR monitor) : monitor(monitor) {}

    void run() {
        const HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId());
        const HDESK inputDesktop = OpenInputDesktop(0, FALSE, GENERIC_READ);
        const bool switched = inputDesktop && originalDesktop && SetThreadDesktop(inputDesktop);
        const auto restoreDesktop = qScopeGuard([=] {
            if (switched) SetThreadDesktop(originalDesktop);
            if (inputDesktop) CloseDesktop(inputDesktop);
        });

        // The duplication and all its COM objects belong to this thread.
        auto state = State::open(monitor, &error);
        std::unique_lock lock(mutex);
        if (stopping) return;
        opened = bool(state);
        if (opened) geometry = state->geometry;
        ready = true;
        cv.notify_one();
        if (!opened) return;

        for (;;) {
            cv.wait(lock, [this] { return stopping || !ready; });
            if (stopping) return;
            // The caller leaves request and result storage alone until ready,
            // and can abandon the wait while the driver holds up this call.
            lock.unlock();
            image = state->readFrame(sdrWhiteLevel, timeoutMs, beforeAcquire, &error, &conversion);
            lock.lock();
            ready = true;
            cv.notify_one();
        }
    }

    bool waitForResult(std::unique_lock<std::mutex> &lock, int watchdogMs) {
        {
            std::lock_guard settingsLock(captureSettingsMutex);
            if (watchdogTimeoutForTesting > 0) watchdogMs = watchdogTimeoutForTesting;
        }
        if (cv.wait_for(lock, std::chrono::milliseconds(watchdogMs), [this] { return ready; }))
            return true;

        // The thread keeps its own reference until the driver call and cleanup finish.
        stopping = true;
        thread.detach();
        std::lock_guard settingsLock(captureSettingsMutex);
        bypassedMonitors.insert(monitor);
        return false;
    }

    const HMONITOR monitor;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool ready = false;
    bool opened = false;
    bool stopping = false;
    QRect geometry;
    float sdrWhiteLevel = 1;
    int timeoutMs = 0;
    std::function<void()> beforeAcquire;
    QString error;
    QImage image;
    QJsonObject conversion;
};

DesktopCapture::DesktopCapture(HMONITOR monitor) : worker_(std::make_shared<Worker>(monitor)) {}

DesktopCapture::~DesktopCapture() {
    if (!worker_->thread.joinable()) return;
    {
        std::lock_guard lock(worker_->mutex);
        worker_->stopping = true;
    }
    worker_->cv.notify_one();
    worker_->thread.join();
}

QRect DesktopCapture::geometry() const { return worker_->geometry; }

std::unique_ptr<DesktopCapture> DesktopCapture::open(HMONITOR monitor, QString *error) {
    if (error) error->clear();
    {
        std::lock_guard lock(captureSettingsMutex);
        if (bypassedMonitors.contains(monitor)) {
            if (error) *error = QStringLiteral("DXGI desktop duplication bypassed after prior driver timeout");
            return {};
        }
    }
    auto capture = std::unique_ptr<DesktopCapture>(new DesktopCapture(monitor));
    auto &worker = *capture->worker_;
    try {
        worker.thread = std::thread([worker = capture->worker_] { worker->run(); });
    } catch (const std::system_error &exception) {
        if (error) *error = QStringLiteral("Could not start DXGI capture worker: %1").arg(
            QString::fromLocal8Bit(exception.what()));
        return {};
    }
    std::unique_lock lock(worker.mutex);
    if (!worker.waitForResult(lock, 1500)) {
        if (error) *error = QStringLiteral("DXGI desktop duplication open timed out");
        return {};
    }
    if (!worker.opened) {
        if (error) *error = worker.error;
        return {};
    }
    return capture;
}

QImage DesktopCapture::read(float sdrWhiteLevel, QString *error) {
    if (error) error->clear();
    // A first frame can arrive before the compositor has drawn anything into the
    // duplicated surface. Retrying is cheap because a still desktop answers with
    // the cached image, and the user sees a correct picture instead of a black
    // one. The last attempt still returns the blank frame: the caller decides
    // whether to fall back to another capture path.
    const int attempts = blank_ ? 1 : 2;
    QImage image;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        image = readFrame(sdrWhiteLevel, attempt ? 150 : 250, error);
        if (image.isNull() || !isBlankFrame(image)) {
            blank_ = false;
            return image;
        }
    }
    blank_ = true;
    return image;
}

QImage DesktopCapture::readFrame(float sdrWhiteLevel, int timeoutMs, QString *error) {
    auto &worker = *worker_;
    std::unique_lock lock(worker.mutex);
    if (!worker.stopping) {
        {
            std::lock_guard settingsLock(captureSettingsMutex);
            worker.beforeAcquire = acquireFrameHookForTesting;
        }
        worker.sdrWhiteLevel = sdrWhiteLevel;
        worker.timeoutMs = timeoutMs;
        worker.error.clear();
        worker.conversion = {};
        worker.ready = false;
        worker.cv.notify_one();
        if (worker.waitForResult(lock, std::max(1500, timeoutMs + 1000))) {
            auto image = std::move(worker.image);
            auto conversion = std::move(worker.conversion);
            if (error) *error = worker.error;
            lock.unlock();
            if (!conversion.isEmpty())
                diagnostics::write(diagnostics::Level::Info, "capture", "Converted advanced-color desktop", conversion);
            return image;
        }
    }
    if (error) *error = QStringLiteral("DXGI desktop duplication read timed out");
    return {};
}

QImage DesktopCapture::State::readFrame(float sdrWhiteLevel, int timeoutMs,
                                       const std::function<void()> &beforeAcquire,
                                       QString *error, QJsonObject *conversion) {
    auto fail = [&](const char *operation, HRESULT hr) -> QImage {
        if (error) *error = failure(operation, hr);
        return {};
    };
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> resource;
    QElapsedTimer elapsed;
    elapsed.start();
    HRESULT hr;
    do {
        if (beforeAcquire) beforeAcquire();
        hr = duplication->AcquireNextFrame(0, &frameInfo, resource.GetAddressOf());
        if (hr != DXGI_ERROR_WAIT_TIMEOUT || elapsed.elapsed() >= timeoutMs) break;
        Sleep(10);
    } while (elapsed.elapsed() < timeoutMs);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT && !lastImage.isNull())
        return lastImage;
    if (FAILED(hr)) return fail("AcquireNextFrame", hr);
    struct ReleaseFrame {
        IDXGIOutputDuplication *duplication;
        ~ReleaseFrame() { duplication->ReleaseFrame(); }
    } release{duplication.Get()};
    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) return fail("Frame texture", hr);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    PixelFormat format;
    if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        format = PixelFormat::ScRgb16;
    else if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && !hdr)
        format = PixelFormat::Bgra8;
    else
        // Once HDR highlights have been clipped into BGRA8, dividing their
        // brightness cannot recover the original colors. Never claim it can.
        return fail("HDR capture requires a floating-point surface", DXGI_ERROR_UNSUPPORTED);
    const QSize size(int(desc.Width), int(desc.Height));
    const QSize rotated = rotation == 90 || rotation == 270 ? size.transposed() : size;
    if (rotated != geometry.size() || qint64(desc.Width) * desc.Height > MaxPixels)
        return fail("Output dimensions changed", DXGI_ERROR_ACCESS_LOST);
    D3D11_TEXTURE2D_DESC stagingDesc{};
    if (staging) staging->GetDesc(&stagingDesc);
    if (!staging || stagingDesc.Width != desc.Width || stagingDesc.Height != desc.Height ||
        stagingDesc.Format != desc.Format) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        staging.Reset();
        hr = device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
        if (FAILED(hr)) return fail("Create staging texture", hr);
    }
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return fail("Map staging texture", hr);
    QImage image = toSrgb(mapped.pData, qsizetype(mapped.RowPitch) * size.height(), size,
                          mapped.RowPitch, format, sdrWhiteLevel, rotation);
    context->Unmap(staging.Get(), 0);
    if (image.isNull()) return fail("Convert captured colors", E_INVALIDARG);
    if (lastImage.isNull())
        *conversion = {{"format", format == PixelFormat::ScRgb16 ? "linear-scRGB-fp16" : "sRGB-bgra8"},
                       {"sdrWhiteLevel", sdrWhiteLevel}, {"hdrOutput", hdr},
                       {"rotation", rotation}, {"adapter", adapter},
                       {"blank", isBlankFrame(image)}};
    lastImage = image;
    return image;
}
} // namespace h2d::windowscapture
