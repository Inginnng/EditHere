#include "controller.h"
#include "ui.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
class HelpDesignApplication final : public QApplication {
  public:
    using QApplication::QApplication;
    std::function<void(const QString &)> openProject;
  protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::FileOpen) {
            auto request=static_cast<QFileOpenEvent *>(event);
            const QString path=request->file().isEmpty() && request->url().isLocalFile() ? request->url().toLocalFile() : request->file();
            if(!path.isEmpty() && openProject) {openProject(path);return true;}
        }
        return QApplication::event(event);
    }
};
int main(int argc, char **argv) {
    using namespace h2d;
#ifdef Q_OS_WIN
    using DpiFunction = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    auto dpiFunction = reinterpret_cast<DpiFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (dpiFunction)
        dpiFunction(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    if (argc >= 4 && QByteArray(argv[1]) == "--inspect") {
        QCoreApplication app(argc, argv);
        bool xOk = false, yOk = false;
        int x = QString::fromLocal8Bit(argv[2]).toInt(&xOk), y = QString::fromLocal8Bit(argv[3]).toInt(&yOk);
        if (!xOk || !yOk)
            return 2;
        qint64 excluded = argc >= 5 ? QString::fromLocal8Bit(argv[4]).toLongLong() : 0;
        QJsonArray result;
        for (const auto &c : nativeElementsAt({x, y}, excluded))
            result.append(QJsonObject{{"bounds", rectJson(c.bounds)}, {"target", c.target}});
        QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Compact);
        std::fwrite(output.constData(), 1, size_t(output.size()), stdout);
        return 0;
    }

    HelpDesignApplication app(argc, argv);
    // Keep the previous local lock and IPC address so an already running version
    // remains the sole owner of screenshots and unsaved feedback.
    app.setApplicationName("Help2Design");
    app.setOrganizationName("Help2Design");
    const QString state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    app.setApplicationName("HelpDesign");
    app.setOrganizationName("HelpDesign");
    app.setApplicationVersion(HELPDESIGN_VERSION);
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(glyph("capture", accent()));
    applyTheme(loadSettings().theme);
    QDir().mkpath(state);
    QLockFile lock(QDir(state).filePath("native.lock"));
    lock.setStaleLockTime(0);
    const QString serverName = "Help2Design-native-" + QString::number(qHash(state));
    const QStringList args = app.arguments();
    QString path;
    for (int i = 1; i < args.size(); i++)
        if (!args[i].startsWith("--")) {
            path = QFileInfo(args[i]).absoluteFilePath();
            break;
        }
    if (!lock.tryLock(0)) {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(800)) {
            socket.write(path.isEmpty() ? QByteArray("capture") : path.toUtf8());
            socket.flush();
            socket.waitForBytesWritten(500);
        }
        return 0;
    }
    QLocalServer server;
    QLocalServer::removeServer(serverName);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(serverName);
    Controller controller;
    app.openProject=[&controller](const QString &path) {controller.start(false,path);};
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &app, [&, socket] {
                auto text = QString::fromUtf8(socket->readAll());
                if (text == "capture")
                    controller.capture();
                else
                    controller.start(false, text);
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QTimer::singleShot(0, &app, [&] { controller.start(args.contains("--demo"), path); });
    return app.exec();
}
