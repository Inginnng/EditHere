#pragma once
#include <QJsonObject>
#include <QLocalSocket>
#include <functional>
namespace h2d {
enum class AgentEndpoint { Agent, Desktop };
struct AgentSocketResult {
    bool connected = false;
    QLocalSocket::LocalSocketError error = QLocalSocket::UnknownSocketError;
    QString message;
};
using AgentConnectAttempt = std::function<AgentSocketResult(AgentEndpoint, int)>;
// An empty launch error means that the OS accepted the launch, not that the GUI is ready.
using AgentLaunchAttempt = std::function<QString()>;
QString legacyDesktopServerName();
// For detecting an older running GUI only; never send commands to this endpoint.
QString previousDesktopServerName();
// The desktop endpoint uses the same authenticated JSON framing as the Agent
// endpoint. Raw legacy commands are refused rather than downgraded.
QByteArray encodeDesktopRequest(const QString &text, const QByteArray &token);
QString sendDesktopRequest(QLocalSocket &socket, const QString &text, const QByteArray &token, int timeoutMs = 5000);
void receiveDesktopRequest(QLocalSocket &socket, QObject &receiver,
                           const QByteArray &token,
                           const std::function<void(const QString &)> &dispatch,
                           int timeoutMs = 5000);
AgentSocketResult connectAgentSocket(QLocalSocket &socket, const QString &name, int timeout);
QJsonObject agentConnectionError(const AgentSocketResult &result, AgentEndpoint endpoint,
                                const QString &phase, const QString &context = {});
// Empty result means connected. Only confirmed missing endpoints permit launching the GUI.
// Injecting the transport/launcher lets tests exercise access-denied paths without changing IPC ACLs.
QJsonObject ensureAgentConnection(bool mayStart, const QString &version,
                                 const AgentConnectAttempt &connect, const AgentLaunchAttempt &launch,
                                 int startupTimeoutMs = 8000);
} // namespace h2d
