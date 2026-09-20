#include "controller.h"
#include "agentserver.h"
#include "autostart.h"
#include "ui.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
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
class EditHereApplication final : public QApplication {
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

    initializeLaunchAtLoginDetection();
    EditHereApplication app(argc, argv);
    // Keep the previous local lock and IPC address so an already running version
    // remains the sole owner of screenshots and unsaved feedback.
    app.setApplicationName("Help2Design");
    app.setOrganizationName("Help2Design");
    const QString state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    app.setApplicationName("HelpDesign");
    app.setOrganizationName("HelpDesign");
    // Keep the previous settings directory; only the public display name changes.
    app.setApplicationDisplayName("EditHere · 改这里");
    app.setApplicationVersion(HELPDESIGN_VERSION);
    app.setQuitOnLastWindowClosed(false);
    QIcon appIcon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QString(":/icons/helpdesign-%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(appIcon);
    applyTheme(loadSettings().theme);
    QDir().mkpath(state);
    QLockFile lock(QDir(state).filePath("native.lock"));
    lock.setStaleLockTime(0);
    const QString serverName = "Help2Design-native-" + QString::number(qHash(state));
    const QStringList args = app.arguments();
    const bool agentStart = args.contains("--agent-start");
    const bool background = args.contains("--autostart") || agentStart;
    QString path;
    for (int i = 1; i < args.size(); i++)
        if (!args[i].startsWith("--")) {
            path = QFileInfo(args[i]).absoluteFilePath();
            break;
        }
    if (!lock.tryLock(0)) {
        if ((background || wasLaunchedAtLogin()) && path.isEmpty())
            return 0;
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
    AgentServer agentServer(controller, &app);
    agentServer.start();
    app.openProject=[&controller](const QString &path) {controller.start(false,path);};
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto socket = server.nextPendingConnection()) {
            // Legacy launchers delimit requests by closing the connection. Buffer
            // until EOF so split local-socket writes never become partial paths.
            socket->setReadBufferSize(65536);
            QObject::connect(socket, &QLocalSocket::disconnected, &app, [&, socket] {
                const auto text = QString::fromUtf8(socket->readAll());
                if (text == "capture") controller.capture();
                else if (!text.isEmpty()) controller.start(false, text);
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QTimer::singleShot(0, &app, [&] { controller.start(args.contains("--demo"), path, background || wasLaunchedAtLogin(), !agentStart && !hasSeenGuide()); });
    return app.exec();
}
