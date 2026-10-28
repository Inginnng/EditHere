#include "aiinstructionsdialog.h"
#include "aicredentials.h"
#include "aifeedbacksnapshot.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <memory>
#include <stdexcept>
using namespace h2d;

namespace {
class TestCredentials final : public AiCredentials {
  public:
    QString key = "test-dialog-key";
    QString error;
    QString requestedProvider;
    int loads = 0;
    void load(const QString &providerId) override {
        requestedProvider = providerId;
        ++loads;
        QTimer::singleShot(0, this, [this, providerId] {
            if (!error.isEmpty()) emit failed(error);
            else emit loaded(providerId, key);
        });
    }
};
class DialogApi final : public QObject {
  public:
    QTcpServer server;
    QJsonObject request;
    int requests = 0, disconnections = 0;
    bool respond = true;
    DialogApi() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                const auto buffer = std::make_shared<QByteArray>();
                const auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    if (*handled) return;
                    const auto boundary = buffer->indexOf("\r\n\r\n");
                    if (boundary < 0) return;
                    qint64 length = -1;
                    for (const auto &line : buffer->left(boundary).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (length < 0 || buffer->size() < boundary + 4 + length) return;
                    *handled = true;
                    request = QJsonDocument::fromJson(buffer->mid(boundary + 4, length)).object();
                    ++requests;
                    if (!respond) return;
                    const auto text = QString::fromUtf8(QJsonDocument(QJsonObject{{"items", QJsonArray{
                        QJsonObject{{"id", "item-1"}, {"target", "顶部标题"},
                                    {"instruction", "将标题改成蓝色，保持文字内容。"}, {"uncertainty", ""}}
                    }}}).toJson(QJsonDocument::Compact));
                    const auto body = QJsonDocument(QJsonObject{{"status", "completed"},
                        {"usage", QJsonObject{{"input_tokens", 123}, {"output_tokens", 45}}},
                        {"output", QJsonArray{QJsonObject{{"type", "message"}, {"content", QJsonArray{
                            QJsonObject{{"type", "output_text"}, {"text", text}}
                        }}}}}}).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    ++disconnections;
                    socket->deleteLater();
                });
            }
        });
    }
    AppSettings settings(const QString &provider = "openai") const {
        auto settings = defaultSettings();
        settings.aiProviderId = provider;
        auto connection = defaultAiConnection(provider);
        connection.baseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        connection.model = "test-dialog-vision";
        connection.protocol = "responses";
        settings.aiProviders[provider] = connection;
        return settings;
    }
};
Document annotatedDocument(const QColor &color = Qt::white) {
    QImage image(240, 160, QImage::Format_RGB32);
    image.fill(color);
    auto document = fromImage(image, "file", "Snapshot test");
    Note note;
    note.point = {70, 45};
    note.comment = "标题改成蓝色，保留原有文字。";
    document.notes.append(note);
    return document;
}
void artifact(QWidget &widget, const QString &name) {
    const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
    if (!folder.isEmpty() && QDir().mkpath(folder)) widget.grab().save(QDir(folder).filePath(name));
}
}

class AiInstructionsTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
        QFontDatabase::addApplicationFont(fonts + "msyh.ttc");
