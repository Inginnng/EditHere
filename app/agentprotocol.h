#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace h2d {
constexpr quint32 MaxAgentMessageBytes = 65536;
enum class AgentFrameState { Incomplete, Complete, Invalid };
QString agentServerName();
QByteArray encodeAgentMessage(const QJsonObject &message);
AgentFrameState takeAgentMessage(QByteArray &buffer, QJsonObject &message, QString &error);
QJsonObject agentError(const QString &code, const QString &message);
int agentExitCode(const QJsonObject &response);
QString validateNewFeedbackPath(const QString &path);
// Publishes a fully written file without replacing any existing destination.
void writeNewFeedback(const QString &path, const QByteArray &bytes);
} // namespace h2d
