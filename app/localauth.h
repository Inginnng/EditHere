#pragma once
#include <QByteArray>
#include <QString>
class QLocalSocket;
namespace h2d {
// This is a local capability, not isolation from malicious processes which can
// already read files as the same OS user. Never log the token.
QString ipcRuntimeDirectory();
QString ipcCredentialPath();
QByteArray randomIpcToken();
QString createIpcCredentials(QByteArray &token);
QString readIpcCredentials(QByteArray &token);
QString validateIpcDirectory();
void removeIpcCredentials(const QByteArray &token);
QString ipcServerPolicyError();
QString ipcPeerError(const QLocalSocket &socket, bool serverSide = false);
bool ipcTokenMatches(const QByteArray &expected, const QString &received);
} // namespace h2d
