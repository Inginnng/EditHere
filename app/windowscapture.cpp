#include "windowscapture.h"
#include "model.h"
#include "diagnostics.h"
#include <QColorSpace>
#include <QTransform>
#include <qfloat16.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace h2d::windowscapture {
using Microsoft::WRL::ComPtr;
namespace {
QString failure(const char *operation, HRESULT hr) {
    return QString("%1: 0x%2").arg(QLatin1String(operation),
        QString::number(quint32(hr), 16).rightJustified(8, '0'));
}
QRect rectangle(const RECT &rect) {
    return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
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
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    QRect geometry;
    QString adapter;
    int rotation = 0;
    bool hdr = false;
    // Remembers that this duplication answers with blank frames so a long
    // capture does not pay the retry timeout for every single read.
    bool blank = false;
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
DesktopCapture::DesktopCapture(std::unique_ptr<State> state) : state_(std::move(state)) {}
DesktopCapture::~DesktopCapture() = default;
QRect DesktopCapture::geometry() const { return state_->geometry; }

std::unique_ptr<DesktopCapture> DesktopCapture::open(HMONITOR monitor, QString *error) {
    if (error) error->clear();
    auto fail = [&](const char *operation, HRESULT hr) -> std::unique_ptr<DesktopCapture> {
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
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM};
            hr = output5->DuplicateOutput1(state->device.Get(), 0, UINT(std::size(formats)), formats,
                                          state->duplication.GetAddressOf());
            if (FAILED(hr)) return fail("DuplicateOutput1", hr);
            return std::unique_ptr<DesktopCapture>(new DesktopCapture(std::move(state)));
        }
    }
    return fail("Monitor output unavailable", DXGI_ERROR_NOT_FOUND);
}

QImage DesktopCapture::read(float sdrWhiteLevel, QString *error) {
    if (error) error->clear();
    // A first frame can arrive before the compositor has drawn anything into the
    // duplicated surface. Retrying is cheap because a still desktop answers with
    // the cached image, and the user sees a correct picture instead of a black
    // one. The last attempt still returns the blank frame: the caller decides
    // whether to fall back to another capture path.
    const int attempts = state_->blank ? 1 : 2;
    QImage image;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        image = readFrame(sdrWhiteLevel, attempt ? 150 : 250, error);
        if (image.isNull() || !isBlankFrame(image)) {
            state_->blank = false;
            return image;
        }
    }
    state_->blank = true;
    return image;
}

QImage DesktopCapture::readFrame(float sdrWhiteLevel, int timeoutMs, QString *error) {
    if (error) error->clear();
    auto fail = [&](const char *operation, HRESULT hr) -> QImage {
        if (error) *error = failure(operation, hr);
        return {};
    };
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = state_->duplication->AcquireNextFrame(UINT(timeoutMs), &frameInfo, resource.GetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT && !state_->lastImage.isNull())
        return state_->lastImage;
    if (FAILED(hr)) return fail("AcquireNextFrame", hr);
    struct ReleaseFrame {
        IDXGIOutputDuplication *duplication;
        ~ReleaseFrame() { duplication->ReleaseFrame(); }
    } release{state_->duplication.Get()};
    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) return fail("Frame texture", hr);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    PixelFormat format;
    if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        format = PixelFormat::ScRgb16;
    else if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && !state_->hdr)
        format = PixelFormat::Bgra8;
    else
        // Once HDR highlights have been clipped into BGRA8, dividing their
        // brightness cannot recover the original colors. Never claim it can.
        return fail("HDR capture requires a floating-point surface", DXGI_ERROR_UNSUPPORTED);
    const QSize size(int(desc.Width), int(desc.Height));
    const QSize rotated = state_->rotation == 90 || state_->rotation == 270 ? size.transposed() : size;
    if (rotated != state_->geometry.size() || qint64(desc.Width) * desc.Height > MaxPixels)
        return fail("Output dimensions changed", DXGI_ERROR_ACCESS_LOST);
    D3D11_TEXTURE2D_DESC stagingDesc{};
    if (state_->staging) state_->staging->GetDesc(&stagingDesc);
    if (!state_->staging || stagingDesc.Width != desc.Width || stagingDesc.Height != desc.Height ||
        stagingDesc.Format != desc.Format) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        state_->staging.Reset();
        hr = state_->device->CreateTexture2D(&desc, nullptr, state_->staging.GetAddressOf());
        if (FAILED(hr)) return fail("Create staging texture", hr);
    }
    state_->context->CopyResource(state_->staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = state_->context->Map(state_->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return fail("Map staging texture", hr);
    QImage image = toSrgb(mapped.pData, qsizetype(mapped.RowPitch) * size.height(), size,
                          mapped.RowPitch, format, sdrWhiteLevel, state_->rotation);
    state_->context->Unmap(state_->staging.Get(), 0);
    if (image.isNull()) return fail("Convert captured colors", E_INVALIDARG);
    if (state_->lastImage.isNull())
        diagnostics::write(diagnostics::Level::Info, "capture", "Converted advanced-color desktop",
            {{"format", format == PixelFormat::ScRgb16 ? "linear-scRGB-fp16" : "sRGB-bgra8"},
             {"sdrWhiteLevel", sdrWhiteLevel}, {"hdrOutput", state_->hdr},
             {"rotation", state_->rotation}, {"adapter", state_->adapter},
             {"blank", isBlankFrame(image)}});
    state_->lastImage = image;
    return image;
}
} // namespace h2d::windowscapture
