#include "platform.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QTest>
using namespace h2d;
class PlatformTests : public QObject {
    Q_OBJECT
  private slots:
    void editorIsOrdinaryWindow() {
        QWidget editor;
        editor.setWindowFlags(Qt::FramelessWindowHint);
        editor.resize(180, 100);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        configureNativeWindow(&editor, false);
#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(editor.winId());
        QVERIFY(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
        configureNativeWindow(&editor, true);
        QVERIFY(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
        configureNativeWindow(&editor, false);
        QVERIFY(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
#endif
    }
    void preparationDismissesTransientWindowBeforeCapture() {
        class TransientPanel final : public QWidget {
          public:
            bool dismissed = false;
            bool event(QEvent *event) override {
                if (event->type() == QEvent::WindowDeactivate) {
                    dismissed = true;
                    hide();
                }
                return QWidget::event(event);
            }
        } panel;
        panel.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        panel.resize(220, 140);
        panel.show();
        panel.raise();
        panel.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QElapsedTimer elapsed;
        elapsed.start();
        bool completed = false;
        bool dismissedAtCapture = false;
        prepareScreenCapture(this, [&] {
            dismissedAtCapture = panel.dismissed && !panel.isVisible();
            completed = true;
        });
        QVERIFY(!completed);
        QTRY_VERIFY_WITH_TIMEOUT(completed, 2500);
        QVERIFY(dismissedAtCapture);
        QVERIFY(elapsed.elapsed() >= 250);
    }
    void preparationIsCancelledWithOwner() {
        bool completed = false;
        auto owner = new QObject;
        prepareScreenCapture(owner, [&] { completed = true; });
        delete owner;
        QTest::qWait(350);
        QVERIFY(!completed);
    }
    void captureAndAccessibleElement() {
        QWidget window;
        window.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        window.resize(420, 220);
        window.setStyleSheet("QWidget { background: #326ca8; }");
        QPushButton button("HelpDesign platform test button", &window);
        button.setAccessibleName("HelpDesign platform test button");
        button.setGeometry(30, 90, 350, 70);
        auto screen = QGuiApplication::primaryScreen();
        window.move(screen->availableGeometry().topLeft() + QPoint(80, 80));
        window.show();
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(180);
        QVector<ScreenFrame> frames;
        captureScreens([&](QVector<ScreenFrame> images, QString) { frames = std::move(images); });
        QVERIFY(!frames.isEmpty());
        QPoint global = button.mapToGlobal(button.rect().center());
        ScreenFrame frame;
        for (const auto &candidate : frames)
            if (candidate.logicalGeometry.contains(global))
                frame = candidate;
        QVERIFY(!frame.image.isNull());
        QCOMPARE(frame.image.size(), frame.nativeGeometry.size());
        const double sx = double(frame.image.width()) / frame.logicalGeometry.width();
        const double sy = double(frame.image.height()) / frame.logicalGeometry.height();
        auto pixel = [&](QPoint p) {
            return QPoint(qRound((p.x() - frame.logicalGeometry.x()) * sx),
                          qRound((p.y() - frame.logicalGeometry.y()) * sy));
        };
        QColor color = frame.image.pixelColor(pixel(window.mapToGlobal(QPoint(20, 20))));
        QVERIFY2(std::abs(color.red() - 50) < 8 && std::abs(color.green() - 108) < 8 &&
                     std::abs(color.blue() - 168) < 8,
                 qPrintable(color.name()));
        QPoint native = frame.nativeGeometry.topLeft() + pixel(global);
        QProcess probe;
        probe.setProgram(qEnvironmentVariable("H2D_PLATFORM_PROBE",
                                              QCoreApplication::applicationDirPath() + "/HelpDesign.exe"));
        probe.setArguments({"--inspect", QString::number(native.x()), QString::number(native.y()), "0"});
#ifdef Q_OS_WIN
        probe.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
#endif
        probe.start();
        QVERIFY(probe.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(probe.state(), QProcess::NotRunning, 5000);
        QCOMPARE(probe.exitCode(), 0);
        auto output = probe.readAllStandardOutput();
        auto parsed = QJsonDocument::fromJson(output);
        QVERIFY2(parsed.isArray(), output.constData());
        bool found = false;
        for (const auto &value : parsed.array()) {
            auto candidate = value.toObject();
            if (candidate["target"].toObject()["label"] == button.accessibleName()) {
                QVERIFY(containsPixel(jsonRect(candidate["bounds"].toObject()), native));
                found = true;
            }
        }
        QVERIFY2(found, output.constData());
        window.hide();
    }
};
QTEST_MAIN(PlatformTests)
#include "platform_test.moc"
