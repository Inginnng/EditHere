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
// Legacy launchers delimit a UTF-8 request with EOF. Consume fragments while
// connected, and discard incomplete/oversized requests instead of retaining an
// idle socket or dispatching a partial file path.
void receiveDesktopRequest(QLocalSocket &socket, QObject &receiver,
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
