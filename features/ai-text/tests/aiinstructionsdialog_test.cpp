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
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>
#include <stdexcept>
using namespace h2d;

namespace {
class TestCredentials final : public AiCredentials {
  public:
    QString key = "test-dialog-key";
    // Only these providers report a stored key, so the dialog can tell
    // "configured once", "configured several times" and "not configured" apart.
    QStringList configured{"openai"};
    QString error;
    QString requestedProvider;
    int loads = 0;
    QMap<QString, int> loadsByProvider;
    void load(const QString &providerId) override {
        requestedProvider = providerId;
        ++loads;
        ++loadsByProvider[providerId];
        QTimer::singleShot(0, this, [this, providerId] {
            if (!error.isEmpty()) emit failed(error);
            else emit loaded(providerId, configured.contains(providerId) ? key : QString());
        });
    }
};
QJsonArray requestContent(const QJsonObject &request) {
    return request["input"].toArray().first().toObject()["content"].toArray();
}
QJsonObject requestTask(const QJsonObject &request) {
    return QJsonDocument::fromJson(requestContent(request).first().toObject()["text"].toString().toUtf8()).object();
}
class DialogApi final : public QObject {
  public:
    QTcpServer server;
    QJsonObject request;
    QList<QJsonObject> payloads;
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
                    payloads.append(request);
                    ++requests;
                    if (!respond) return;
                    QJsonArray items;
                    for (const auto &fact : requestTask(request)["itemsToInterpret"].toArray())
                        items.append(QJsonObject{{"id", fact.toObject()["id"]}, {"target", "顶部标题"},
                                                 {"instruction", "将标题改成蓝色，保持文字内容。"}, {"uncertainty", ""}});
                    const auto text = QString::fromUtf8(QJsonDocument(QJsonObject{{"items", items}}).toJson(QJsonDocument::Compact));
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
Document movedDocument() {
    QImage image(240, 160, QImage::Format_RGB32);
    image.fill(Qt::white);
    const QRect source(20, 20, 50, 30);
    QPainter painter(&image); painter.fillRect(source, Qt::red); painter.end();
    auto document = fromImage(image, "file", "Layout");
    document.candidates = {{source, manualTarget()}};
    document.layout = createLayout(image.size(), document.candidates);
    for (const auto &group : document.layout->groups) {
        if (group.originalBounds != QRectF(source)) continue;
        const auto id = group.id;
        transformLayoutGroup(*document.layout, id, QRectF(120, 80, 50, 30));
        break;
    }
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
    void largeSetSendsOnlyAnnotatedAndMovedPages() {
        const auto annotated = annotatedDocument(Qt::blue);
        const auto clean = fromImage(annotated.image, "file", "Unchanged");
        ImageSet set; set.name = "Project review";
        for (int i = 1; i <= 64; ++i)
            set.pages.append({QString("clean-%1").arg(i), "Unchanged", clean});
        set.pages[3] = {"settings", "Settings", annotated};
        set.pages[47] = {"dashboard", "Dashboard", movedDocument()};

        DialogApi api;
        TestCredentials credentials;
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(set), nullptr, &credentials, {}, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        QVERIFY(generate && output);
        QTRY_VERIFY(generate->isEnabled());
        generate->click();
        QTRY_VERIFY(output->toPlainText().contains("Dashboard"));
        QCOMPARE(api.requests, 1);
        QCOMPARE(api.request["instructions"].toString(), aiSystemPrompt());
        QVERIFY(api.request["instructions"].toString().startsWith("你负责将用户在 EditHere 中的图形化批注与几何调整"));

        const auto task = requestTask(api.request);
        const auto context = task["feedbackContext"].toObject();
        const auto pages = context["pages"].toArray();
        const auto facts = task["itemsToInterpret"].toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(facts.size(), 2);
        QCOMPARE(facts[0].toObject()["pageId"].toString(), QString("settings"));
        QCOMPARE(facts[1].toObject()["pageId"].toString(), QString("dashboard"));
        QVERIFY(facts[1].toObject()["movement"].isObject());
        QVERIFY(facts[1].toObject()["annotations"].toArray().isEmpty());
        QVERIFY(!QJsonDocument(task).toJson().contains("Unchanged"));
        const auto content = requestContent(api.request);
        QCOMPARE(content.size(), 7); // Task plus three label/image pairs.
        QVERIFY(content[1].toObject()["text"].toString().contains("Settings (settings)"));
        const auto pixel = [&](qsizetype index, const QPoint &point) {
            const auto data = content[index].toObject()["image_url"].toString().section(',', 1).toLatin1();
            return QImage::fromData(QByteArray::fromBase64(data)).pixelColor(point);
        };
        QCOMPARE(pixel(2, QPoint(0, 0)), QColor(Qt::blue));
        QCOMPARE(pixel(4, QPoint(140, 95)), QColor(Qt::white));
        QCOMPARE(pixel(6, QPoint(140, 95)), QColor(Qt::red));
        QVERIFY(output->toPlainText().contains("Settings"));
        QVERIFY(!output->toPlainText().contains("item-"));
    }
    void changedPagesOverImageLimitAreBatchedInOrder() {
        ImageSet set; set.name = "Large project";
        const auto annotated = annotatedDocument();
        for (int i = 1; i <= 18; ++i)
            set.pages.append({QString("page-%1").arg(i), QString("Screen %1").arg(i), i == 9 ? movedDocument() : annotated});
        set.groupNotes.append(QJsonObject{{"text", "统一侧边栏文案。"}});

        DialogApi api;
        AiFeedbackClient client;
        QSignalSpy done(&client, &AiFeedbackClient::completed), errors(&client, &AiFeedbackClient::failed),
                   usage(&client, &AiFeedbackClient::usageAvailable);
        const auto settings = api.settings();
        client.generate(settings.aiProviders.value(settings.aiProviderId), "dialog-test-secret", aiFeedbackSnapshot(set));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(api.requests, 2);
        int nextPage = 1, nextItem = 1, groupNotes = 0;
        for (const auto &request : api.payloads) {
            const auto task = requestTask(request);
            const auto pages = task["feedbackContext"].toObject()["pages"].toArray();
            qsizetype images = 0;
            for (const auto &page : pages) {
                QCOMPARE(page.toObject()["index"].toInt(), nextPage);
                images += nextPage++ == 9 ? 2 : 1;
            }
            QVERIFY(images <= 16);
            QCOMPARE(requestContent(request).size(), 1 + 2 * images);
            for (const auto &value : task["itemsToInterpret"].toArray()) {
                const auto fact = value.toObject();
                QCOMPARE(fact["id"].toString(), QString("item-%1").arg(nextItem++));
                if (!fact.contains("pageId")) ++groupNotes;
            }
        }
        QCOMPARE(nextPage, 19);
        QCOMPARE(nextItem, 20);
        QCOMPARE(groupNotes, 1);
        const auto result = done.first().first().toString();
        QVERIFY(result.contains("18. 第 18 页：Screen 18"));
        QVERIFY(result.contains("19. 整组意见"));
        QCOMPARE(usage.size(), 1);
        QCOMPARE(usage.first().first().toLongLong(), qint64(246));
    }
    void groupNotesWithoutPageChangesSendNoImages() {
        ImageSet set; set.name = "Project review";
        auto clean = annotatedDocument(); clean.notes.clear();
        set.pages.append({"home", "Home", clean});
        set.groupNotes.append(QJsonObject{{"text", "统一侧边栏文案。"}});
        DialogApi api;
        AiFeedbackClient client;
        QSignalSpy done(&client, &AiFeedbackClient::completed);
        const auto settings = api.settings();
        client.generate(settings.aiProviders.value(settings.aiProviderId), "dialog-test-secret", aiFeedbackSnapshot(set));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(api.requests, 1);
        QCOMPARE(requestContent(api.request).size(), 1);
        const auto facts = requestTask(api.request)["itemsToInterpret"].toArray();
        QCOMPARE(facts.size(), 1);
        QCOMPARE(facts.first().toObject()["annotations"].toArray().first().toString(), QString("统一侧边栏文案。"));
        QVERIFY(done.first().first().toString().contains("整组意见"));
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
        AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &credentials, {}, &credentials);
        dialog.resize(1080, 740);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *copy = dialog.findChild<QPushButton *>("aiInstructionsCopy");
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        auto *usage = dialog.findChild<QLabel *>("aiInstructionsUsage");
        QVERIFY(generate && copy && output && usage);
        QVERIFY(!generate->isEnabled());
        QVERIFY(!copy->isEnabled());
        dialog.show();
        QTRY_VERIFY(generate->isEnabled());
        QCOMPARE(credentials.loadsByProvider.value("openai"), 1);
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
        dialog.resize(740, 620);
        QTest::qWait(1);
        const auto sources = dialog.findChild<QWidget *>("aiAnalysisSources");
        QVERIFY(sources && sources->width() < 260);
        QVERIFY(output->width() >= 350);
        artifact(dialog, "ai-analysis-narrow-dark.png");
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
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials, {}, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
        auto *status = dialog.findChild<QLabel *>("aiInstructionsStatus");
        QVERIFY(generate && configure && status);
        dialog.show();
        QTRY_COMPARE(credentials.loadsByProvider.value("openai"), 1);
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
        changed.configured = {"kimi-cn"};
        AiInstructionsDialog reopened(api.settings("kimi-cn"), aiFeedbackSnapshot(annotatedDocument()), nullptr, &changed, {}, &changed);
        reopened.show();
        auto *newGenerate = reopened.findChild<QPushButton *>("aiInstructionsGenerate");
        QTRY_VERIFY(newGenerate->isEnabled());
        QCOMPARE(changed.requestedProvider, QString("kimi-cn"));
    }
    void closingDialogAbortsActiveRequestWithoutRequiringDestruction() {
        DialogApi api;
        api.respond = false;
        TestCredentials credentials;
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials, {}, &credentials);
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
    void stoppingRegenerationKeepsEditedTextVisibleAndEditable() {
        DialogApi api;
        TestCredentials credentials;
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials, {}, &credentials);
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        auto *cancel = dialog.findChild<QPushButton *>("aiInstructionsCancel");
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        auto *status = dialog.findChild<QLabel *>("aiInstructionsStatus");
        dialog.show(); QTRY_VERIFY(generate->isEnabled()); generate->click();
        QTRY_VERIFY(!output->toPlainText().isEmpty());
        output->appendPlainText("手动补充：按钮文字沿用原文。");
        const auto edited = output->toPlainText();
        api.respond = false; generate->click(); QTRY_COMPARE(api.requests, 2);
        QVERIFY(output->isVisible()); QVERIFY(output->isReadOnly());
        QCOMPARE(output->toPlainText(), edited);
        cancel->click(); QTRY_VERIFY(generate->isEnabled());
        QVERIFY(output->isVisible()); QVERIFY(!output->isReadOnly());
        QCOMPARE(output->toPlainText(), edited); QVERIFY(!status->property("error").toBool());
        // Clearing a draft keeps the editor open so users can write a replacement.
        output->clear(); QVERIFY(output->isVisible());
        QVERIFY(!dialog.findChild<QPushButton *>("aiInstructionsCopy")->isEnabled());
    }
    void perRunPreferencesAndDraftSurviveApiSettings() {
        DialogApi api;
        TestCredentials credentials;
        const auto snapshot = aiFeedbackSnapshot(annotatedDocument());
        AiInstructionsDraft draft{"手动编辑的说明", QString("使用中文，保持简短。")};
        AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &credentials, draft, &credentials);
        auto *extra = dialog.findChild<QLineEdit *>("aiRunInstructions");
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        dialog.show(); QTRY_VERIFY(generate->isEnabled());
        QCOMPARE(output->toPlainText(), draft.text); QVERIFY(output->isVisible());
        QCOMPARE(extra->text(), draft.extraInstructions.value());
        extra->setText("按优先级排列。"); generate->click(); QTRY_COMPARE(api.requests, 1);
        const auto content = api.request["input"].toArray().first().toObject()["content"].toArray();
        const auto task = QJsonDocument::fromJson(content.first().toObject()["text"].toString().toUtf8()).object();
        QCOMPARE(task["additionalUserPreferences"].toString(), extra->text());
        QTRY_VERIFY(generate->isEnabled()); output->appendPlainText("附加的一句。");
        const auto saved = dialog.draft();
        dialog.findChild<QPushButton *>("aiInstructionsSettings")->click();
        QCOMPARE(dialog.result(), int(AiInstructionsDialog::Configure));
        AiInstructionsDialog reopened(api.settings("kimi-cn"), snapshot, nullptr, &credentials, saved, &credentials);
        reopened.show();
        QCOMPARE(reopened.findChild<QPlainTextEdit *>("aiInstructionsText")->toPlainText(), saved.text);
        QCOMPARE(reopened.findChild<QLineEdit *>("aiRunInstructions")->text(), saved.extraInstructions.value());
    }
    void providerEntryShowsWhatIsActuallyConfigured() {
        DialogApi api;
        const auto snapshot = aiFeedbackSnapshot(annotatedDocument());
        {
            TestCredentials none;
            none.configured.clear();
            AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &none, {}, &none);
            auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
            QVERIFY(configure);
            QTRY_COMPARE(configure->text(), QString("配置 API…"));
            QVERIFY(configure->toolTip().contains("还没有配置"));
            QSignalSpy finished(&dialog, &QDialog::finished);
            configure->click();
            QCOMPARE(finished.size(), 1);
            QCOMPARE(finished.first().first().toInt(), int(AiInstructionsDialog::Configure));
        }
        {
            TestCredentials one;
            one.configured = {"deepseek"};
            AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &one, {}, &one);
            auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
            QTRY_COMPARE(configure->text(), QString("DeepSeek"));
            QVERIFY(!configure->menu());
        }
        {
            TestCredentials two;
            two.configured = {"openai", "deepseek"};
            AiInstructionsDialog dialog(api.settings(), snapshot, nullptr, &two, {}, &two);
            auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
            QTRY_COMPARE(configure->text(), QString("OpenAI"));
            QVERIFY(!configure->menu());
            auto *providerSwitch = dialog.findChild<QPushButton *>("aiInstructionsProviderSwitch");
            QVERIFY(providerSwitch && providerSwitch->menu());
            auto *menu = dialog.findChild<QMenu *>("aiProviderMenu");
            QVERIFY(menu);
            QCOMPARE(menu->actions().size(), 4);
            QAction *deepseek = nullptr;
            for (auto *action : menu->actions())
                if (action->text() == QString("DeepSeek")) deepseek = action;
            QVERIFY(deepseek);
            deepseek->trigger();
            QTRY_COMPARE(two.requestedProvider, QString("deepseek"));
        }
    }
    void providerChoiceSurvivesClosingAndReloadingSettings() {
        DialogApi api;
        TestCredentials credentials;
        credentials.configured = {"openai", "deepseek"};
        const auto snapshot = aiFeedbackSnapshot(annotatedDocument());
        auto preferences = api.settings();
        AiInstructionsDialog dialog(preferences, snapshot, nullptr, &credentials, {}, &credentials);
        dialog.show();
        auto *configure = dialog.findChild<QPushButton *>("aiInstructionsSettings");
        auto *providerSwitch = dialog.findChild<QPushButton *>("aiInstructionsProviderSwitch");
        QTRY_VERIFY(providerSwitch->isVisible() && providerSwitch->menu());
        QVERIFY(!configure->menu());
        QVERIFY(configure->geometry().right() < providerSwitch->geometry().left());
        for (auto *action : providerSwitch->menu()->actions())
            if (action->text() == "DeepSeek") action->trigger();
        QTRY_COMPARE(configure->text(), QString("DeepSeek"));
        dialog.reject();
        dialog.applyTo(preferences);
        QCOMPARE(preferences.aiProviderId, QString("deepseek"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        QString error;
        QVERIFY2(saveSettings(preferences, &error, path), qPrintable(error));
        TestCredentials reopenedCredentials;
        reopenedCredentials.configured = credentials.configured;
        AiInstructionsDialog reopened(loadSettings(path), snapshot, nullptr, &reopenedCredentials, {}, &reopenedCredentials);
        reopened.show();
        QTRY_COMPARE(reopened.findChild<QPushButton *>("aiInstructionsSettings")->text(), QString("DeepSeek"));
        QCOMPARE(api.requests, 0);
    }
    void systemPromptButtonEditsTheActualRequestAndKeepsCancelledChangesOut() {
        DialogApi api;
        TestCredentials credentials;
        AiInstructionsDialog dialog(api.settings(), aiFeedbackSnapshot(annotatedDocument()), nullptr, &credentials, {}, &credentials);
        dialog.show();
        auto *button = dialog.findChild<QPushButton *>("aiInstructionsPrompt");
        auto *generate = dialog.findChild<QPushButton *>("aiInstructionsGenerate");
        QVERIFY(button && button->isVisible());
        QTRY_VERIFY(generate->isEnabled());
        const QString custom = "按页面分组，用自然语言说明修改，不要输出坐标。";
        bool edited = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *prompt = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            const auto close = qScopeGuard([prompt] { if (prompt && prompt->isVisible()) prompt->reject(); });
            QVERIFY(prompt && prompt->objectName() == "aiPromptDialog");
            auto *text = prompt->findChild<QPlainTextEdit *>("aiBuiltinPrompt");
            QCOMPARE(text->toPlainText(), aiSystemPrompt());
            text->setPlainText(custom);
            edited = true;
            prompt->findChild<QPushButton *>("aiPromptSave")->click();
        });
        button->click();
        QVERIFY(edited);
        auto preferences = api.settings();
        dialog.applyTo(preferences);
        QCOMPARE(preferences.aiSystemPrompt, custom);
        QTimer::singleShot(0, &dialog, [&] {
            auto *prompt = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            const auto close = qScopeGuard([prompt] { if (prompt && prompt->isVisible()) prompt->reject(); });
            QVERIFY(prompt && prompt->objectName() == "aiPromptDialog");
            prompt->findChild<QPlainTextEdit *>("aiBuiltinPrompt")->setPlainText("未保存的修改");
            prompt->reject();
        });
        button->click();
        dialog.applyTo(preferences);
        QCOMPARE(preferences.aiSystemPrompt, custom);
        generate->click();
        QVERIFY(!button->isEnabled());
        QTRY_COMPARE(api.requests, 1);
        QCOMPARE(api.request["instructions"].toString(), custom);
        QTRY_VERIFY(button->isEnabled());
    }
};
QTEST_MAIN(AiInstructionsTests)
#include "aiinstructionsdialog_test.moc"
