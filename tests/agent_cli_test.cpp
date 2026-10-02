#include "agentprotocol.h"
#include "agentconnection.h"
#include "agentserver.h"
#include "controller.h"
#include "videoplayback.h"
#include "ui.h"
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDir>
#include <stdexcept>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#ifdef Q_OS_MACOS
#include <sys/un.h>
#endif
using namespace h2d;
class AgentCliTests : public QObject {
    Q_OBJECT
    // Keep test names shorter than the production endpoints. Qt prefixes these
    // with the per-user temporary directory on Unix.
    static QString isolatedSocketName() { return "eh-" + uniqueId(); }
    static AppSettings quietSettings() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        return settings;
    }
    static Editor *editorOf(Controller &controller) {
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        return qobject_cast<Editor *>(tray->contextMenu()->parentWidget());
    }
    static QString inputProject(const QTemporaryDir &directory) {
        QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::white);
        auto doc = fromImage(image, "file", "Agent test");
        Note note; note.isGlobal = true; note.comment = "Move the button"; doc.notes.append(note);
        auto path = directory.filePath("input.edithere");
        saveBytes(path, serializeDocument(doc, true));
        return path;
    }
    static QJsonObject request(const QString &input, const QString &output, int timeout = 60) {
        return {{"protocol", 1}, {"command", "annotate"}, {"input", input}, {"output", output}, {"timeout", timeout}};
    }
    static QByteArray readFile(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
    static QString cliExecutable() {
#ifdef Q_OS_WIN
        return QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli.exe");
#else
        return QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli");
#endif
    }
    static void addVideoPointNote(Editor &editor, QPoint point, const QString &comment) {
        const auto mode = editor.findChild<QPushButton *>("mode_point");
        QVERIFY(mode);
        mode->click();
        const auto count = editor.document().notes.size();
        const auto canvas = editor.canvas();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, (QPointF(point) * canvas->zoom()).toPoint());
        QTRY_COMPARE(editor.document().notes.size(), count + 1);
        const auto id = editor.document().notes.last().id;
        const auto edit = editor.findChild<QPlainTextEdit *>("noteText_" + id);
        QVERIFY(edit);
        edit->setPlainText(comment);
        editor.agentFeedback(true);
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
            QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
        }
