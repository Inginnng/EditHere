#include "controller.h"
#include "agentconnection.h"
#include "agentserver.h"
#include "autostart.h"
#include "diagnostics.h"
#include "i18n.h"
#include "ui.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QMessageBox>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QScreen>
#include <QThread>
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
    app.setApplicationName("EditHere");
#ifdef Q_OS_LINUX
    app.setDesktopFileName("com.edithere.capture");
#endif
    app.setOrganizationName("EditHere");
    const QString state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    app.setApplicationDisplayName(QCoreApplication::translate("h2d", "EditHere · 改这里"));
    app.setApplicationVersion(EDITHERE_VERSION);
    QString logError;
    if (!diagnostics::start({}, &logError))
        std::fprintf(stderr, "EditHere diagnostics: %s\n", qPrintable(logError));
    struct LogLifetime {
        ~LogLifetime() { diagnostics::stop(); }
    } logLifetime;
    app.setQuitOnLastWindowClosed(false);
    QIcon appIcon;
    for (const int size : {16, 20, 24, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QString(":/icons/edithere-%1.png").arg(size), QSize(size, size));
    app.setWindowIcon(appIcon);
    const auto startupSettings = loadSettings();
    applyTheme(startupSettings.theme);
    // The translators have to be installed before any widget exists, because every
    // label is created in a C++ constructor. Later changes are applied live.
    installLanguage(startupSettings.language);
    if (!QDir().mkpath(state)) {
        diagnostics::write(diagnostics::Level::Error, "startup.state_directory", "Unable to create application state directory");
        QMessageBox::critical(nullptr, "EditHere", QCoreApplication::translate("h2d", "无法创建程序数据目录，请检查当前用户的目录权限。"));
        return 2;
    }
    QLockFile lock(QDir(state).filePath("native.lock"));
    lock.setStaleLockTime(0);
    const QString serverName = legacyDesktopServerName();
    const QStringList args = app.arguments();
    const bool agentStart = args.contains("--agent-start");
    const bool background = args.contains("--autostart") || agentStart;
    QJsonArray screens;
    for (auto screen : QGuiApplication::screens())
        screens.append(QJsonObject{{"width", screen->geometry().width()}, {"height", screen->geometry().height()},
                                   {"scale", screen->devicePixelRatio()}});
    diagnostics::write(diagnostics::Level::Info, "startup.runtime", "Desktop runtime initialized",
        {{"platform", QGuiApplication::platformName()}, {"screens", screens},
         {"background", background}, {"agentStart", agentStart}});
    QString path;
    for (int i = 1; i < args.size(); i++)
        if (!args[i].startsWith("--")) {
            path = QFileInfo(args[i]).absoluteFilePath();
            break;
        }
    const bool quitRequest = args.contains("--quit");
    if (quitRequest) {
        // Ask the running instance to go through its normal exit path, which
        // still prompts for unsaved annotations. Exit 0 once it is gone, 2 when
        // it is still holding the lock (the caller keeps waiting).
        if (lock.tryLock(0)) {
            lock.unlock();
            return 0;
        }
        if (lock.error() != QLockFile::LockFailedError) {
            diagnostics::write(diagnostics::Level::Error, "startup.quit_lock", "Cannot access the running instance lock",
                               {{"lockError", int(lock.error())}});
            return 2;
        }
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(800)) {
            socket.write("quit");
            socket.flush();
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
        } else {
            diagnostics::write(diagnostics::Level::Warning, "startup.quit_request", socket.errorString());
        }
        for (int i = 0; i < 150; ++i) {
            if (lock.tryLock(0)) {
                lock.unlock();
                return 0;
            }
            QThread::msleep(100);
        }
        diagnostics::write(diagnostics::Level::Warning, "startup.quit_timeout", "Running instance did not finish its normal exit");
        return 2;
    }
    if (!lock.tryLock(0)) {
        if (lock.error() != QLockFile::LockFailedError) {
            diagnostics::write(diagnostics::Level::Error, "startup.lock", "Unable to create application instance lock",
                               {{"lockError", int(lock.error())}});
            QMessageBox::critical(nullptr, "EditHere", QCoreApplication::translate("h2d", "无法创建程序锁，请检查程序数据目录的权限。"));
            return 2;
        }
        if ((background || wasLaunchedAtLogin()) && path.isEmpty())
            return 0;
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(800)) {
            socket.write(path.isEmpty() ? QByteArray("capture") : path.toUtf8());
            socket.flush();
            if (socket.bytesToWrite() && !socket.waitForBytesWritten(500)) {
                diagnostics::write(diagnostics::Level::Error, "startup.forward", socket.errorString());
                return 2;
            }
        } else {
            diagnostics::write(diagnostics::Level::Error, "startup.forward", socket.errorString());
            QMessageBox::warning(nullptr, "EditHere", QCoreApplication::translate("h2d", "无法连接正在运行的 EditHere，请退出后重试。"));
            return 2;
        }
        return 0;
    }
    QLocalServer server;
    QLocalServer::removeServer(serverName);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) {
        diagnostics::write(diagnostics::Level::Error, "startup.desktop_server", server.errorString());
        QMessageBox::critical(nullptr, "EditHere", QCoreApplication::translate("h2d", "无法启动程序通信服务，请退出其他 EditHere 实例后重试。"));
        return 2;
    }
    Controller controller;
    AgentServer agentServer(controller, &app);
    if (!agentServer.start())
        diagnostics::write(diagnostics::Level::Error, "startup.agent_server", agentServer.errorString());
    app.openProject=[&controller](const QString &path) {controller.start(false,path);};
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto socket = server.nextPendingConnection()) {
            receiveDesktopRequest(*socket, controller, [&controller](const QString &text) {
                if (text == "capture") controller.capture();
                else if (text == "quit") controller.quit();
                else if (!text.isEmpty()) controller.start(false, text);
            });
        }
    });
    QTimer::singleShot(0, &app, [&] {
        controller.start(args.contains("--demo"), path, background || wasLaunchedAtLogin(), !agentStart && !hasSeenGuide());
        if (args.contains("--capture")) controller.capture();
    });
    return app.exec();
}
