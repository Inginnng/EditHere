#include "agentconnection.h"
#include "agentprotocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QThread>
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
    const auto oldName = QCoreApplication::applicationName();
    const auto oldOrganization = QCoreApplication::organizationName();
    QCoreApplication::setApplicationName("Help2Design");
    QCoreApplication::setOrganizationName("Help2Design");
    const auto state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QCoreApplication::setApplicationName(oldName);
    QCoreApplication::setOrganizationName(oldOrganization);
    return "Help2Design-native-" + QString::number(qHash(state));
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

    // A pre-CLI GUI still owns the desktop endpoint. Connecting without writing is a no-op
    // for its legacy protocol and avoids inspecting or modifying the real instance lock.
    const auto desktop = connect(AgentEndpoint::Desktop, 400);
    if (desktop.connected) {
        // main() publishes the legacy listener before constructing the UI and Agent server.
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