#endif
        applyTheme(ThemeMode::Light);
    }
    void snapshotKeepsOriginalAndAdjustedPixelsAndDoesNotMutateWithDocument() {
        auto document = annotatedDocument();
        const QRect source(20, 20, 50, 30);
        const QRect destination(120, 80, 50, 30);
        QPainter painter(&document.image);
        painter.fillRect(source, Qt::red);
        painter.end();
        document.png = encodePng(document.image);
        document.candidates = {{source, manualTarget()}};
        document.layout = createLayout(document.image.size(), document.candidates);
        QString groupId;
        for (const auto &group : document.layout->groups)
            if (group.originalBounds == QRectF(source)) groupId = group.id;
        QVERIFY(!groupId.isEmpty());
        transformLayoutGroup(*document.layout, groupId, destination);
        Note movement;
        movement.isPoint = false;
        movement.movementSource = source;
        movement.rect = destination;
        movement.comment = "把红色按钮移到右下方。";
        document.notes = {movement};
        const auto snapshot = aiFeedbackSnapshot(document);
        QCOMPARE(snapshot.images.size(), 2);
        QCOMPARE(snapshot.imageLabels.size(), 2);
        QCOMPARE(snapshot.images[0].pixelColor(source.center()), QColor(Qt::red));
        QCOMPARE(snapshot.images[0].pixelColor(destination.center()), QColor(Qt::white));
        QCOMPARE(snapshot.images[1].pixelColor(destination.center()), QColor(Qt::red));
        const auto frozenJson = QJsonDocument(snapshot.feedback).toJson();
        QVERIFY(frozenJson.contains(movement.comment.toUtf8()));
        QVERIFY(!frozenJson.contains("data:image"));
        QCOMPARE(snapshot.feedback["image"].toObject()["mode"].toString(), QString("none"));
        document.image.fill(Qt::blue);
        document.notes[0].comment = "Subsequent unrelated edit";
        transformLayoutGroup(*document.layout, groupId, QRectF(10, 90, 50, 30));
        QCOMPARE(snapshot.images[0].pixelColor(source.center()), QColor(Qt::red));
        QCOMPARE(snapshot.images[1].pixelColor(destination.center()), QColor(Qt::red));
        QCOMPARE(QJsonDocument(snapshot.feedback).toJson(), frozenJson);
    }
    void videoSnapshotUsesExportedFrameOrderAndMatchingImages() {
        VideoProject project;
        project.source = "D:/missing/movie.mp4";
        project.durationMs = 30000;
        auto late = annotatedDocument(Qt::red);
        auto early = annotatedDocument(Qt::green);
        auto unused = fromImage(QImage(240, 160, QImage::Format_RGB32), "file", "No annotation");
        project.frames = {{12000999, late}, {1000123, early}, {5000000, unused}};
        const auto snapshot = aiFeedbackSnapshot(project);
        const auto frames = snapshot.feedback["frames"].toArray();
        QCOMPARE(frames.size(), 2);
        QCOMPARE(snapshot.images.size(), 2);
        QCOMPARE(frames[0].toObject()["id"].toString(), early.id);
        QCOMPARE(frames[0].toObject()["timestampUs"].toInteger(), qint64(1000123));
        QCOMPARE(frames[1].toObject()["id"].toString(), late.id);
        QCOMPARE(snapshot.images[0].pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(snapshot.images[1].pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(snapshot.imageLabels[0].contains(early.id));
        QVERIFY(snapshot.imageLabels[0].contains("1000123"));
        QVERIFY(snapshot.imageLabels[1].contains(late.id));
        QVERIFY(snapshot.imageLabels[1].contains("12000999"));
        QVERIFY(!QJsonDocument(snapshot.feedback).toJson().contains("base64"));
        project.frames.clear();
        for (int i = 0; i < 9; ++i) project.frames.append({i * 1000000, annotatedDocument()});
        QVERIFY_EXCEPTION_THROWN(aiFeedbackSnapshot(project), std::runtime_error);
    }
    void generatedResultCopiesOnlyReviewedText() {
        DialogApi api;
        TestCredentials credentials;
        auto snapshot = aiFeedbackSnapshot(annotatedDocument());
        AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *copy = dialog.findChild<QPushButton *>("aiInstructionsCopy");
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        auto *usage = dialog.findChild<QLabel *>("aiInstructionsUsage");
        QVERIFY(generate && copy && output && usage);
        QVERIFY(!generate->isEnabled());
        QVERIFY(!copy->isEnabled());
        dialog.show();
        QTRY_VERIFY(generate->isEnabled());
        QCOMPARE(credentials.loads, 1);
        QCOMPARE(api.requests, 0); // Opening this dialog never spends API tokens.
        QVERIFY(!output->isVisible());
        QVERIFY(copy->isHidden());
        artifact(dialog, "ai-analysis-ready.png");
        generate->click();
        QTRY_VERIFY(output->toPlainText().contains("顶部标题"));
        QTRY_VERIFY(copy->isEnabled());
        QTRY_VERIFY(output->isVisible());
        QVERIFY(copy->property("primary").toBool());
        QVERIFY(!generate->property("primary").toBool());
        QCOMPARE(api.requests, 1);
        QVERIFY(usage->text().contains("123"));
        output->appendPlainText("人工核对：保持标题内容。");
        const auto expected = output->toPlainText();
        QImage oldClipboard(3, 3, QImage::Format_RGB32);
        oldClipboard.fill(Qt::yellow);
        QApplication::clipboard()->setImage(oldClipboard);
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), expected);
        const auto *mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime->hasText());
        QVERIFY(!mime->hasImage());
        QVERIFY(!mime->hasUrls());
        QVERIFY(!mime->text().contains("data:image"));
        QVERIFY(!mime->text().contains("base64"));
        artifact(dialog, "ai-instructions-generated.png");
        applyTheme(ThemeMode::Dark);
        QTest::qWait(1);
        artifact(dialog, "ai-analysis-dark.png");
        applyTheme(ThemeMode::Light);
    }
    void missingKeyAndCredentialFailureKeepConfigurationReachable_data() {
        QTest::addColumn<bool>("failure");
        QTest::newRow("missing-key") << false;
        QTest::newRow("keyring-error") << true;
    }
    void missingKeyAndCredentialFailureKeepConfigurationReachable() {
        QFETCH(bool, failure);
        DialogApi api;
        TestCredentials credentials;
        if (failure) credentials.error = "Credential store unavailable";
        else credentials.key.clear();
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
        auto *status = dialog.findChild<QLabel *>("aiInstructionsStatus");
        QVERIFY(generate && configure && status);
        dialog.show();
        QTRY_COMPARE(credentials.loads, 1);
        if (failure) QTRY_VERIFY(status->text().contains("unavailable"));
        else QTRY_VERIFY(status->text().contains("Key"));
        QVERIFY(!generate->isEnabled());
        QVERIFY(configure->isEnabled());
        QCOMPARE(api.requests, 0);
        QSignalSpy finished(&dialog, &QDialog::finished);
        configure->click();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toInt(), int(AiInstructionsDialog::Configure));
        QVERIFY(!dialog.isVisible());
        // A fresh dialog after settings picks up the new provider and credentials.
        TestCredentials changed;
        AiInstructionsDialog reopened(api.settings("kimi-cn"), aiFeedbackSnapshot(annotatedDocument()), nullptr, &changed);
        reopened.show();
        auto *newGenerate = reopened.findChild<QPushButton *>("aiInstructionsGenerate");
        QTRY_VERIFY(newGenerate->isEnabled());
        QCOMPARE(changed.requestedProvider, QString("kimi-cn"));
    }
    void closingDialogAbortsActiveRequestWithoutRequiringDestruction() {
        DialogApi api;
        api.respond = false;
        TestCredentials credentials;
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *client = dialog.findChild<AiFeedbackClient *>();
        QVERIFY(generate && client);
        dialog.show();
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_COMPARE(api.requests, 1);
        QVERIFY(client->isRunning());
        dialog.close();
        QTRY_VERIFY(!client->isRunning());
        QTRY_COMPARE(api.disconnections, 1);
        QCOMPARE(api.requests, 1);
    }
};
QTEST_MAIN(AiInstructionsTests)
#include "aiinstructionsdialog_test.moc"
