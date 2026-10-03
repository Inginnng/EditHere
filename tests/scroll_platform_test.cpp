#include "capturetoolbar.h"
#include "platform.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QTest>
#include <QWidget>
#include <cstdio>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#endif

using namespace h2d;

#ifdef Q_OS_WIN
namespace {
constexpr UINT QueryOffset = WM_APP + 41;
constexpr UINT QueryChildEvents = WM_APP + 42;
constexpr UINT QueryRootEvents = WM_APP + 43;
constexpr int FixedBand = 36;
constexpr int DocumentHeight = 2000;
struct FixtureState {
    int offset = 0;
    int childEvents = 0;
    int rootEvents = 0;
    QJsonObject metadata;
    QString readyFile;
    void report(const char *type) const {
        QJsonObject object = metadata;
        object["type"] = type;
        object["offset"] = offset;
        object["childEvents"] = childEvents;
        object["rootEvents"] = rootEvents;
        const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        std::fflush(stdout);
        if (!readyFile.isEmpty()) {
            QFile file(readyFile);
            if (file.open(QIODevice::WriteOnly))
                file.write(json);
        }
    }
};
void fill(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
void paintDocument(HDC dc, const RECT &rect, int offset) {
    fill(dc, rect, RGB(250, 250, 250));
    const int bottom = rect.bottom - FixedBand;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, FixedBand, rect.right, bottom);
    for (int y = FixedBand - offset % 24, row = offset / 24;
         y < bottom; y += 24, ++row) {
        const RECT band{0, y, rect.right, y + 24};
        fill(dc, band, RGB(180 + (row * 19) % 65, 180 + (row * 37) % 65,
                          180 + (row * 43) % 65));
        wchar_t text[100]{};
        swprintf_s(text, L"Document row %03d | native child without WS_VSCROLL", row);
        TextOutW(dc, 12, y + 4, text, int(wcslen(text)));
        // Non-periodic row texture makes gaps and incorrect overlaps visible.
        for (int x = 0; x < 20; ++x) {
            const int bx = 350 + x * 6;
            const RECT block{bx, y + 5, bx + 4, y + 18};
            fill(dc, block, RGB((row * 61 + x * 13) % 255,
                               (row * 7 + x * 31) % 255,
                               (row * 37 + x * 11) % 255));
        }
    }
    RestoreDC(dc, saved);
    fill(dc, RECT{0, 0, rect.right, FixedBand}, RGB(44, 86, 160));
    fill(dc, RECT{0, bottom, rect.right, rect.bottom}, RGB(55, 65, 75));
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 12, 10, L"Fixed document header", 21);
    TextOutW(dc, 12, bottom + 10, L"Fixed document footer", 21);
}
bool saveReference(const QString &path, int width) {
    const int height = DocumentHeight + 2 * FixedBand;
    HDC memory = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(memory);
        return false;
    }
    const auto previous = SelectObject(memory, bitmap);
    paintDocument(memory, RECT{0, 0, width, height}, 0);
    GdiFlush();
    const bool saved = QImage(static_cast<const uchar *>(pixels), width, height, width * 4,
                             QImage::Format_RGB32).save(path);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    return saved;
}
LRESULT CALLBACK fixtureProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<FixtureState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<FixtureState *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);
    const bool child = (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0;
    if (message == QueryOffset)
        return state->offset;
    if (message == QueryChildEvents)
        return state->childEvents;
    if (message == QueryRootEvents)
        return state->rootEvents;
    if (message == WM_MOUSEWHEEL) {
        if (child) {
            ++state->childEvents;
            RECT rect{};
            GetClientRect(window, &rect);
            const int viewport = rect.bottom - 2 * FixedBand;
            state->offset = qBound(0, state->offset - GET_WHEEL_DELTA_WPARAM(wParam) * 72 / WHEEL_DELTA,
                                  DocumentHeight - viewport);
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        } else {
            ++state->rootEvents;
        }
        state->report("offset");
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        fill(dc, rect, RGB(250, 250, 250));
        if (child)
            paintDocument(dc, rect, state->offset);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && !child)
        PostQuitMessage(0);
    return DefWindowProcW(window, message, wParam, lParam);
}
int runFixture(QApplication &app) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = fixtureProc;
    type.hInstance = instance;
    type.lpszClassName = L"EditHere.ScrollCaptureFixture";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    FixtureState state;
    const int width = 520, height = 380;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const QRect desktop(monitor.rcWork.left, monitor.rcWork.top,
                        monitor.rcWork.right - monitor.rcWork.left,
                        monitor.rcWork.bottom - monitor.rcWork.top);
    const int left = desktop.center().x() - width / 2;
    const int top = desktop.center().y() - height / 2;
    HWND root = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, type.lpszClassName,
        L"EditHere scrolling platform fixture", WS_POPUP | WS_CLIPCHILDREN,
        left, top, width, height, nullptr, nullptr, instance, &state);
    if (!root)
        return 2;
    HWND child = CreateWindowExW(0, type.lpszClassName, L"Scrolling page",
        WS_CHILD | WS_VISIBLE, 0, 0, width, height, root, nullptr, instance, &state);
    if (!child)
        return 3;
    ShowWindow(root, SW_SHOWNOACTIVATE);
    UpdateWindow(root);
    UpdateWindow(child);
    state.metadata = QJsonObject{{"window", QString::number(quintptr(root))},
        {"child", QString::number(quintptr(child))}, {"processId", int(GetCurrentProcessId())},
        {"nativeRect", rectJson(QRect(left, top, width, height))},
        {"contentNativeRect", rectJson(QRect(left, top + FixedBand, width, height - 2 * FixedBand))},
        {"documentHeight", DocumentHeight}, {"fixedHeader", FixedBand}, {"fixedFooter", FixedBand}};
    const auto arguments = app.arguments();
    const int referenceIndex = arguments.indexOf("--reference-file");
    if (referenceIndex >= 0 && referenceIndex + 1 < arguments.size() &&
        !saveReference(arguments[referenceIndex + 1], width))
        return 4;
    const int readyIndex = arguments.indexOf("--ready-file");
    if (readyIndex >= 0 && readyIndex + 1 < arguments.size())
        state.readyFile = arguments[readyIndex + 1];
    state.report("ready");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
} // namespace
#endif

