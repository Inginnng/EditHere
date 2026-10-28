#include "aifeedback.h"
#include <QBuffer>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <functional>
#include <memory>
using namespace h2d;

class FakeAiApi : public QObject {
  public:
    QTcpServer server;
    QList<QJsonObject> requests;
    QList<QMap<QByteArray, QByteArray>> headers;
    QList<QByteArray> paths;
    QByteArray response;
    int status = 200;
    QByteArray extraHeaders;
    bool respond = true;
    explicit FakeAiApi(QObject *parent = nullptr) : QObject(parent) {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    if (*handled) return;
                    const int boundary = buffer->indexOf("\r\n\r\n");
                    if (boundary < 0) return;
                    QMap<QByteArray, QByteArray> values;
                    const auto lines = buffer->left(boundary).split('\n');
                    for (qsizetype i = 1; i < lines.size(); ++i) {
                        const auto line = lines[i].trimmed();
                        const auto colon = line.indexOf(':');
                        if (colon > 0) values[line.left(colon).toLower()] = line.mid(colon + 1).trimmed();
                    }
                    const auto length = values.value("content-length").toLongLong();
                    if (buffer->size() < boundary + 4 + length) return;
                    *handled = true;
                    requests.append(QJsonDocument::fromJson(buffer->mid(boundary + 4, length)).object());
                    headers.append(values);
                    paths.append(lines.first().split(' ').value(1));
                    if (!respond) return;
                    const auto body = response;
                    const auto head = "HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extraHeaders + "\r\n";
                    socket->write(head + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1/v1").arg(server.serverPort()); }
};

namespace {
QJsonObject rect(int x, int y, int width = 30, int height = 20) {
    return {{"x1", x}, {"y1", y}, {"x2", x + width}, {"y2", y + height}};
}
AiFeedbackInput input() {
    AiFeedbackInput result;
    const QJsonObject child{{"id", "o2"}, {"kind", "point"}, {"point", QJsonObject{{"x", 60}, {"y", 45}}},
                            {"annotations", QJsonArray{QJsonObject{{"number", 2}, {"text", "颜色改成蓝色"}}}}};
    const QJsonObject object{{"id", "o1"}, {"kind", "region"}, {"source", rect(10, 20)},
                             {"movement", QJsonObject{{"rect", rect(50, 40, 60)}, {"dx", 40}, {"dy", 20}, {"scaleX", 2}}},
                             {"annotations", QJsonArray{QJsonObject{{"number", 1}, {"text", "向右移动，与下方输入框对齐\n保留原有文字"}}}},
                             {"children", QJsonArray{child}}};
    result.feedback = QJsonObject{{"schemaVersion", "feedback-minimal-4"}, {"annotationSpace", "result"},
                                   {"image", QJsonObject{{"mode", "embedded"}, {"dataUrl", "data:image/png;base64,SHOULD_NOT_LEAK"},
                                                         {"width", 160}, {"height", 100}}},
                                   {"capture", QJsonObject{{"page", "登录页"}}}, {"objects", QJsonArray{object}}};
    QImage original(160, 100, QImage::Format_RGB32); original.fill(Qt::red);
    QImage adjusted(160, 100, QImage::Format_RGB32); adjusted.fill(Qt::green);
    result.images = {original, adjusted}; result.imageLabels = {"original", "result"};
    return result;
}
QString modelContent(const QStringList &ids = {"item-1", "item-2"}) {
    QJsonArray items;
    for (const auto &id : ids)
        items.append(QJsonObject{{"id", id}, {"target", id == "item-1" ? "顶部登录按钮" : "按钮内图标"},
                                  {"instruction", "按用户要求调整，并与输入框对齐。"}, {"uncertainty", ""}});
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"items", items}}).toJson(QJsonDocument::Compact));
}
QByteArray responseFor(const QString &protocol, const QString &text, const QString &finish = {}) {
    QJsonObject result{{"usage", QJsonObject{{"input_tokens", 124}, {"output_tokens", 30}, {"prompt_tokens", 124}, {"completion_tokens", 30}}}};
    if (protocol == "responses") {
        result["status"] = finish.isEmpty() ? "completed" : finish;
        result["output"] = QJsonArray{QJsonObject{{"type", "message"}, {"content", QJsonArray{QJsonObject{{"type", "output_text"}, {"text", text}}}}}};
    } else if (protocol == "anthropic-messages") {
        result["stop_reason"] = finish.isEmpty() ? "end_turn" : finish;
        result["content"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}};
    } else {
        result["choices"] = QJsonArray{QJsonObject{{"finish_reason", finish.isEmpty() ? "stop" : finish},
                                                  {"message", QJsonObject{{"role", "assistant"}, {"content", text}}}}};
    }
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
AiConnectionSettings config(FakeAiApi &api, const QString &protocol = "responses") {
    AiConnectionSettings result;
    result.baseUrl = api.base(); result.model = "test-vision-model"; result.protocol = protocol;
    return result;
}
QJsonArray sentUserContent(const QJsonObject &body, const QString &protocol) {
    return protocol == "responses" ? body["input"].toArray().last().toObject()["content"].toArray() :
           body["messages"].toArray().last().toObject()["content"].toArray();
}
} // namespace

