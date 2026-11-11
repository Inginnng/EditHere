#include "aicredentials.h"
#include "aifeedback.h"
#include "aiinstructionsdialog.h"
#include "aisettingspage.h"
#include "i18n.h"
#include "ui.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
using namespace h2d;
namespace {
QByteArray fixture(const QString &folder, const QString &name) {
    QFile file(QDir(folder).filePath(name));
    if (file.open(QIODevice::ReadOnly)) return file.readAll();
    file.setFileName(":/ai-samples/" + name);
    if (file.open(QIODevice::ReadOnly)) return file.readAll();
    return {};
}
class MemoryCredentials final : public AiCredentials {
  public:
    using AiCredentials::AiCredentials;
    QMap<QString, QString> keys;
    void load(const QString &id) override { QTimer::singleShot(0, this, [this, id] { emit loaded(id, keys.value(id)); }); }
    void save(const QString &id, const QString &key) override { keys[id] = key; emit saved(id); }
    void remove(const QString &id) override { keys.remove(id); emit removed(id); }
};
class LocalReplay final : public QObject {
  public:
    QTcpServer server;
    QByteArray content;
    bool failNext = false;
    explicit LocalReplay(QByteArray reply) : content(std::move(reply)) {
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
                    const auto request = QJsonDocument::fromJson(buffer->mid(boundary + 4, length)).object();
                    const bool test = QJsonDocument(request).toJson().contains("dominant color");
                    const QString text = test ? "BLUE" : QString::fromUtf8(content);
                    QJsonObject response{{"usage", QJsonObject{{"input_tokens", 812}, {"output_tokens", 196}}}};
                    if (request.contains("input")) {
                        response["status"] = "completed";
                        response["output"] = QJsonArray{QJsonObject{{"type", "message"}, {"content", QJsonArray{
                            QJsonObject{{"type", "output_text"}, {"text", text}}
                        }}}};
                    } else if (request.contains("system")) {
                        response["stop_reason"] = "end_turn";
                        response["content"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}};
                    } else {
                        response["choices"] = QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"role", "assistant"}, {"content", text}}}}};
                    }
                    const auto body = failNext ? QByteArray("{\"error\":{\"message\":\"本地示例：服务暂时不可用，请重试。\"}}") : QJsonDocument(response).toJson(QJsonDocument::Compact);
                    const bool failed = failNext; failNext = false;
                    QTimer::singleShot(700, socket, [socket, body, failed] {
                        socket->write(QByteArray("HTTP/1.1 ") + (failed ? "503 Error" : "200 OK") + "\r\nContent-Type: application/json\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    QUrl endpoint() const { return QUrl(QString("http://127.0.0.1:%1/replay").arg(server.serverPort())); }
};
void settingsDialog(AppSettings &settings, MemoryCredentials &credentials, LocalReplay *replay) {
    QDialog dialog;
    dialog.setWindowTitle(replay ? "AI 文字工作台 · 本地设置示例" : "AI 文字工作台 · 连接设置");
    dialog.resize(660, 760);
    auto *body = new QVBoxLayout(&dialog); body->setContentsMargins(0, 0, 0, 16);
    auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true);
    auto *page = new AiSettingsPage(scroll, &credentials); page->setSettings(settings); scroll->setWidget(page); body->addWidget(scroll, 1);
    if (replay) page->findChild<AiFeedbackClient *>()->setEndpointOverride(replay->endpoint());
    auto *back = textButton("保存并返回解析", true, &dialog); body->addWidget(back, 0, Qt::AlignHCenter);
    QObject::connect(back, &QPushButton::clicked, &dialog, [&] {
        page->saveCredentials([&](bool success) { if (success) { page->applyTo(settings); dialog.accept(); } });
    });
    dialog.exec();
}
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EditHereDevelopment");
    QCoreApplication::setApplicationName("AiTextWorkbench");
    QStandardPaths::setTestModeEnabled(true);
    QCommandLineParser parser; parser.addHelpOption(); parser.setApplicationDescription("EditHere AI 文字模块：复用真实界面的独立调试入口");
    parser.addOption({"live", "手动连接真实 API（配置和密钥只存本次内存）"});
    parser.addOption({"dark", "使用深色界面"});
    parser.addOption({"settings", "先打开连接设置"});
    parser.addOption({"fail-once", "本地示例的第一次请求返回错误"});
    parser.addOption({"fixtures", "示例输入目录（screen.svg、feedback.json、reply.json）", "folder"});
    parser.addOption({"render", "导出本地示例界面截图并退出", "folder"});
    parser.addOption({"render-settings", "导出连接设置与系统提示词窗口截图并退出", "folder"});
    parser.addOption({"no-keys", "不提供任何已保存的密钥，用于查看未配置状态"});
    parser.process(app);
    if (parser.isSet("live") && parser.isSet("render")) return 2;
    auto assets = QDir(QCoreApplication::applicationDirPath()).filePath("resources");
    if (!QDir(assets).exists()) assets = QDir(QCoreApplication::applicationDirPath()).filePath("../features/ai-text/resources");
    if (!QDir(assets).exists()) assets = QStringLiteral(AI_TEXT_SOURCE_DIR "/resources");
    if (qEnvironmentVariableIsEmpty("EDITHERE_AI_TEXT_ASSETS") && QDir(assets).exists())
        qputenv("EDITHERE_AI_TEXT_ASSETS", assets.toUtf8());
    auto folder = parser.value("fixtures");
    if (folder.isEmpty()) {
        folder = QDir(QCoreApplication::applicationDirPath()).filePath("../features/ai-text/workbench/fixtures");
        if (!QDir(folder).exists()) folder = QStringLiteral(AI_TEXT_SOURCE_DIR "/workbench/fixtures");
    }
#ifdef Q_OS_WIN
    const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
    for (const auto &name : {"msyh.ttc", "segoeui.ttf", "georgia.ttf"})
        QFontDatabase::addApplicationFont(fonts + name);
#endif
    QSvgRenderer renderer(fixture(folder, "screen.svg"));
    QImage image(renderer.defaultSize(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
    if (!renderer.isValid() || image.isNull()) return 3;
    { QPainter painter(&image); renderer.render(&painter); }
    AiFeedbackInput input{QJsonDocument::fromJson(fixture(folder, "feedback.json")).object(), {image}, {"原图"}};
    if (input.feedback.isEmpty()) return 4;
    if (!parser.isSet("fixtures") || QFile::exists(QDir(folder).filePath("screen-result.svg"))) {
        QSvgRenderer resultRenderer(fixture(folder, "screen-result.svg"));
        if (resultRenderer.isValid()) {
            QImage resultImage(resultRenderer.defaultSize(), QImage::Format_ARGB32_Premultiplied);
            resultImage.fill(Qt::white);
            { QPainter painter(&resultImage); resultRenderer.render(&painter); }
            input.images.append(resultImage); input.imageLabels.append("调整后画面");
        }
    }

    installLanguage(LanguageMode::SimplifiedChinese);
    applyTheme(parser.isSet("dark") ? ThemeMode::Dark : ThemeMode::Light);
    MemoryCredentials credentials;
    std::unique_ptr<LocalReplay> replay;
    if (!parser.isSet("live")) {
        replay = std::make_unique<LocalReplay>(fixture(folder, "reply.json"));
        if (!replay->server.isListening()) return 5;
        replay->failNext = parser.isSet("fail-once");
        if (!parser.isSet("no-keys"))
            for (const auto &preset : aiProviderPresets()) credentials.keys[preset.id] = "local-workbench-key";
    }
    AppSettings settings; settings.aiProviders["openai"] = defaultAiConnection();
    AiInstructionsDraft draft;
    if (parser.isSet("settings") || parser.isSet("live")) settingsDialog(settings, credentials, replay.get());
    if (parser.isSet("render-settings")) {
        const auto output = QDir(parser.value("render-settings"));
        if (!output.mkpath(".")) return 6;
        QDialog dialog;
        dialog.setWindowTitle("AI 文字工作台 · 连接设置");
        dialog.resize(660, 760);
        auto *body = new QVBoxLayout(&dialog); body->setContentsMargins(0, 0, 0, 16);
        auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true);
        auto *page = new AiSettingsPage(scroll, &credentials); page->setSettings(settings);
        scroll->setWidget(page); body->addWidget(scroll, 1);
        dialog.show();
        QApplication::processEvents();
        dialog.grab().save(output.filePath("settings.png"));
        QTimer::singleShot(60, &dialog, [&dialog, output] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *prompt = qobject_cast<QDialog *>(widget);
                    prompt && prompt->isVisible() && prompt->objectName() == "aiPromptDialog") {
                    prompt->grab().save(output.filePath("prompt.png"));
                    prompt->reject();
                }
        });
        if (auto *button = page->findChild<QPushButton *>("aiViewPrompt")) button->click();
        return 0;
    }
    for (;;) {
        AiInstructionsDialog dialog(settings, input, nullptr, &credentials, draft, &credentials);
        dialog.setWindowTitle(replay ? "AI 文字工作台 · 本地示例" : "AI 文字工作台 · 真实 API");
        auto *client = dialog.findChild<AiFeedbackClient *>();
        if (replay) client->setEndpointOverride(replay->endpoint());
        if (parser.isSet("render")) {
            dialog.resize(1120, 780);
            const auto output = QDir(parser.value("render"));
            if (!output.mkpath(".")) return 6;
            QObject::connect(client, &AiFeedbackClient::completed, &dialog, [&dialog, output, &app](const QString &text) {
                QTimer::singleShot(80, &dialog, [&dialog, output, text, &app] {
                    dialog.grab().save(output.filePath("result-light.png"));
                    QFile file(output.filePath("result.txt")); if (file.open(QIODevice::WriteOnly)) file.write(text.toUtf8());
                    applyTheme(ThemeMode::Dark);
                    QTimer::singleShot(50, &dialog, [&dialog, output, &app] {
                        dialog.grab().save(output.filePath("result-dark.png")); dialog.resize(780, 620);
                        QTimer::singleShot(50, &dialog, [&dialog, output, &app] {
                            dialog.grab().save(output.filePath("result-compact.png")); dialog.accept();
                        });
                    });
                });
            });
            QObject::connect(client, &AiFeedbackClient::failed, &dialog, [&dialog, output](const QString &message) {
                QFile file(output.filePath("error.txt")); if (file.open(QIODevice::WriteOnly)) file.write(message.toUtf8());
                dialog.reject();
            });
            QTimer::singleShot(150, &dialog, [&dialog, output] {
                dialog.grab().save(output.filePath("ready.png"));
                dialog.findChild<QPushButton *>("aiInstructionsGenerate")->click();
            });
            QTimer::singleShot(15000, &dialog, &QDialog::reject);
        }
        const int result = dialog.exec(); draft = dialog.draft();
        if (result != AiInstructionsDialog::Configure) return parser.isSet("render") && result != QDialog::Accepted ? 7 : 0;
        settingsDialog(settings, credentials, replay.get());
    }
}
