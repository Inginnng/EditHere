#include "agentprotocol.h"
#include "agentconnection.h"
#include "model.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <cstdio>
using namespace h2d;
namespace {
int printResult(const QJsonObject &result) {
    const auto json = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    std::fflush(stdout);
    return agentExitCode(result);
}
QString guiExecutable() {
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath("EditHere.exe");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath("EditHere");
#endif
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("edithere-cli");
    app.setApplicationVersion(HELPDESIGN_VERSION);
    auto args = app.arguments();
    args.removeFirst();
    if (args == QStringList{"--help"} || args == QStringList{"help"} || args.isEmpty()) {
        std::puts("EditHere Agent CLI " HELPDESIGN_VERSION "\n"
                  "Usage:\n"
                  "  edithere-cli --help | --version\n"
                  "  edithere-cli status\n"
                  "  edithere-cli open <image-or-project>\n"
                  "  edithere-cli capture\n"
                  "  edithere-cli export <project> --output <new.json> [--no-image]\n"
                  "  edithere-cli annotate <image-or-project> --output <new.json> [--no-image] [--timeout <seconds>]\n\n"
                  "annotate waits for the user to finish in EditHere (default timeout 1800s; range 1..86400).\n"
                  "Feedback embeds the image by default. Output must not exist; its parent must exist.\n"
                  "open/capture only acknowledge acceptance; status never starts the app.\n"
                  "Results are one UTF-8 JSON object on stdout. No feedback file is written on cancel/timeout.\n"
                  "Exit codes: 0 success, 2 arguments/protocol, 3 unavailable, 4 busy, 5 I/O, 6 cancelled, 7 timeout, 8 desktop access required.");
        return 0;
    }
    if (args == QStringList{"--version"}) { std::puts("EditHere " HELPDESIGN_VERSION); return 0; }
    const auto command = args.takeFirst();
    const bool withPath = command == "open" || command == "export" || command == "annotate";
    if (!withPath && command != "status" && command != "capture")
        return printResult(agentError("invalid_arguments", "Unknown command. Run edithere-cli --help."));
    QString input, output;
    bool embed = true;
    int timeoutSeconds = 1800;
    if (withPath) {
        if (args.isEmpty() || args.first().startsWith("--"))
            return printResult(agentError("invalid_arguments", "An image or project path is required."));
        input = QFileInfo(args.takeFirst()).absoluteFilePath();
    }
    bool outputSeen = false, imageSeen = false, timeoutSeen = false;
    while (!args.isEmpty()) {
        const auto option = args.takeFirst();
        if (option == "--output" && !outputSeen && (command == "export" || command == "annotate") && !args.isEmpty()) {
            outputSeen = true;
            output = QFileInfo(args.takeFirst()).absoluteFilePath();
        } else if (option == "--no-image" && !imageSeen && (command == "export" || command == "annotate")) {
            imageSeen = true; embed = false;
        } else if (option == "--timeout" && !timeoutSeen && command == "annotate" && !args.isEmpty()) {
            timeoutSeen = true;
            bool ok = false; timeoutSeconds = args.takeFirst().toInt(&ok);
            if (!ok || timeoutSeconds < 1 || timeoutSeconds > 86400)
                return printResult(agentError("invalid_arguments", "--timeout must be between 1 and 86400 seconds."));
        } else return printResult(agentError("invalid_arguments", "Unknown, duplicate or incomplete option: " + option));
    }
    if ((command == "export" || command == "annotate") && !outputSeen)
        return printResult(agentError("invalid_arguments", "--output <new.json> is required."));
    if (withPath && !QFileInfo(input).isFile())
        return printResult(agentError("io_error", "Input file does not exist: " + input));
    if (outputSeen) {
        const auto error = validateNewFeedbackPath(output);
        if (!error.isEmpty()) return printResult(agentError("io_error", error));
    }
    if (command == "export") {
        try {
            const auto doc = loadDocument(input);
            writeNewFeedback(output, serializeFeedback(doc, embed));
            return printResult({{"ok", true}, {"command", "export"}, {"output", output}, {"annotations", doc.notes.size()}, {"imageIncluded", embed}});
        } catch (const std::exception &error) { return printResult(agentError("io_error", QString::fromUtf8(error.what()))); }
    }
    QLocalSocket socket;
    socket.setReadBufferSize(MaxAgentMessageBytes + 4);
    const auto connection = ensureAgentConnection(command != "status", HELPDESIGN_VERSION,
        [&](AgentEndpoint endpoint, int timeout) {
            if (endpoint == AgentEndpoint::Agent) return connectAgentSocket(socket, agentServerName(), timeout);
            QLocalSocket desktop;
            return connectAgentSocket(desktop, legacyDesktopServerName(), timeout);
        }, [] {
            const auto executable = guiExecutable();
            if (!QFileInfo(executable).isFile())
                return QString("Cannot find EditHere next to this CLI. Install or unpack the complete application.");
            QProcess process;
            process.setProgram(executable);
            process.setArguments({"--agent-start"});
            if (!process.startDetached())
                return QString("The OS could not start EditHere: ") + process.errorString();
            return QString();
        });
    if (!connection.isEmpty()) return printResult(connection);
    QJsonObject request{{"protocol", 1}, {"command", command}};
    if (withPath) request["input"] = input;
    if (outputSeen) { request["output"] = output; request["embed"] = embed; }
    if (command == "annotate") request["timeout"] = timeoutSeconds;
    const auto message = encodeAgentMessage(request);
    if (socket.write(message) != message.size())
        return printResult(agentConnectionError({false, socket.error(), socket.errorString()}, AgentEndpoint::Agent, "send", "Unable to send the request."));
    socket.flush();
    const qint64 deadlineMs = command == "annotate" ? qint64(timeoutSeconds) * 1000 + 10000 : 30000;
    QElapsedTimer waiting; waiting.start();
    QByteArray responseBytes;
    while (waiting.elapsed() < deadlineMs) {
        responseBytes += socket.readAll();
        QString error; QJsonObject response;
        const auto state = takeAgentMessage(responseBytes, response, error);
        if (state == AgentFrameState::Complete) return printResult(response);
        if (state == AgentFrameState::Invalid) return printResult(agentError("protocol_error", error));
        if (socket.state() == QLocalSocket::UnconnectedState)
            return printResult(agentConnectionError({false, socket.error(), socket.errorString()}, AgentEndpoint::Agent, "response", "EditHere disconnected before returning a result. No completion was confirmed."));
        socket.waitForReadyRead(int(qBound<qint64>(qint64(0), deadlineMs - waiting.elapsed(), qint64(500))));
    }
    socket.abort();
    return printResult(agentError("timeout", "EditHere did not respond in time. Your edits remain in the application."));
}