#endif
        applyTheme(ThemeMode::Light);
    }
    void uncertainConnectionsNeverReportStoppedOrLaunch_data() {
        QTest::addColumn<int>("socketError");
        QTest::newRow("access-denied") << int(QLocalSocket::SocketAccessError);
        QTest::newRow("timeout") << int(QLocalSocket::SocketTimeoutError);
        QTest::newRow("refused") << int(QLocalSocket::ConnectionRefusedError);
        QTest::newRow("closed") << int(QLocalSocket::PeerClosedError);
        QTest::newRow("resource") << int(QLocalSocket::SocketResourceError);
        QTest::newRow("connection") << int(QLocalSocket::ConnectionError);
        QTest::newRow("unsupported") << int(QLocalSocket::UnsupportedSocketOperationError);
        QTest::newRow("unknown") << int(QLocalSocket::UnknownSocketError);
    }
    void uncertainConnectionsNeverReportStoppedOrLaunch() {
        QFETCH(int, socketError);
        for (const bool mayStart : {false, true}) {
            int attempts = 0, launches = 0;
            const auto response = ensureAgentConnection(mayStart, "test", [&](AgentEndpoint, int) {
                ++attempts;
                return AgentSocketResult{false, QLocalSocket::LocalSocketError(socketError), "original OS/Qt diagnostic"};
            }, [&] { ++launches; return QString(); });
            QCOMPARE(attempts, 1);
            QCOMPARE(launches, 0);
            QVERIFY(!response["ok"].toBool());
            QVERIFY(response["running"].isNull());
            QCOMPARE(agentExitCode(response), socketError == QLocalSocket::SocketAccessError ? 8 : 3);
            QCOMPARE(response["error"].toObject()["code"].toString(),
                     socketError == QLocalSocket::SocketAccessError ? QString("desktop_access_required") : QString("connection_error"));
            const auto detail = response["connection"].toObject();
            QCOMPARE(detail["socketError"].toInt(), socketError);
            QCOMPARE(detail["message"].toString(), QString("original OS/Qt diagnostic"));
            QCOMPARE(detail["endpoint"].toString(), QString("agent"));
            QVERIFY(!detail["socketErrorName"].toString().isEmpty());
        }
    }
    void missingAgentButInaccessibleDesktopDoesNotLaunch() {
        int attempts = 0, launches = 0;
        const auto response = ensureAgentConnection(true, "test", [&](AgentEndpoint endpoint, int) {
            ++attempts;
            return AgentSocketResult{false, endpoint == AgentEndpoint::Agent ? QLocalSocket::ServerNotFoundError : QLocalSocket::SocketAccessError,
                                     "desktop denied"};
        }, [&] { ++launches; return QString(); });
        QCOMPARE(attempts, 2);
        QCOMPARE(launches, 0);
        QCOMPARE(agentExitCode(response), 8);
        QVERIFY(response["running"].isNull());
        QCOMPARE(response["connection"].toObject()["endpoint"].toString(), QString("desktop"));
    }
    void existingDesktopWithoutAgentIsNotRestarted() {
        int attempts = 0, launches = 0;
        const auto response = ensureAgentConnection(true, "test", [&](AgentEndpoint endpoint, int) {
            ++attempts;
            return AgentSocketResult{endpoint == AgentEndpoint::Desktop, QLocalSocket::ServerNotFoundError, "missing Agent listener"};
        }, [&] { ++launches; return QString(); }, 0);
        QCOMPARE(attempts, 3);
        QCOMPARE(launches, 0);
        QCOMPARE(agentExitCode(response), 3);
        QVERIFY(response["running"].toBool());
        QCOMPARE(response["error"].toObject()["code"].toString(), QString("agent_endpoint_unavailable"));
    }
    void existingDesktopCanFinishStartingWithoutDuplicateLaunch() {
        for (const bool mayStart : {false, true}) {
            int agentAttempts = 0, desktopAttempts = 0, launches = 0;
            const auto response = ensureAgentConnection(mayStart, "test", [&](AgentEndpoint endpoint, int) {
                if (endpoint == AgentEndpoint::Desktop) {
                    ++desktopAttempts;
                    return AgentSocketResult{true, QLocalSocket::UnknownSocketError, {}};
                }
                ++agentAttempts;
                return AgentSocketResult{agentAttempts >= 4, QLocalSocket::ServerNotFoundError, "Agent still initializing"};
            }, [&] { ++launches; return QString(); }, 2000);
            QVERIFY(response.isEmpty());
            QCOMPARE(agentAttempts, 4);
            QCOMPARE(desktopAttempts, 1);
            QCOMPARE(launches, 0);
        }
    }
    void existingDesktopWaitStopsImmediatelyOnAccessDenied() {
        int agentAttempts = 0, launches = 0;
        const auto response = ensureAgentConnection(true, "test", [&](AgentEndpoint endpoint, int) {
            if (endpoint == AgentEndpoint::Desktop)
                return AgentSocketResult{true, QLocalSocket::UnknownSocketError, {}};
            ++agentAttempts;
            return AgentSocketResult{false, agentAttempts == 1 ? QLocalSocket::ServerNotFoundError : QLocalSocket::SocketAccessError,
                                     "Agent access denied"};
        }, [&] { ++launches; return QString(); });
        QCOMPARE(agentAttempts, 2);
        QCOMPARE(launches, 0);
        QCOMPARE(agentExitCode(response), 8);
        QVERIFY(response["running"].isNull());
    }
    void statusOnlyReportsStoppedWhenBothEndpointsAreMissing() {
        int attempts = 0, launches = 0;
        const auto response = ensureAgentConnection(false, "test-version", [&](AgentEndpoint, int) {
            ++attempts;
            return AgentSocketResult{false, QLocalSocket::ServerNotFoundError, "not found"};
        }, [&] { ++launches; return QString(); });
        QCOMPARE(attempts, 2);
        QCOMPARE(launches, 0);
        QVERIFY(response["ok"].toBool());
        QVERIFY(response["running"].isBool());
        QVERIFY(!response["running"].toBool());
        QCOMPARE(response["version"].toString(), QString("test-version"));
    }
    void startupDistinguishesLaunchFailureTimeoutAndPermissionFailure() {
        for (int scenario = 0; scenario < 3; ++scenario) {
            int attempts = 0, launches = 0;
            const auto response = ensureAgentConnection(true, "test", [&](AgentEndpoint, int) {
                ++attempts;
                return AgentSocketResult{false, scenario == 2 && attempts > 2 ? QLocalSocket::SocketAccessError : QLocalSocket::ServerNotFoundError,
                                         "last connection diagnostic"};
            }, [&] { ++launches; return scenario == 0 ? QString("OS refused to create process") : QString(); }, 0);
            QCOMPARE(launches, 1);
            QCOMPARE(attempts, scenario == 0 ? 2 : 3);
            QVERIFY(response["running"].isNull());
            QCOMPARE(agentExitCode(response), scenario == 2 ? 8 : 3);
            QCOMPARE(response["error"].toObject()["code"].toString(),
                     scenario == 0 ? QString("startup_failed") : scenario == 1 ? QString("startup_timeout") : QString("desktop_access_required"));
            QCOMPARE(response["connection"].toObject()["phase"].toString(), QString("startup"));
            QCOMPARE(response["connection"].toObject()["message"].toString(), QString("last connection diagnostic"));
        }
    }
    void connectedAndColdStartingAppContinueWithoutSpuriousErrors() {
        for (int scenario = 0; scenario < 3; ++scenario) {
            int attempts = 0, launches = 0;
            const auto response = ensureAgentConnection(true, "test", [&](AgentEndpoint endpoint, int) {
                ++attempts;
                const bool ready = scenario == 0 || (scenario == 1 && attempts >= 3) || (scenario == 2 && attempts >= 4);
                return AgentSocketResult{ready || (scenario == 1 && endpoint == AgentEndpoint::Desktop),
                                         QLocalSocket::ServerNotFoundError, "not found"};
            }, [&] { ++launches; return QString(); });
            QVERIFY(response.isEmpty());
            QCOMPARE(attempts, scenario == 0 ? 1 : scenario == 1 ? 3 : 4);
            QCOMPARE(launches, scenario == 2 ? 1 : 0);
        }
    }