class ScrollPlatformTests : public QObject {
    Q_OBJECT
  private slots:
    void capabilityAndInvalidRegion() {
        QString reason;
#ifdef Q_OS_WIN
        QVERIFY(supportsScrollingCapture(&reason));
        QVERIFY(reason.isEmpty());
#else
        QVERIFY(!supportsScrollingCapture(&reason));
        QVERIFY(!reason.isEmpty());
#endif
        bool completed = false;
        captureScrollRegion({}, [&](QImage image, QString error) {
            QVERIFY(image.isNull());
            QVERIFY(!error.isEmpty());
            completed = true;
        });
        QTRY_VERIFY(completed);
        QVERIFY(!scrollCaptureStep({}, {}, 1, &reason));
        QVERIFY(!reason.isEmpty());
    }
    void scrollsExternalChildThroughOverlayAndCapturesNativePixels() {
#ifdef Q_OS_WIN
        QProcess fixture;
        fixture.setProgram(QCoreApplication::applicationFilePath());
        fixture.setArguments({"--scroll-fixture"});
        fixture.start();
        QVERIFY(fixture.waitForStarted());
        QVERIFY(fixture.waitForReadyRead(5000));
        const QByteArray output = fixture.readLine();
        const auto ready = QJsonDocument::fromJson(output).object();
        QVERIFY2(!ready.isEmpty(), output.constData());
        const auto targetWindow = ready["window"].toString().toULongLong();
        const auto hwnd = reinterpret_cast<HWND>(quintptr(targetWindow));
        struct CloseFixture {
            HWND window;
            QProcess &process;
            ~CloseFixture() {
                if (IsWindow(window))
                    PostMessageW(window, WM_CLOSE, 0, 0);
                if (process.state() != QProcess::NotRunning)
                    process.waitForFinished(5000);
            }
        } close{hwnd, fixture};
        const QRect region = jsonRect(ready["nativeRect"].toObject());
        const QPoint nativePoint = region.center();
        const auto target = scrollCaptureTargetAt(nativePoint);
        QCOMPARE(target.window, quintptr(targetWindow));
        QCOMPARE(target.processId, quint32(fixture.processId()));
        QVERIFY(!(GetWindowLongPtrW(reinterpret_cast<HWND>(quintptr(ready["child"].toString().toULongLong())),
                                    GWL_STYLE) & WS_VSCROLL));
        QString error;
        QImage before, after;
        captureScrollRegion(region, [&](QImage image, QString failure) { before = image; error = failure; });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(before.size(), region.size());
        QCOMPARE(before.devicePixelRatio(), 1.0);
        const QColor header(44, 86, 160);
        QCOMPARE(before.pixelColor(3, 3), header);

        // This is the previous failure: the native target remains underneath our
        // topmost frozen capture window. Wheel input must reach the external child.
        QWidget overlay;
        overlay.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        overlay.setStyleSheet("background:#ee00dd");
        auto screen = QGuiApplication::primaryScreen();
        const double scale = screen->devicePixelRatio();
        overlay.setGeometry(qRound(region.x() / scale), qRound(region.y() / scale),
                            qRound(region.width() / scale), qRound(region.height() / scale));
        overlay.show();
        configureNativeWindow(&overlay, true);
        SetWindowPos(reinterpret_cast<HWND>(overlay.winId()), HWND_TOPMOST, region.x(), region.y(),
                     region.width(), region.height(), SWP_NOACTIVATE);
        QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        const HWND pointWindow = WindowFromPoint(POINT{nativePoint.x(), nativePoint.y()});
        QCOMPARE(GetAncestor(pointWindow, GA_ROOT), reinterpret_cast<HWND>(overlay.winId()));
        const auto underOverlay = scrollCaptureTargetAt(nativePoint);
        QCOMPARE(underOverlay.window, target.window);
        QCOMPARE(underOverlay.processId, target.processId);
        POINT cursorBefore{}, cursorAfter{};
        GetCursorPos(&cursorBefore);
        QVERIFY2(scrollCaptureStep(target, nativePoint, 1, &error), qPrintable(error));
        GetCursorPos(&cursorAfter);
        QCOMPARE(cursorAfter.x, cursorBefore.x);
        QCOMPARE(cursorAfter.y, cursorBefore.y);
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(72));
        QCOMPARE(SendMessageW(hwnd, QueryChildEvents, 0, 0), LRESULT(1));
        QCOMPARE(SendMessageW(hwnd, QueryRootEvents, 0, 0), LRESULT(0));
        overlay.hide(); // Capturing hidden controls also works on older Windows.
        captureScrollRegion(region, [&](QImage image, QString failure) { after = image; error = failure; });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(after.pixelColor(3, 3), header);
        QCOMPARE(after.copy(0, FixedBand, region.width(), region.height() - 2 * FixedBand - 72),
                 before.copy(0, FixedBand + 72, region.width(), region.height() - 2 * FixedBand - 72));

