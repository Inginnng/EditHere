#include "agentprotocol.h"
#include "agentserver.h"
#include "controller.h"
#include "ui.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDir>
#include <stdexcept>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace h2d;
class AgentCliTests : public QObject {
    Q_OBJECT
    static AppSettings quietSettings() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        return settings;
    }
    static Editor *editorOf(Controller &controller) {
        auto tray = controller.findChild<QSystemTrayIcon *>("helpDesignTray");
        return qobject_cast<Editor *>(tray->contextMenu()->parentWidget());
    }
    static QString inputProject(const QTemporaryDir &directory) {
        QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::white);
        auto doc = fromImage(image, "file", "Agent test");
        Note note; note.isGlobal = true; note.comment = "Move the button"; doc.notes.append(note);
        auto path = directory.filePath("input.helpdesign");
        saveBytes(path, serializeDocument(doc, true));
        return path;
    }
    static QJsonObject request(const QString &input, const QString &output, int timeout = 60) {
        return {{"protocol", 1}, {"command", "annotate"}, {"input", input}, {"output", output}, {"timeout", timeout}};
    }
    static QByteArray readFile(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
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
            const auto saved = directory.filePath(source + ".helpdesign");
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
        QCOMPARE(feedback["annotations"].toArray().size(), 1);
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
        const auto name = "EditHere-test-" + uniqueId();
        QVERIFY(server.listen(name));
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
};
QTEST_MAIN(AgentCliTests)
#include "agent_cli_test.moc"
