#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace h2d {
constexpr quint32 MaxAgentMessageBytes = 65536;
enum class AgentFrameState { Incomplete, Complete, Invalid };
// Every endpoint name is derived from this one directory, resolved with the GUI's application
// identity rather than the caller's. The CLI calls itself "edithere-cli", so asking
// QStandardPaths directly would hand the two processes different directories — and different
// endpoint names — even though they are meant to find each other.
QString agentStateLocation();
QString agentServerName();
QByteArray encodeAgentMessage(const QJsonObject &message);
AgentFrameState takeAgentMessage(QByteArray &buffer, QJsonObject &message, QString &error);
QJsonObject agentError(const QString &code, const QString &message);
int agentExitCode(const QJsonObject &response);
QString validateNewFeedbackPath(const QString &path);
// Publishes a fully written file without replacing any existing destination.
void writeNewFeedback(const QString &path, const QByteArray &bytes);
} // namespace h2d