#ifdef Q_OS_MACOS
    void socketNamesFitMacTemporaryDirectory() {
        // sockaddr_un counts bytes including the terminator, not QString characters.
        // This checks the actual runner TMPDIR without binding the user's endpoints.
        const auto limit = qsizetype(sizeof(sockaddr_un{}.sun_path));
        for (const auto &name : {agentServerName(), legacyDesktopServerName(), isolatedSocketName()}) {
            const auto path = QDir::temp().filePath(name);
            const auto bytes = QFile::encodeName(path).size() + 1;
            const auto detail = QString("Unix socket requires %1 bytes, limit %2: %3")
                                    .arg(bytes).arg(limit).arg(path);
            QVERIFY2(bytes <= limit, qPrintable(detail));
        }
    }
#endif
    void endpointNamesIgnoreTheCallerIdentity() {
        // The GUI runs as "EditHere"/"EditHere", the CLI as "edithere-cli" with no organization,
        // so QStandardPaths answers with a different directory for each. Both endpoint names have
        // to be derived from the shared one: otherwise the CLI looks for a name the GUI never
        // created, and every desktop command fails with startup_timeout (REG-113).
        const auto name = QCoreApplication::applicationName();
        const auto organization = QCoreApplication::organizationName();
        QCoreApplication::setApplicationName("EditHere");
        QCoreApplication::setOrganizationName("EditHere");
        // Resolve the GUI's own directory first, so a cached answer cannot mask the mistake.
        const auto guiState = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const auto guiAgent = agentServerName();
        const auto guiDesktop = legacyDesktopServerName();
        QCoreApplication::setApplicationName("edithere-cli");
        QCoreApplication::setOrganizationName(QString());
        const auto cliState = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        const auto cliAgent = agentServerName();
        const auto cliDesktop = legacyDesktopServerName();
        QCoreApplication::setApplicationName(name);
        QCoreApplication::setOrganizationName(organization);
        QVERIFY2(guiState != cliState, "the two identities must resolve differently for this test to mean anything");
        QCOMPARE(cliAgent, guiAgent);
        QCOMPARE(cliDesktop, guiDesktop);
        QCOMPARE(agentStateLocation(), guiState);
    }
    void realTransportKeepsMissingErrorAndDesktopProbeSendsNothing() {
        QLocalSocket socket;
        // macOS prepends its long per-user temp path to Unix socket names.
        // Keep these names short enough for sockaddr_un::sun_path.
        const auto missing = connectAgentSocket(socket, isolatedSocketName(), 100);
        QVERIFY(!missing.connected);
        QCOMPARE(missing.error, QLocalSocket::ServerNotFoundError);
        QVERIFY(!missing.message.isEmpty());
        QLocalServer desktop;
        const auto name = isolatedSocketName();
        QVERIFY2(desktop.listen(name), qPrintable(desktop.errorString()));
        const auto connected = connectAgentSocket(socket, name, 1000);
        QVERIFY(connected.connected);
        QTRY_VERIFY(desktop.hasPendingConnections());
        auto accepted = desktop.nextPendingConnection();
        QVERIFY(accepted);
        QSignalSpy reads(accepted, &QLocalSocket::readyRead);
        socket.abort();
        QTRY_COMPARE(accepted->state(), QLocalSocket::UnconnectedState);
        QVERIFY(accepted->readAll().isEmpty());
        QCOMPARE(reads.size(), 0);
        delete accepted;
    }
    void desktopRequestsConsumeFragmentsAndWaitForEof() {
        QLocalServer server;
        const auto name = isolatedSocketName();
        QVERIFY2(server.listen(name), qPrintable(server.errorString()));
        QStringList requests;
        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(1000));
        QTRY_VERIFY(server.hasPendingConnections());
        const auto accepted = server.nextPendingConnection();
        receiveDesktopRequest(*accepted, *this, [&](const QString &text) { requests.append(text); });
        const QString path = "C:/中文 目录/图片.png";
        const auto bytes = path.toUtf8();
        // Split inside the first three-byte character, then let both reads run
        // before EOF so neither fragment can be dispatched as a partial path.
        const auto split = bytes.indexOf(QString("中").toUtf8()) + 1;
        client.write(bytes.first(split)); client.flush();
        QTRY_COMPARE(accepted->bytesAvailable(), 0);
        QTest::qWait(30);
        QVERIFY(requests.isEmpty());
        client.write(bytes.mid(split)); client.flush();
        QTest::qWait(30);
        QVERIFY(requests.isEmpty());
        client.disconnectFromServer();
        QTRY_COMPARE(requests, QStringList{path});
    }
    void desktopRequestAlreadyAtEofIsNotLost() {
        QLocalServer server;
        const auto name = isolatedSocketName();
        QVERIFY2(server.listen(name), qPrintable(server.errorString()));
        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(1000));
        QTRY_VERIFY(server.hasPendingConnections());
        const auto accepted = server.nextPendingConnection();
        client.write("capture"); client.flush();
        client.disconnectFromServer();
        // Slow startup can install the handler after the peer has already
        // closed; there will be no second disconnected signal to wait for.
        QTRY_COMPARE(accepted->state(), QLocalSocket::UnconnectedState);
        QStringList requests;
        receiveDesktopRequest(*accepted, *this, [&](const QString &text) { requests.append(text); });
        QCOMPARE(requests, QStringList{"capture"});
    }
    void invalidDesktopRequestsAreDiscardedAndNextClientWorks_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("waitForDisconnect");
        QTest::addColumn<int>("timeoutMs");
        QTest::newRow("oversized-without-eof") << QByteArray(65537, 'x') << true << 5000;
        QTest::newRow("idle-without-eof") << QByteArray() << true << 150;
        QTest::newRow("invalid-utf8") << QByteArray::fromHex("c328") << false << 5000;
        QTest::newRow("incomplete-utf8") << QByteArray::fromHex("e4b8") << false << 5000;
    }
    void invalidDesktopRequestsAreDiscardedAndNextClientWorks() {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, waitForDisconnect);
        QFETCH(int, timeoutMs);
        QLocalServer server;
        const auto name = isolatedSocketName();
        QVERIFY2(server.listen(name), qPrintable(server.errorString()));
        QStringList requests;
        connect(&server, &QLocalServer::newConnection, this, [&] {
            while (const auto accepted = server.nextPendingConnection())
                receiveDesktopRequest(*accepted, *this, [&](const QString &text) { requests.append(text); }, timeoutMs);
        });
        QLocalSocket bad;
        bad.connectToServer(name);
        QVERIFY(bad.waitForConnected(1000));
        if (!bytes.isEmpty()) { bad.write(bytes); bad.flush(); }
        if (!waitForDisconnect) bad.disconnectFromServer();
        QTRY_COMPARE_WITH_TIMEOUT(bad.state(), QLocalSocket::UnconnectedState, 1500);
        QVERIFY(requests.isEmpty());
        QLocalSocket good;
        good.connectToServer(name);
        QVERIFY(good.waitForConnected(1000));
        good.write("capture"); good.flush();
        good.disconnectFromServer();
        QTRY_COMPARE(requests, QStringList{"capture"});
    }
    void invalidIpcRequestsDoNotBreakFollowingConnections_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("zero-length") << QByteArray::fromHex("00000000");
        QTest::newRow("oversized") << QByteArray::fromHex("00010001");
        QTest::newRow("non-object") << QByteArray::fromHex("000000025b5d");
        QTest::newRow("invalid-json") << QByteArray::fromHex("000000017b");
        QTest::newRow("unsupported-protocol") << encodeAgentMessage({{"protocol", 2}, {"command", "status"}});
        const auto status = encodeAgentMessage({{"protocol", 1}, {"command", "status"}});
        QTest::newRow("multiple-requests") << status + status;
    }
    void invalidIpcRequestsDoNotBreakFollowingConnections() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        AgentServer server(controller);
        const auto name = isolatedSocketName();
        QVERIFY2(server.listen(name), qPrintable(server.errorString()));
        for (const bool invalid : {true, false}) {
            QLocalSocket socket;
            socket.connectToServer(name);
            QVERIFY(socket.waitForConnected(1000));
            socket.write(invalid ? bytes : encodeAgentMessage({{"protocol", 1}, {"command", "status"}}));
            socket.flush();
            QByteArray replyBytes;
            QJsonObject reply;
            QString error;
            AgentFrameState framing = AgentFrameState::Incomplete;
            QTRY_VERIFY_WITH_TIMEOUT(([&] {
                // QTRY evaluates its condition again after it first succeeds.
                // A complete frame has already been consumed from replyBytes.
                if (framing != AgentFrameState::Incomplete) return true;
                replyBytes += socket.readAll();
                framing = takeAgentMessage(replyBytes, reply, error);
                return framing != AgentFrameState::Incomplete;
            })(), 1500);
            QCOMPARE(framing, AgentFrameState::Complete);
            if (invalid) {
                QCOMPARE(agentExitCode(reply), 2);
                QCOMPARE(reply["error"].toObject()["code"].toString(), QString("protocol_error"));
            } else {
                QVERIFY(reply["ok"].toBool());
                QVERIFY(reply["running"].toBool());
            }
        }
    }
    void fragmentedMessagesRequireCompleteFrame() {
        const QJsonObject expected{{"command", "open"}, {"input", "C:/中文 目录/file.png"}};
        const auto frame = encodeAgentMessage(expected);
        QByteArray pending;
        QJsonObject result;
        QString error;
        for (qsizetype i = 0; i < frame.size(); ++i) {
            pending += frame[i];
            const auto state = takeAgentMessage(pending, result, error);
            QCOMPARE(state, i + 1 == frame.size() ? AgentFrameState::Complete : AgentFrameState::Incomplete);
        }
        QCOMPARE(result, expected);
        QVERIFY(pending.isEmpty());
        pending = QByteArray::fromHex("7fffffff");
        QCOMPARE(takeAgentMessage(pending, result, error), AgentFrameState::Invalid);
        pending = QByteArray::fromHex("000000025b5d");
        QCOMPARE(takeAgentMessage(pending, result, error), AgentFrameState::Invalid);
    }
    void outputNeverReplacesExistingFiles() {
        QTemporaryDir directory;
        auto output = directory.filePath("feedback.json");
        writeNewFeedback(output, "first");
        QVERIFY_EXCEPTION_THROWN(writeNewFeedback(output, "second"), std::runtime_error);
        QCOMPARE(readFile(output), QByteArray("first"));
        QVERIFY_EXCEPTION_THROWN(writeNewFeedback(directory.filePath("missing/feedback.json"), "x"), std::runtime_error);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }
    void dirtyDocumentAndActiveSessionRejectReplacement() {
        QTemporaryDir directory;
        const auto input = inputProject(directory);
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        auto current = loadDocument(input); current.dirty = true;
        const auto id = current.id;
        editor->setDocument(current);
        auto reply = controller.handleAgentRequest(request(input, directory.filePath("result.json")));
        QCOMPARE(agentExitCode(reply), 4);
        QCOMPARE(editor->document().id, id);
        current.dirty = false; editor->setDocument(current);
        reply = controller.handleAgentRequest(request(input, directory.filePath("result.json")));
        QVERIFY(reply["pending"].toBool());
        QCOMPARE(agentExitCode(controller.handleAgentRequest({{"command", "open"}, {"input", input}})), 4);
        QVERIFY(!editor->allowReplace());
        editor->findChild<QPushButton *>("agentCancel")->click();
        QVERIFY(!QFileInfo::exists(directory.filePath("result.json")));
        editor->hide();
    }
    void unsavedCapturesAndClipboardImagesRejectAgentReplacement() {
        QTemporaryDir directory;
        const auto input = inputProject(directory);
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        for (const auto &source : QStringList{"screen", "clipboard"}) {
            QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::blue);
            auto current = fromImage(image, source, "Unsaved capture");
            editor->setDocument(current);
            QVERIFY(!editor->document().dirty);
            // Keep the existing manual replacement behavior; only Agent requests
            // must not silently discard a capture that has no saved project.
            QVERIFY(editor->allowReplace());
            const auto response = controller.handleAgentRequest({{"command", "open"}, {"input", input}});
            QCOMPARE(agentExitCode(response), 4);
            QCOMPARE(editor->document().id, current.id);
            const auto saved = directory.filePath(source + ".edithere");
            saveBytes(saved, serializeDocument(current, true));
            editor->setDocument(current, saved);
            QVERIFY(controller.handleAgentRequest({{"command", "open"}, {"input", input}})["ok"].toBool());
        }
        editor->hide();
    }
    void completionPublishesFeedbackOnlyOnExplicitFinish() {
        QTemporaryDir directory;
        auto output = directory.filePath("feedback.json");
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QSignalSpy finished(&controller, &Controller::agentSessionFinished);
        const auto pending = controller.handleAgentRequest(request(inputProject(directory), output));
        QVERIFY(pending["pending"].toBool());
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(editor->findChild<QWidget *>("agentSessionBanner")->isVisible());
        QTest::qWait(100);
        QDir().mkpath("../artifacts");
        QVERIFY(editor->grab().save("../artifacts/agent-cli-session.png"));
        editor->findChild<QPushButton *>("agentFinish")->click();
        QCOMPARE(finished.size(), 1);
        const auto reply = finished[0][1].toJsonObject();
        QVERIFY(reply["ok"].toBool());
        QCOMPARE(reply["output"].toString(), output);
        const auto feedback = QJsonDocument::fromJson(readFile(output)).object();
        // 0.9.0 object-oriented format: the global note becomes a sourceless object.
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 1);
        const auto object = objects[0].toObject();
        QVERIFY(object["source"].isNull());
        QCOMPARE(object["movements"].toArray().size(), 0);
        QCOMPARE(object["annotations"].toArray().size(), 1);
        QCOMPARE(object["annotations"].toArray()[0].toString(), QString("Move the button"));
        QVERIFY(!feedback["image"].toString().isEmpty());
        QVERIFY(!editor->findChild<QWidget *>("agentSessionBanner")->isVisible());
        QVERIFY(editor->hasDocument());
        editor->hide();
    }
    void cancellationAndTimeoutKeepDocumentWithoutOutput() {
        QTemporaryDir directory;
        const auto input = inputProject(directory);
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QSignalSpy finished(&controller, &Controller::agentSessionFinished);
        controller.handleAgentRequest(request(input, directory.filePath("cancelled.json")));
        const auto id = editor->document().id;
        editor->findChild<QPushButton *>("agentCancel")->click();
        QCOMPARE(agentExitCode(finished[0][1].toJsonObject()), 6);
        QCOMPARE(editor->document().id, id);
        QVERIFY(!QFileInfo::exists(directory.filePath("cancelled.json")));
        controller.handleAgentRequest(request(input, directory.filePath("timeout.json"), 1));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 2500);
        QCOMPARE(agentExitCode(finished[1][1].toJsonObject()), 7);
        QVERIFY(editor->hasDocument());
        QVERIFY(!QFileInfo::exists(directory.filePath("timeout.json")));
        editor->hide();
    }
    void fileCreatedWhileWaitingIsNotOverwritten() {
        QTemporaryDir directory;
        const auto output = directory.filePath("feedback.json");
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        auto editor = editorOf(controller);
        QSignalSpy finished(&controller, &Controller::agentSessionFinished);
        controller.handleAgentRequest(request(inputProject(directory), output));
        writeNewFeedback(output, "other process owns this");
        editor->findChild<QPushButton *>("agentFinish")->click();
        QCOMPARE(agentExitCode(finished[0][1].toJsonObject()), 5);
        QCOMPARE(readFile(output), QByteArray("other process owns this"));
        QVERIFY(editor->hasDocument());
        editor->hide();
    }
    void socketDisconnectCancelsItsSession() {
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        AgentServer server(controller);
        const auto name = isolatedSocketName();
        QVERIFY2(server.listen(name), qPrintable(server.errorString()));
        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected(1000));
        const auto frame = encodeAgentMessage(request(inputProject(directory), directory.filePath("result.json")));
        socket.write(frame.first(3)); socket.flush();
        QTest::qWait(30);
        QVERIFY(!controller.handleAgentRequest({{"command", "status"}})["agentSession"].toBool());
        socket.write(frame.mid(3)); socket.flush();
        QTRY_VERIFY(controller.handleAgentRequest({{"command", "status"}})["agentSession"].toBool());
        QSignalSpy finished(&controller, &Controller::agentSessionFinished);
        socket.abort();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(agentExitCode(finished[0][1].toJsonObject()), 6);
        QVERIFY(editorOf(controller)->hasDocument());
        QVERIFY(!QFileInfo::exists(directory.filePath("result.json")));
        editorOf(controller)->hide();
    }
    void consoleExportReportsJsonAndNonzeroOnConflict() {
        QTemporaryDir directory;
        const auto input = inputProject(directory), output = directory.filePath("result.json");
#ifdef Q_OS_WIN
        const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli.exe");
#else
        const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli");
#endif
        QProcess cli;
        cli.start(executable, {"export", input, "--output", output, "--no-image"});
        QVERIFY(cli.waitForFinished(15000));
        QCOMPARE(cli.exitCode(), 0);
        const auto reply = QJsonDocument::fromJson(cli.readAllStandardOutput()).object();
        QVERIFY(reply["ok"].toBool());
        QVERIFY(!QJsonDocument::fromJson(readFile(output)).object().contains("image"));
        cli.start(executable, {"export", input, "--output", output});
        QVERIFY(cli.waitForFinished(15000));
        QCOMPARE(cli.exitCode(), 5);
        QCOMPARE(QJsonDocument::fromJson(cli.readAllStandardOutput()).object()["error"].toObject()["code"].toString(), QString("io_error"));
        cli.start(executable, {"annotate", input, "--output", directory.filePath("other.json"), "--timeout", "0"});
        QVERIFY(cli.waitForFinished(15000));
        QCOMPARE(cli.exitCode(), 2);
    }
    void consoleVideoOpenAnnotateSaveAndReopenThroughAgentTransport() {
        const auto source = qEnvironmentVariable("EDITHERE_VIDEO_TEST_SOURCE");
        if (source.isEmpty())
            QSKIP("Set EDITHERE_VIDEO_TEST_SOURCE to exercise online video CLI and real decoding.");
        QVERIFY2(QFileInfo(source).isFile(), qPrintable("Video test source not found: " + source));
        struct StandardPathsTestMode {
            const bool previous = QStandardPaths::isTestModeEnabled();
            StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(true); }
            ~StandardPathsTestMode() { QStandardPaths::setTestModeEnabled(previous); }
        } isolatedStandardPaths;
