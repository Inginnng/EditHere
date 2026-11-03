#include "agentconnection.h"
#include "agentprotocol.h"
#include "localauth.h"
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <memory>
namespace h2d {
namespace {
bool missing(const AgentSocketResult &result) {
    return !result.connected && result.error == QLocalSocket::ServerNotFoundError;
}
QJsonObject diagnostic(const AgentSocketResult &result, AgentEndpoint endpoint, const QString &phase) {
    const char *name = "UnknownSocketError";
    switch (result.error) {
    case QLocalSocket::ConnectionRefusedError: name = "ConnectionRefusedError"; break;
    case QLocalSocket::PeerClosedError: name = "PeerClosedError"; break;
    case QLocalSocket::ServerNotFoundError: name = "ServerNotFoundError"; break;
    case QLocalSocket::SocketAccessError: name = "SocketAccessError"; break;
    case QLocalSocket::SocketResourceError: name = "SocketResourceError"; break;
    case QLocalSocket::SocketTimeoutError: name = "SocketTimeoutError"; break;
    case QLocalSocket::DatagramTooLargeError: name = "DatagramTooLargeError"; break;
    case QLocalSocket::ConnectionError: name = "ConnectionError"; break;
    case QLocalSocket::UnsupportedSocketOperationError: name = "UnsupportedSocketOperationError"; break;
    case QLocalSocket::OperationError: name = "OperationError"; break;
    case QLocalSocket::UnknownSocketError: break;
    }
    return {{"phase", phase}, {"endpoint", endpoint == AgentEndpoint::Agent ? "agent" : "desktop"},
            {"socketError", int(result.error)}, {"socketErrorName", QString::fromLatin1(name)},
            {"message", result.message}};
}
QJsonObject endpointUnavailable(const AgentSocketResult &result) {
    auto response = agentError("agent_endpoint_unavailable",
        "EditHere is running, but its Agent endpoint is unavailable. It may still be starting or use an older version. "
        "Wait briefly and retry; if it persists, save your edits before updating or restarting EditHere.");
    response["running"] = true;
    response["connection"] = diagnostic(result, AgentEndpoint::Agent, "connect");
    return response;
}
}
QString legacyDesktopServerName() {
#ifndef Q_OS_WIN
    return QDir(ipcRuntimeDirectory()).filePath("d");
#else
    return "EditHere-native-v2-" + QString::number(qHash(agentStateLocation()));
#endif
}
QString previousDesktopServerName() {
    // Shares agentStateLocation() with the Agent endpoint: both names have to be reached from
    // any process identity, not just from the GUI that listens on them.
    const auto state = agentStateLocation();
    return "EditHere-native-" + QString::number(qHash(state));
}
QByteArray encodeDesktopRequest(const QString &text, const QByteArray &token) {
    return encodeAgentMessage({{"protocol", 1}, {"token", QString::fromLatin1(token)}, {"request", text}});
}
QString sendDesktopRequest(QLocalSocket &socket, const QString &text, const QByteArray &token, int timeoutMs) {
    if (const auto error = ipcPeerError(socket); !error.isEmpty()) return error;
    socket.setReadBufferSize(MaxAgentMessageBytes + 4);
    const auto bytes = encodeDesktopRequest(text, token);
    if (socket.write(bytes) != bytes.size()) return socket.errorString();
    socket.flush();
    QElapsedTimer elapsed; elapsed.start();
    QByteArray response;
    while (elapsed.elapsed() < timeoutMs) {
        response += socket.readAll();
        if (response.size() > MaxAgentMessageBytes + 4) return "Desktop reply exceeds the supported size.";
        QJsonObject reply; QString error;
        const auto state = takeAgentMessage(response, reply, error);
        if (state == AgentFrameState::Invalid) return error;
        if (state == AgentFrameState::Complete && !response.isEmpty()) return "Expected one desktop reply.";
        if (state == AgentFrameState::Complete)
            return reply["ok"].toBool() ? QString() : reply["error"].toObject()["message"].toString("Desktop request was refused.");
        if (socket.state() == QLocalSocket::UnconnectedState) return "Desktop connection closed before authenticating the request.";
        socket.waitForReadyRead(qMin(500, qMax(1, timeoutMs - int(elapsed.elapsed()))));
    }
    return "Desktop request authentication timed out.";
}
void receiveDesktopRequest(QLocalSocket &socket, QObject &receiver,
                           const QByteArray &token,
                           const std::function<void(const QString &)> &dispatch, int timeoutMs) {
    constexpr qsizetype maximumBytes = MaxAgentMessageBytes + 4;
    socket.setReadBufferSize(maximumBytes + 1);
    if (!ipcPeerError(socket, true).isEmpty()) { socket.abort(); socket.deleteLater(); return; }
    struct State { QByteArray bytes; bool finished = false; };
    auto state = std::make_shared<State>();
    auto deadline = new QTimer(&socket);
    deadline->setSingleShot(true);
    auto collect = [&socket, state, deadline, token, dispatch] {
        if (state->finished || !socket.isOpen()) return;
        state->bytes += socket.readAll();
        if (state->bytes.size() > maximumBytes) {
            state->finished = true;
            state->bytes.clear();
            socket.abort();
            return;
        }
        QJsonObject request;
        QString error;
        const auto frame = takeAgentMessage(state->bytes, request, error);
        if (frame == AgentFrameState::Incomplete) return;
        state->finished = true;
        deadline->stop();
        QJsonObject reply;
        if (frame == AgentFrameState::Invalid || !state->bytes.isEmpty() || request["protocol"].toInt() != 1 || !request["request"].isString())
            reply = agentError("protocol_error", "Expected one authenticated desktop request.");
        else if (!ipcTokenMatches(token, request["token"].toString()))
            reply = agentError("authentication_required", "A valid IPC credential for this desktop session is required.");
        else reply = {{"ok", true}, {"accepted", true}};
        socket.write(encodeAgentMessage(reply));
        socket.flush();
        if (socket.bytesToWrite()) socket.waitForBytesWritten(500);
        socket.disconnectFromServer();
        if (reply["ok"].toBool()) dispatch(request["request"].toString());
    };
    QObject::connect(&socket, &QLocalSocket::readyRead, &socket, collect);
    QObject::connect(deadline, &QTimer::timeout, &socket, [&socket, state] {
        state->finished = true;
        state->bytes.clear();
        socket.abort();
        socket.deleteLater();
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &receiver, [state, deadline] {
        state->finished = true;
        deadline->stop();
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &socket, &QObject::deleteLater);
    deadline->start(qMax(1, timeoutMs));
    // The peer can write before nextPendingConnection() is called.
    if (socket.bytesAvailable()) collect();
    if (socket.state() == QLocalSocket::UnconnectedState) {
        socket.deleteLater();
    }
}
AgentSocketResult connectAgentSocket(QLocalSocket &socket, const QString &name, int timeout) {
    socket.abort();
    socket.connectToServer(name);
    // Capture the error before aborting/reusing the socket; it describes this attempt only.
    if (socket.state() == QLocalSocket::ConnectedState || socket.waitForConnected(timeout))
        return {true, QLocalSocket::UnknownSocketError, {}};
    return {false, socket.error(), socket.errorString()};
}
QJsonObject agentConnectionError(const AgentSocketResult &result, AgentEndpoint endpoint,
                                const QString &phase, const QString &context) {
    const bool denied = result.error == QLocalSocket::SocketAccessError;
    const auto message = denied
        ? QString("The current execution environment cannot access EditHere's desktop connection. "
                  "Use the host tool's authorized desktop execution context for the same signed-in user. "
                  "This does not mean EditHere is stopped; do not restart it or weaken its IPC permissions.")
        : QString("Cannot determine EditHere's availability because its local connection failed. "
                  "Check the connection details before retrying; this does not mean the app is stopped.");
    auto response = agentError(denied ? "desktop_access_required" : "connection_error",
                               context.isEmpty() ? message : context + " " + message);
    response["running"] = QJsonValue::Null;
    response["connection"] = diagnostic(result, endpoint, phase);
    return response;
}
QJsonObject ensureAgentConnection(bool mayStart, const QString &version,
                                 const AgentConnectAttempt &connect, const AgentLaunchAttempt &launch,
                                 int startupTimeoutMs) {
    auto result = connect(AgentEndpoint::Agent, 400);
    if (result.connected) return {};
    if (!missing(result)) return agentConnectionError(result, AgentEndpoint::Agent, "connect");

    // The desktop listener may precede the Agent listener during GUI startup.
    // Probes send no data, including when the caller detects an older GUI.
    const auto desktop = connect(AgentEndpoint::Desktop, 400);
    if (desktop.connected) {
        // main() publishes the desktop listener before constructing the UI and Agent server.
        // Missing endpoints fail immediately, so wait explicitly for that initialization gap.
        QElapsedTimer initializing;
        initializing.start();
        do {
            result = connect(AgentEndpoint::Agent, 150);
            if (result.connected) return {};
            if (!missing(result)) return agentConnectionError(result, AgentEndpoint::Agent, "connect");
            const qint64 remaining = startupTimeoutMs - initializing.elapsed();
            if (remaining <= 0) break;
            QThread::msleep(static_cast<unsigned long>(qMin<qint64>(100, remaining)));
        } while (initializing.elapsed() < startupTimeoutMs);
        return endpointUnavailable(result);
    }
    if (!missing(desktop)) return agentConnectionError(desktop, AgentEndpoint::Desktop, "connect");
    if (!mayStart) return {{"ok", true}, {"running", false}, {"version", version}};

    const auto launchError = launch();
    if (!launchError.isEmpty()) {
        auto response = agentError("startup_failed", launchError);
        response["running"] = QJsonValue::Null;
        response["connection"] = diagnostic(result, AgentEndpoint::Agent, "startup");
        return response;
    }
    QElapsedTimer starting;
    starting.start();
    do {
        result = connect(AgentEndpoint::Agent, 150);
        if (result.connected) return {};
        // Permission errors and connection failures cannot be repaired by repeatedly spawning/waiting.
        if (!missing(result)) return agentConnectionError(result, AgentEndpoint::Agent, "startup");
        const qint64 remaining = startupTimeoutMs - starting.elapsed();
        if (remaining <= 0) break;
        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(100, remaining)));
    } while (starting.elapsed() < startupTimeoutMs);
    auto response = agentError("startup_timeout",
        "The OS accepted EditHere's launch, but its Agent endpoint did not become available in time. "
        "The GUI may still be starting or may have failed to initialize. Check the desktop app before retrying.");
    response["running"] = QJsonValue::Null;
    response["connection"] = diagnostic(result, AgentEndpoint::Agent, "startup");
    return response;
}
} // namespace h2d
