#include "platform.h"
#include <QApplication>
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
    void captureAndAccessibleElement() {
        QWidget window;
        window.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        window.resize(420, 220);
        window.setStyleSheet("QWidget { background: #326ca8; }");
        QPushButton button("Help2Design platform test button", &window);
        button.setAccessibleName("Help2Design platform test button");
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
        probe.setProgram(QCoreApplication::applicationDirPath() + "/Help2Design.exe");
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