#ifdef Q_OS_WIN
        const auto videoCli = QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli-test.exe");
#else
        const auto videoCli = QDir(QCoreApplication::applicationDirPath()).filePath("edithere-cli-test");
#endif
        QVERIFY2(QFileInfo(videoCli).isFile(), qPrintable("Video test CLI not found: " + videoCli));
        QTemporaryDir directory;
        Controller controller(nullptr, quietSettings(), directory.filePath("settings.ini"));
        const auto editor = editorOf(controller);
        QVERIFY(editor);
        AgentServer server(controller);
        // Both processes use Qt test mode. Windows can accept multiple named
        // pipe listeners, so a successful listen on the ordinary endpoint would
        // not prove that CLI requests are isolated from a user's running app.
        QVERIFY2(server.listen(agentServerName()), qPrintable(server.errorString()));
        const auto playback = editor->findChild<VideoPlayback *>();
        QVERIFY(playback);
        QSignalSpy paused(playback, &VideoPlayback::framePaused);

        QProcess open;
        open.start(videoCli, {"open", source});
        QVERIFY2(open.waitForStarted(), qPrintable(open.errorString()));
        QTRY_COMPARE_WITH_TIMEOUT(open.state(), QProcess::NotRunning, 15000);
        const auto openOutput = open.readAllStandardOutput();
        const auto openErrors = open.readAllStandardError();
        QVERIFY2(open.exitCode() == 0,
                 qPrintable(QString("Video open CLI failed (exit %1):\n%2\n%3")
                            .arg(open.exitCode()).arg(QString::fromUtf8(openOutput), QString::fromUtf8(openErrors))));
        const auto accepted = QJsonDocument::fromJson(openOutput).object();
        QVERIFY(accepted["ok"].toBool());
        QVERIFY(accepted["accepted"].toBool());
        QCOMPARE(accepted["command"].toString(), QString("open"));
        QTRY_VERIFY_WITH_TIMEOUT(!paused.isEmpty() && !editor->document().image.isNull(), 15000);
        QVERIFY(editor->hasVideo());
        QCOMPARE(editor->videoProject().source, QFileInfo(source).absoluteFilePath());
        QVERIFY(playback->durationMs() > 3000);

        const auto embeddedPath = directory.filePath("video-embedded.json");
        const int beforeAnnotate = paused.size();
        QProcess annotate;
        annotate.start(videoCli, {"annotate", source, "--output", embeddedPath, "--timeout", "60"});
        QVERIFY2(annotate.waitForStarted(), qPrintable(annotate.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(editor->findChild<QWidget *>("agentSessionBanner")->isVisible(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeAnnotate && !editor->document().image.isNull(), 15000);
        QCOMPARE(annotate.state(), QProcess::Running);
        QVERIFY(!QFileInfo::exists(embeddedPath));

        const qint64 duration = playback->durationMs();
        const qint64 firstTarget = qMax<qint64>(1000, qMin<qint64>(duration / 3, 65000));
        const qint64 secondTarget = qMax<qint64>(firstTarget + 1000, qMin<qint64>(duration * 2 / 3, 105000));
        int beforeSeek = paused.size();
        playback->seek(firstTarget);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek &&
                                qAbs(paused.last()[1].toLongLong() / 1000 - firstTarget) < 1000, 15000);
        const QPoint samePoint(editor->document().image.width() / 3, editor->document().image.height() / 3);
        const QString firstComment = "第一个时间点：放大标题文字";
        const QString secondComment = "第二个时间点：将此区域改为蓝色";
        const QString thirdComment = "第二个时间点：缩短这一行说明";
        addVideoPointNote(*editor, samePoint, firstComment);
        beforeSeek = paused.size();
        playback->seek(secondTarget);
        QTRY_VERIFY_WITH_TIMEOUT(paused.size() > beforeSeek &&
                                qAbs(paused.last()[1].toLongLong() / 1000 - secondTarget) < 1000, 15000);
        addVideoPointNote(*editor, samePoint, secondComment);
        // Existing annotation badges take a 17-screen-pixel hit radius. Choose
        // a distinct image location that stays outside it at fitted zoom.
        const QPoint thirdPoint(editor->document().image.width() * 2 / 3,
                                editor->document().image.height() / 2);
        addVideoPointNote(*editor, thirdPoint, thirdComment);
        QCOMPARE(editor->totalAnnotationCount(), 3);
        QCOMPARE(editor->videoProject().frames.size(), 2);

        // Exercise the real save action and file dialog, including clearing the
        // dirty state needed before a subsequent Agent request can replace it.
        const auto projectPath = directory.filePath("saved-video.edithere");
        const bool oldDialogs = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const bool oldTestMode = QStandardPaths::isTestModeEnabled();
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        QStandardPaths::setTestModeEnabled(true);
        bool selectedFile = false, saveError = false;
        QTimer selectFile, saveDeadline;
        connect(&selectFile, &QTimer::timeout, editor, [&] {
            auto active = QApplication::activeModalWidget();
            if (const auto dialog = qobject_cast<QFileDialog *>(active); dialog && !selectedFile) {
                selectedFile = true;
                if (const auto filename = dialog->findChild<QLineEdit *>("fileNameEdit")) {
                    filename->setText(projectPath);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                } else {
                    saveError = true;
                    dialog->reject();
                }
            } else if (const auto error = qobject_cast<QMessageBox *>(active)) {
                saveError = true;
                error->accept();
            }
        });
        saveDeadline.setSingleShot(true);
        connect(&saveDeadline, &QTimer::timeout, editor, [] {
            if (const auto modal = QApplication::activeModalWidget()) modal->close();
        });
        selectFile.start(20);
        saveDeadline.start(5000);
        const bool saved = editor->saveProject();
        selectFile.stop();
        saveDeadline.stop();
        QStandardPaths::setTestModeEnabled(oldTestMode);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, oldDialogs);
        QVERIFY(selectedFile && !saveError && saved);
        QVERIFY(!editor->hasUnsavedChanges());
        QCOMPARE(loadVideoProject(projectPath).frames.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(readFile(projectPath)).object()["schemaVersion"].toString(), QString("video-project-1"));

        editor->findChild<QPushButton *>("agentFinish")->click();
        QTRY_COMPARE_WITH_TIMEOUT(annotate.state(), QProcess::NotRunning, 15000);
        QCOMPARE(annotate.exitCode(), 0);
        const auto result = QJsonDocument::fromJson(annotate.readAllStandardOutput()).object();
        QVERIFY(result["ok"].toBool());
        QCOMPARE(result["annotations"].toInt(), 3);
        QCOMPARE(result["imageIncluded"].toBool(), true);
        QCOMPARE(result["output"].toString(), embeddedPath);
        const auto embedded = QJsonDocument::fromJson(readFile(embeddedPath)).object();
        QCOMPARE(embedded["schemaVersion"].toString(), QString("video-feedback-1"));
        QCOMPARE(embedded["frames"].toArray().size(), 2);
        const auto objects = embedded["objects"].toArray();
        QCOMPARE(objects.size(), 3);
        QCOMPARE(objects[0].toObject()["source"], objects[1].toObject()["source"]);
        QVERIFY(objects[0].toObject()["frameId"] != objects[1].toObject()["frameId"]);
        QVERIFY(objects[0].toObject()["timestampMs"].toInteger() < objects[1].toObject()["timestampMs"].toInteger());
        QCOMPARE(objects[0].toObject()["annotations"].toArray()[0].toString(), firstComment);
        QCOMPARE(objects[1].toObject()["annotations"].toArray()[0].toString(), secondComment);
        QCOMPARE(objects[2].toObject()["annotations"].toArray()[0].toString(), thirdComment);
        for (const auto &frame : embedded["frames"].toArray())
            QVERIFY(frame.toObject()["feedback"].toObject().contains("image"));

        open.start(videoCli, {"open", projectPath});
        QVERIFY(open.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(open.state(), QProcess::NotRunning, 15000);
        QCOMPARE(open.exitCode(), 0);
        QVERIFY(QJsonDocument::fromJson(open.readAllStandardOutput()).object()["accepted"].toBool());
        QCOMPARE(editor->videoProject().frames.size(), 2);
        QCOMPARE(editor->totalAnnotationCount(), 3);

        const auto noImagePath = directory.filePath("video-no-image.json");
        annotate.start(videoCli, {"annotate", projectPath, "--output", noImagePath, "--no-image", "--timeout", "60"});
        QVERIFY(annotate.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(editor->findChild<QWidget *>("agentSessionBanner")->isVisible(), 15000);
        QCOMPARE(editor->totalAnnotationCount(), 3);
        QVERIFY(!QFileInfo::exists(noImagePath));
        editor->findChild<QPushButton *>("agentFinish")->click();
        QTRY_COMPARE_WITH_TIMEOUT(annotate.state(), QProcess::NotRunning, 15000);
        QCOMPARE(annotate.exitCode(), 0);
        const auto noImageResult = QJsonDocument::fromJson(annotate.readAllStandardOutput()).object();
        QVERIFY(noImageResult["ok"].toBool());
        QCOMPARE(noImageResult["annotations"].toInt(), 3);
        QCOMPARE(noImageResult["imageIncluded"].toBool(), false);
        const auto noImage = QJsonDocument::fromJson(readFile(noImagePath)).object();
        QCOMPARE(noImage["frames"].toArray().size(), 2);
        QCOMPARE(noImage["objects"], embedded["objects"]);
        for (const auto &frame : noImage["frames"].toArray())
            QVERIFY(!frame.toObject()["feedback"].toObject().contains("image"));

        const auto artifactFolder = qEnvironmentVariable("EDITHERE_VIDEO_TEST_ARTIFACTS");
        if (!artifactFolder.isEmpty()) {
            QDir().mkpath(artifactFolder);
            saveBytes(QDir(artifactFolder).filePath("cli-video-project.edithere"), readFile(projectPath));
            saveBytes(QDir(artifactFolder).filePath("cli-video-embedded.json"), readFile(embeddedPath));
            saveBytes(QDir(artifactFolder).filePath("cli-video-no-image.json"), readFile(noImagePath));
            editor->grab().save(QDir(artifactFolder).filePath("cli-video-session.png"));
        }
        editor->hide();
    }
};
QTEST_MAIN(AgentCliTests)
#include "agent_cli_test.moc"