class AiFeedbackTests : public QObject {
    Q_OBJECT
  private slots:
    void presetsAndPromptAreBundled() {
        const auto presets = aiProviderPresets();
        QVERIFY(presets.size() >= 6);
        for (const auto &preset : presets) {
            if (preset.id == "custom") continue;
            const auto settings = defaultAiConnection(preset.id);
            QVERIFY2(validateAiConnection(settings).isEmpty(), qPrintable(preset.id));
        }
        const auto prompt = aiSystemPrompt();
        QVERIFY(prompt.contains("untrusted task material"));
        QVERIFY(prompt.contains("parentItemId"));
        QVERIFY(prompt.contains("movement.rect"));
    }
    void rejectsUnsafeUrlsAndMismatchedProtocol() {
        AiConnectionSettings settings = defaultAiConnection();
        settings.model = "vision";
        for (const auto &url : {"http://example.com/v1", "https://user:password@example.com/v1", "https://example.com/v1?key=secret",
                                "https://example.com/v1#fragment", "https://example.com/v1/messages"}) {
            settings.baseUrl = url;
            QVERIFY2(!validateAiConnection(settings).isEmpty(), url);
        }
        settings.baseUrl = "http://127.0.0.1:1234/v1";
        QVERIFY(validateAiConnection(settings).isEmpty());
        settings.baseUrl = "https://example.com/v1/responses/";
        QVERIFY(validateAiConnection(settings).isEmpty());
    }
    void protocols_data() {
        QTest::addColumn<QString>("protocol"); QTest::addColumn<QByteArray>("path");
        QTest::newRow("responses") << QString("responses") << QByteArray("/v1/responses");
        QTest::newRow("chat") << QString("chat-completions") << QByteArray("/v1/chat/completions");
        QTest::newRow("anthropic") << QString("anthropic-messages") << QByteArray("/v1/messages");
    }
    void protocols() {
        QFETCH(QString, protocol); QFETCH(QByteArray, path);
        FakeAiApi api; api.response = responseFor(protocol, modelContent());
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed), usage(&client, &AiFeedbackClient::usageAvailable);
        client.generate(config(api, protocol), "test-secret-key", input());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(errors.size(), 0); QCOMPARE(api.requests.size(), 1); QCOMPARE(api.paths.first(), path);
        const auto headers = api.headers.first();
        if (protocol == "anthropic-messages") {
            QCOMPARE(headers.value("x-api-key"), QByteArray("test-secret-key"));
            QCOMPARE(headers.value("anthropic-version"), QByteArray("2023-06-01"));
            QVERIFY(!headers.contains("authorization"));
        } else QCOMPARE(headers.value("authorization"), QByteArray("Bearer test-secret-key"));
        const auto body = api.requests.first();
        const auto content = sentUserContent(body, protocol);
        QVERIFY(!QJsonDocument(body).toJson().contains("SHOULD_NOT_LEAK"));
        const auto metadata = QJsonDocument::fromJson(content.first().toObject()["text"].toString().toUtf8()).object();
        QCOMPARE(metadata["itemsToInterpret"].toArray().size(), 2);
        QCOMPARE(metadata["itemsToInterpret"].toArray()[1].toObject()["parentItemId"].toString(), QString("item-1"));
        QCOMPARE(content.size(), 5); // Main text plus label/image pairs.
        const auto image = content[2].toObject();
        QString data;
        if (protocol == "responses") { QCOMPARE(image["type"].toString(), QString("input_image")); data = image["image_url"].toString().section(',', 1); }
        else if (protocol == "anthropic-messages") { QCOMPARE(image["type"].toString(), QString("image")); data = image["source"].toObject()["data"].toString(); }
        else { QCOMPARE(image["type"].toString(), QString("image_url")); data = image["image_url"].toObject()["url"].toString().section(',', 1); }
        const auto decoded = QImage::fromData(QByteArray::fromBase64(data.toLatin1()));
        QCOMPARE(decoded.size(), QSize(160, 100)); QCOMPARE(decoded.pixelColor(0, 0), QColor(Qt::red));
        const auto result = done.first().first().toString();
        QVERIFY(result.contains("顶部登录按钮")); QVERIFY(result.contains("dx=40")); QVERIFY(result.contains("dy=20"));
        QVERIFY(result.contains("2 倍")); QVERIFY(result.contains("保留原有文字")); QVERIFY(result.contains("颜色改成蓝色"));
        QVERIFY(result.contains("item-1 / o1")); QVERIFY(result.contains("item-2 / o2"));
        QVERIFY(result.contains("登录页")); QVERIFY(result.contains("160 × 100"));
        QVERIFY(result.contains("DPR")); QVERIFY(result.contains("未知"));
        QCOMPARE(usage.size(), 1); QCOMPARE(usage.first()[0].toLongLong(), 124); QCOMPARE(usage.first()[1].toLongLong(), 30);
        QVERIFY(!client.isRunning());
    }
    void fullEndpointIsNotDuplicated() {
        FakeAiApi api; api.response = responseFor("responses", modelContent());
        auto settings = config(api); settings.baseUrl += "/responses/";
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed);
        client.generate(settings, "secret", input()); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(api.paths.first(), QByteArray("/v1/responses"));
    }
    void connectionTestUsesOnlyBuiltInImage() {
        FakeAiApi api; api.response = responseFor("responses", "BLUE");
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed);
        client.testConnection(config(api), "secret"); QTRY_COMPARE(done.size(), 1);
        const auto bytes = QJsonDocument(api.requests.first()).toJson();
        QVERIFY(!bytes.contains("itemsToInterpret")); QVERIFY(!bytes.contains("登录页"));
        QVERIFY(bytes.contains("input_image"));
        QVERIFY(!bytes.contains("BLUE")); // The text does not reveal the visual answer.
    }
    void missingDuplicateOrUnknownObjectsAreRejected_data() {
        QTest::addColumn<QStringList>("ids");
        QTest::newRow("missing-child") << QStringList{"item-1"};
        QTest::newRow("duplicate") << QStringList{"item-1", "item-1"};
        QTest::newRow("unknown") << QStringList{"item-1", "item-3"};
    }
    void missingDuplicateOrUnknownObjectsAreRejected() {
        QFETCH(QStringList, ids);
        FakeAiApi api; api.response = responseFor("responses", modelContent(ids));
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(errors.size(), 1); QCOMPARE(done.size(), 0);
    }
    void truncatedResponseFails_data() {
        QTest::addColumn<QString>("protocol"); QTest::addColumn<QString>("reason");
        QTest::newRow("responses-incomplete") << QString("responses") << QString("incomplete");
        QTest::newRow("chat-length") << QString("chat-completions") << QString("length");
        QTest::newRow("anthropic-max-tokens") << QString("anthropic-messages") << QString("max_tokens");
    }
    void truncatedResponseFails() {
        QFETCH(QString, protocol); QFETCH(QString, reason);
        FakeAiApi api; api.response = responseFor(protocol, modelContent(), reason);
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed), done(&client, &AiFeedbackClient::completed);
        client.generate(config(api, protocol), "secret", input()); QTRY_COMPARE(errors.size(), 1); QCOMPARE(done.size(), 0);
    }
    void unavailableUsageIsNotReportedAsZero_data() {
        QTest::addColumn<QJsonObject>("usage");
        QTest::newRow("missing") << QJsonObject{};
        QTest::newRow("partial") << QJsonObject{{"input_tokens", 1}};
        QTest::newRow("null") << QJsonObject{{"input_tokens", QJsonValue()}, {"output_tokens", QJsonValue()}};
        QTest::newRow("negative") << QJsonObject{{"input_tokens", -1}, {"output_tokens", 5}};
        QTest::newRow("fractional") << QJsonObject{{"input_tokens", 1.5}, {"output_tokens", 5}};
        QTest::newRow("string") << QJsonObject{{"input_tokens", "124"}, {"output_tokens", 30}};
    }
    void unavailableUsageIsNotReportedAsZero() {
        QFETCH(QJsonObject, usage);
        auto response = QJsonDocument::fromJson(responseFor("responses", modelContent())).object();
        response["usage"] = usage;
        FakeAiApi api; api.response = QJsonDocument(response).toJson();
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), counts(&client, &AiFeedbackClient::usageAvailable);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(counts.size(), 0);
    }
    void explicitZeroUsageIsPreserved() {
        auto response = QJsonDocument::fromJson(responseFor("responses", modelContent())).object();
        response["usage"] = QJsonObject{{"input_tokens", 0}, {"output_tokens", 0}};
        FakeAiApi api; api.response = QJsonDocument(response).toJson();
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), counts(&client, &AiFeedbackClient::usageAvailable);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(counts.size(), 1); QCOMPARE(counts.first()[0].toLongLong(), 0); QCOMPARE(counts.first()[1].toLongLong(), 0);
    }
    void credentialsOrImageEncodingsInSuccessfulRepliesAreRejected_data() {
        QTest::addColumn<QString>("injection");
        QTest::newRow("key-echo") << QString("test-secret-123456");
        QTest::newRow("image-echo") << QString("data:image/png;base64,SHOULD_NOT_LEAK");
    }
    void credentialsOrImageEncodingsInSuccessfulRepliesAreRejected() {
        QFETCH(QString, injection);
        auto content = QJsonDocument::fromJson(modelContent().toUtf8()).object();
        auto items = content["items"].toArray(); auto first = items[0].toObject();
        first["instruction"] = injection; items[0] = first; content["items"] = items;
        FakeAiApi api; api.response = responseFor("responses", QString::fromUtf8(QJsonDocument(content).toJson()));
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "test-secret-123456", input()); QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(done.size(), 0); QVERIFY(!errors.first().first().toString().contains(injection));
    }
    void oversizedApiReplyStopsReception() {
        FakeAiApi api; api.response = QByteArray(2 * 1024 * 1024 + 1, 'x');
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(done.size(), 0); QVERIFY(errors.first().first().toString().contains("2 MiB"));
        QVERIFY(!client.isRunning());
    }
    void oversizedUncertaintyIsRejected() {
        auto content = QJsonDocument::fromJson(modelContent().toUtf8()).object();
        auto items = content["items"].toArray(); auto first = items[0].toObject();
        first["uncertainty"] = QString(2001, 'x'); items[0] = first; content["items"] = items;
        FakeAiApi api; api.response = responseFor("responses", QString::fromUtf8(QJsonDocument(content).toJson()));
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(errors.size(), 1); QCOMPARE(done.size(), 0);
    }
    void exactGeometryAndUserAmbiguitySurviveModelInterpretation() {
        auto data = input(); auto objects = data.feedback["objects"].toArray(); auto first = objects[0].toObject();
        auto geometry = first["source"].toObject(); geometry["x1"] = 10.123456789012345; first["source"] = geometry;
        first["annotations"] = QJsonArray{"大概移到右边？", QJsonObject{{"number", 3}, {"text", "但不要改变顺序。\n保留两行"}}};
        objects[0] = first; data.feedback["objects"] = objects;
        auto image = data.feedback["image"].toObject(); image["devicePixelRatio"] = 1.25; data.feedback["image"] = image;
        auto content = QJsonDocument::fromJson(modelContent().toUtf8()).object(); auto items = content["items"].toArray();
        first = items[0].toObject(); first["uncertainty"] = "需确认向右移动的对齐基准。"; items[0] = first; content["items"] = items;
        FakeAiApi api; api.response = responseFor("responses", QString::fromUtf8(QJsonDocument(content).toJson()));
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed);
        client.generate(config(api), "secret", data); QTRY_COMPARE(done.size(), 1);
        const auto output = done.first().first().toString();
        const auto encoded = QJsonDocument(QJsonArray{geometry["x1"]}).toJson(QJsonDocument::Compact);
        QVERIFY(output.contains(QString::fromUtf8(encoded.mid(1, encoded.size() - 2))));
        QVERIFY(output.contains("DPR）：1.25")); QVERIFY(output.contains("大概移到右边？"));
        QVERIFY(output.contains("但不要改变顺序。\n保留两行")); QVERIFY(output.contains("需确认向右移动的对齐基准。"));
        QVERIFY(output.contains("所属对象：item-1")); QVERIFY(output.contains("dx=40"));
    }
    void kimiUsesCompletionTokenBudget() {
        FakeAiApi api; api.response = responseFor("chat-completions", modelContent());
        auto settings = config(api, "chat-completions"); settings.providerId = "kimi-cn";
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed);
        client.generate(settings, "secret", input()); QTRY_COMPARE(done.size(), 1);
        QVERIFY(api.requests.first().contains("max_completion_tokens"));
        QVERIFY(!api.requests.first().contains("max_tokens"));
    }
    void apiErrorRedactsKeyAndDoesNotRetry() {
        FakeAiApi api; api.status = 401;
        api.response = QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", "Invalid key test-secret-123456"}}}}).toJson();
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "test-secret-123456", input()); QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().contains("test-secret-123456")); QCOMPARE(api.requests.size(), 1);
    }
    void redirectDoesNotForwardCredentials() {
        FakeAiApi api; FakeAiApi destination;
        api.status = 307; api.extraHeaders = "Location: " + destination.base().toUtf8() + "/responses\r\n";
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(destination.requests.size(), 0); QCOMPARE(api.requests.size(), 1);
    }
    void cancellationFinishesAndAllowsNextRequest() {
        FakeAiApi api; api.respond = false;
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed), done(&client, &AiFeedbackClient::completed);
        client.generate(config(api), "secret", input()); QTRY_COMPARE(api.requests.size(), 1);
        QVERIFY(client.isRunning()); client.cancel(); QTRY_COMPARE(errors.size(), 1); QVERIFY(!client.isRunning());
        api.respond = true; api.response = responseFor("responses", modelContent());
        client.generate(config(api), "secret", input()); QTRY_COMPARE(done.size(), 1); QCOMPARE(api.requests.size(), 2);
    }
    void excessImagesAndFramesFailBeforeNetwork() {
        FakeAiApi api;
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed);
        auto data = input(); while (data.images.size() < 17) data.images.append(data.images.first()); data.imageLabels.clear();
        client.generate(config(api), "secret", data); QCOMPARE(errors.size(), 1); QCOMPARE(api.requests.size(), 0);
        data = input(); QJsonArray frames;
        for (int i = 0; i < 9; ++i) frames.append(QJsonObject{{"id", QString::number(i)}, {"timestampMs", i * 100}, {"timestampUs", i * 100000}, {"feedback", data.feedback}});
        data.feedback = QJsonObject{{"schemaVersion", "video-feedback-1"}, {"frames", frames}, {"objects", QJsonArray{}}};
        client.generate(config(api), "secret", data); QCOMPARE(errors.size(), 2); QCOMPARE(api.requests.size(), 0);
    }
    void providerDimensionLimitsFailBeforeNetwork() {
        FakeAiApi api;
        AiFeedbackClient client; QSignalSpy errors(&client, &AiFeedbackClient::failed);
        auto data = input(); data.images[0] = QImage(8001, 1, QImage::Format_RGB32); data.images[0].fill(Qt::white);
        client.generate(config(api, "anthropic-messages"), "secret", data);
        QCOMPARE(errors.size(), 1); QCOMPARE(api.requests.size(), 0);
        auto settings = config(api, "chat-completions"); settings.providerId = "deepseek";
        data.images[0] = QImage(8193, 1, QImage::Format_RGB32); data.images[0].fill(Qt::white);
        client.generate(settings, "secret", data); QCOMPARE(errors.size(), 2); QCOMPARE(api.requests.size(), 0);
    }
    void videoFramesKeepSeparateTimestampsAndSkipRootIndex() {
        FakeAiApi api; api.response = responseFor("responses", modelContent({"item-1", "item-2", "item-3", "item-4"}));
        auto data = input(); const auto imageFeedback = data.feedback;
        data.feedback = QJsonObject{{"schemaVersion", "video-feedback-1"},
                                      {"frames", QJsonArray{QJsonObject{{"id", "f1"}, {"timestampMs", 123}, {"timestampUs", 123456}, {"feedback", imageFeedback}},
                                                             QJsonObject{{"id", "f2"}, {"timestampMs", 222}, {"timestampUs", 222999}, {"feedback", imageFeedback}}}},
                                      {"objects", QJsonArray{QJsonObject{{"id", "duplicate-root-index"}}}}};
        AiFeedbackClient client; QSignalSpy done(&client, &AiFeedbackClient::completed);
        client.generate(config(api), "secret", data); QTRY_COMPARE(done.size(), 1);
        const auto output = done.first().first().toString();
        QVERIFY(output.contains("123 ms / 123456 µs")); QVERIFY(output.contains("222 ms / 222999 µs"));
        QVERIFY(!output.contains("duplicate-root-index"));
    }
};
QTEST_GUILESS_MAIN(AiFeedbackTests)
#include "aifeedback_test.moc"
