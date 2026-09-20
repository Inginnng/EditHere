#include "agentserver.h"
#include "agentprotocol.h"
#include "controller.h"
#include <QLocalSocket>
#include <QTimer>
#include <memory>
namespace h2d {
AgentServer::AgentServer(Controller &controller, QObject *parent) : QLocalServer(parent) {
    setSocketOptions(QLocalServer::UserAccessOption);
    connect(this, &QLocalServer::newConnection, this, [this, &controller] {
        while (auto socket = nextPendingConnection()) {
            socket->setReadBufferSize(MaxAgentMessageBytes + 4);
            struct State { QByteArray bytes; bool received = false; QString session; };
            auto state = std::make_shared<State>();
            auto deadline = new QTimer(socket);
            deadline->setSingleShot(true);
            deadline->start(5000);
            auto reply = [socket](const QJsonObject &message) {
                socket->write(encodeAgentMessage(message));
                socket->disconnectFromServer();
            };
            connect(deadline, &QTimer::timeout, socket, [reply] { reply(agentError("timeout", "IPC request was not completed within 5 seconds.")); });
            connect(socket, &QLocalSocket::disconnected, socket, [socket, state, &controller] {
                if (!state->session.isEmpty()) controller.cancelAgentSession(state->session, "cancelled", "Agent disconnected. Your edits remain in EditHere.");
                socket->deleteLater();
            });
            connect(socket, &QLocalSocket::readyRead, socket, [socket, state, deadline, reply, &controller] {
                if (state->received) { socket->readAll(); return; }
                state->bytes += socket->readAll();
                QJsonObject request;
                QString error;
                const auto framing = takeAgentMessage(state->bytes, request, error);
                if (framing == AgentFrameState::Incomplete) return;
                deadline->stop();
                state->received = true;
                if (framing == AgentFrameState::Invalid || !state->bytes.isEmpty()) {
                    reply(agentError("protocol_error", error.isEmpty() ? "Expected one request per connection." : error));
                    return;
                }
                if (request["protocol"].toInt() != 1) {
                    reply(agentError("protocol_error", "Unsupported IPC protocol."));
                    return;
                }
                const auto response = controller.handleAgentRequest(request);
                if (!response["pending"].toBool()) { reply(response); return; }
                state->session = response["session"].toString();
                connect(&controller, &Controller::agentSessionFinished, socket, [state, reply](const QString &id, const QJsonObject &result) {
                    if (id != state->session) return;
                    state->session.clear();
                    reply(result);
                });
            });
        }
    });
}
bool AgentServer::start() {
    const auto name = agentServerName();
    QLocalServer::removeServer(name);
    return listen(name);
}
} // namespace h2d
