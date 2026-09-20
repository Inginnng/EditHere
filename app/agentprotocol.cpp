#include "agentprotocol.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtEndian>
#include <stdexcept>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif
namespace h2d {
QString agentServerName() {
    const auto oldName = QCoreApplication::applicationName();
    const auto oldOrganization = QCoreApplication::organizationName();
    QCoreApplication::setApplicationName("Help2Design");
    QCoreApplication::setOrganizationName("Help2Design");
    const auto state = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QCoreApplication::setApplicationName(oldName);
    QCoreApplication::setOrganizationName(oldOrganization);
    return "EditHere-agent-v1-" + QString::fromLatin1(QCryptographicHash::hash(state.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}
QByteArray encodeAgentMessage(const QJsonObject &message) {
    const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(json.size()), frame.data());
    frame += json;
    return frame;
}
AgentFrameState takeAgentMessage(QByteArray &buffer, QJsonObject &message, QString &error) {
    if (buffer.size() < 4) return AgentFrameState::Incomplete;
    const auto size = qFromBigEndian<quint32>(buffer.constData());
    if (!size || size > MaxAgentMessageBytes) {
        error = "IPC message exceeds the supported size.";
        return AgentFrameState::Invalid;
    }
    if (buffer.size() < qsizetype(size) + 4) return AgentFrameState::Incomplete;
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(buffer.mid(4, size), &parse);
    buffer.remove(0, qsizetype(size) + 4);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "IPC message must be a JSON object.";
        return AgentFrameState::Invalid;
    }
    message = doc.object();
    return AgentFrameState::Complete;
}
QJsonObject agentError(const QString &code, const QString &message) {
    return {{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}
int agentExitCode(const QJsonObject &response) {
    if (response["ok"].toBool()) return 0;
    const auto code = response["error"].toObject()["code"].toString();
    if (code == "invalid_arguments" || code == "protocol_error") return 2;
    if (code == "busy") return 4;
    if (code == "io_error") return 5;
    if (code == "cancelled") return 6;
    if (code == "timeout") return 7;
    if (code == "desktop_access_required") return 8;
    return 3;
}
QString validateNewFeedbackPath(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isAbsolute()) return "Output must be an absolute file path.";
    const QFileInfo file(path);
    if (file.exists() || file.isSymLink()) return "Output already exists. Choose a new file; EditHere never overwrites feedback.";
    if (!file.dir().exists()) return "Output parent directory does not exist.";
    if (file.fileName().isEmpty()) return "Output must name a file.";
    return {};
}
void writeNewFeedback(const QString &path, const QByteArray &bytes) {
    auto fail = [](const QString &message) { throw std::runtime_error(message.toStdString()); };
    if (const auto error = validateNewFeedbackPath(path); !error.isEmpty()) fail(error);
    // QTemporaryFile::close() can retain the native handle for reuse. Destroy
    // the object before publishing; otherwise Windows refuses the rename.
    struct TemporaryPath {
        QString path;
        ~TemporaryPath() { if (!path.isEmpty()) QFile::remove(path); }
    } temporary;
    {
        QTemporaryFile temp(QFileInfo(path).dir().filePath(".edithere-feedback-XXXXXX"));
        if (!temp.open()) fail(temp.errorString());
        if (temp.write(bytes) != bytes.size() || !temp.flush()) fail(temp.errorString());
        temporary.path = temp.fileName();
        temp.setAutoRemove(false);
    }
#ifdef Q_OS_WIN
    if (!MoveFileExW(reinterpret_cast<LPCWSTR>(temporary.path.utf16()), reinterpret_cast<LPCWSTR>(path.utf16()), MOVEFILE_WRITE_THROUGH))
        fail(QString("Unable to publish feedback without replacement (Windows error %1).").arg(GetLastError()));
#else
    const auto source = QFile::encodeName(temporary.path), target = QFile::encodeName(path);
    if (::link(source.constData(), target.constData()) != 0)
        fail(QString("Unable to publish feedback without replacement: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
#endif
}
} // namespace h2d