        // The live selection must remain visible without becoming the native
        // mouse target. In particular, Chromium resolves wheel input using
        // WindowFromPoint even after it was sent to the render child directly.
        ScrollCaptureRegion liveRegion;
        const QRect logicalRegion(qRound(region.x() / scale), qRound(region.y() / scale),
                                  qRound(region.width() / scale), qRound(region.height() / scale));
        liveRegion.setSelection(logicalRegion, screen->geometry());
        liveRegion.setState(true, Qt::Vertical);
        liveRegion.show();
        configureNativeWindow(&liveRegion, true);
        QVERIFY(QTest::qWaitForWindowExposed(&liveRegion));
        const HWND windowThroughFrame = WindowFromPoint(POINT{nativePoint.x(), nativePoint.y()});
        QCOMPARE(GetAncestor(windowThroughFrame, GA_ROOT), hwnd);
        QCOMPARE(scrollCaptureTargetAt(nativePoint).window, target.window);
        QCOMPARE(scrollCaptureTargetAt(nativePoint).processId, target.processId);
        QVERIFY(liveRegion.isVisible());

        QImage throughFrame;
        captureScrollRegion(region, [&](QImage image, QString failure) {
            throughFrame = image;
            error = failure;
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(throughFrame, after);
        QVERIFY(liveRegion.isVisible());

        // Both wheel signs are meaningful: upward scrolling is delivered to
        // the same external child and its native pixels return to the origin.
        GetCursorPos(&cursorBefore);
        QVERIFY2(scrollCaptureStep(target, nativePoint, -1, &error), qPrintable(error));
        GetCursorPos(&cursorAfter);
        QCOMPARE(cursorAfter.x, cursorBefore.x);
        QCOMPARE(cursorAfter.y, cursorBefore.y);
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(0));
        QCOMPARE(SendMessageW(hwnd, QueryChildEvents, 0, 0), LRESULT(2));
        QCOMPARE(SendMessageW(hwnd, QueryRootEvents, 0, 0), LRESULT(0));
        QImage returned;
        captureScrollRegion(region, [&](QImage image, QString failure) {
            returned = image;
            error = failure;
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(returned, before);

        // Keep the border inside the fixture's stable painted surface. Sampling
        // the desktop just outside the fixture would make this check depend on
        // unrelated windows and DWM's continuously changing background.
        liveRegion.setSelection(logicalRegion.adjusted(8, 8, -8, -8), screen->geometry());
        QTest::qWait(40);
        // A hollow frame does not cover the selected pixels. When Windows
        // supports display affinity, its painted border must also be absent
        // from a larger readout crossing the frame itself.
        DWORD frameAffinity = 0;
        const bool hasFrameAffinity = GetWindowDisplayAffinity(
            reinterpret_cast<HWND>(liveRegion.winId()), &frameAffinity);
        QCOMPARE(excludedFromCapture(&liveRegion), hasFrameAffinity && frameAffinity == 0x00000011);
        if (excludedFromCapture(&liveRegion)) {
            const QRect logicalFrame = liveRegion.geometry();
            const QRect nativeFrame(qRound(logicalFrame.x() * scale), qRound(logicalFrame.y() * scale),
                                    qRound(logicalFrame.width() * scale), qRound(logicalFrame.height() * scale));
            QImage visibleFrameRead, hiddenFrameRead;
            captureScrollRegion(nativeFrame, [&](QImage image, QString failure) {
                visibleFrameRead = image;
                error = failure;
            });
            QVERIFY2(error.isEmpty(), qPrintable(error));
            liveRegion.hide();
            captureScrollRegion(nativeFrame, [&](QImage image, QString failure) {
                hiddenFrameRead = image;
                error = failure;
            });
            QVERIFY2(error.isEmpty(), qPrintable(error));
            bool borderContaminated = visibleFrameRead.size() != hiddenFrameRead.size();
            QPoint firstMismatch;
            for (int row = 0; !borderContaminated && row < visibleFrameRead.height(); ++row) {
                for (int column = 0; column < visibleFrameRead.width(); ++column) {
                    const QColor visible = visibleFrameRead.pixelColor(column, row);
                    const QColor hidden = hiddenFrameRead.pixelColor(column, row);
                    if (std::abs(visible.red() - hidden.red()) > 2 ||
                        std::abs(visible.green() - hidden.green()) > 2 ||
                        std::abs(visible.blue() - hidden.blue()) > 2) {
                        borderContaminated = true;
                        firstMismatch = {column, row};
                        break;
                    }
                }
            }
            if (borderContaminated) {
                const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
                if (!folder.isEmpty()) {
                    QDir().mkpath(folder);
                    visibleFrameRead.save(QDir(folder).filePath("scroll-visible-frame.png"));
                    hiddenFrameRead.save(QDir(folder).filePath("scroll-hidden-frame.png"));
                }
                qWarning() << "First exclusion mismatch" << firstMismatch
                           << visibleFrameRead.pixelColor(firstMismatch)
                           << hiddenFrameRead.pixelColor(firstMismatch);
            }
            QVERIFY2(!borderContaminated, "The live selection border entered the captured pixels.");
        } else {
            liveRegion.hide();
        }

        // Retain the existing invalid-target and occlusion checks at a known
        // nonzero offset after proving the upward path above.
        QVERIFY2(scrollCaptureStep(target, nativePoint, 1, &error), qPrintable(error));
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(72));

        auto wrongOwner = target;
        ++wrongOwner.processId;
        QVERIFY(!scrollCaptureStep(wrongOwner, nativePoint, 1, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(72));
        QVERIFY(!scrollCaptureStep(target, nativePoint, 0, &error));
        QVERIFY(!scrollCaptureStep(target, nativePoint, 9, &error));
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(72));

        // A separate topmost application occluding the locked target must stop
        // capture rather than silently delivering input to that new application.
        QProcess occluder;
        occluder.setProgram(QCoreApplication::applicationFilePath());
        occluder.setArguments({"--scroll-fixture"});
        occluder.start();
        QVERIFY(occluder.waitForStarted());
        QVERIFY(occluder.waitForReadyRead(5000));
        const auto covered = QJsonDocument::fromJson(occluder.readLine()).object();
        QVERIFY(!covered.isEmpty());
        const HWND coveringWindow = reinterpret_cast<HWND>(
            quintptr(covered["window"].toString().toULongLong()));
        CloseFixture closeOccluder{coveringWindow, occluder};
        QCOMPARE(scrollCaptureTargetAt(nativePoint).window, quintptr(coveringWindow));
        QVERIFY(!scrollCaptureStep(target, nativePoint, 1, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(SendMessageW(hwnd, QueryOffset, 0, 0), LRESULT(72));
        QCOMPARE(SendMessageW(coveringWindow, QueryOffset, 0, 0), LRESULT(0));
        PostMessageW(coveringWindow, WM_CLOSE, 0, 0);
        QTRY_COMPARE_WITH_TIMEOUT(occluder.state(), QProcess::NotRunning, 5000);

        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.state(), QProcess::NotRunning, 5000);
        QVERIFY(!scrollCaptureStep(target, nativePoint, 1, &error));
        QVERIFY(!error.isEmpty());
#else
        QSKIP("The native scrolling backend is currently Windows only.");
#endif
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (app.arguments().contains("--scroll-fixture"))
        return runFixture(app);
#endif
    ScrollPlatformTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "scroll_platform_test.moc"
