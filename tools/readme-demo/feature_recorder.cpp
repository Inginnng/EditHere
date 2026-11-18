// README recordings of the production widgets on an isolated offscreen display.
// Source images are fictional, API responses are explicitly local replays, and
// neither user settings nor operating-system credentials are accessed.
#include "aicredentials.h"
#include "aifeedback.h"
#include "aifeedbackformat.h"
#include "aifeedbacksnapshot.h"
#include "aiinstructionsdialog.h"
#include "editor.h"
#include "i18n.h"
#include "imageset.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace h2d;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "output already exists or cannot be written");
    require(file.write(bytes) == bytes.size(), "could not write output");
}
class ReplayCredentials final : public AiCredentials {
  public:
    void load(const QString &id) override {
        QTimer::singleShot(0, this, [this, id] { emit loaded(id, id == "openai" ? "local-recording-placeholder" : ""); });
    }
};
class ReplayApi final : public QObject {
  public:
    QTcpServer server;
    int requests = 0;
    ReplayApi() {
        require(server.listen(QHostAddress::LocalHost, 0), "could not start local replay");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    const auto split = buffer->indexOf("\r\n\r\n");
                    if (*handled || split < 0) return;
                    qint64 length = -1;
                    for (const auto &line : buffer->left(split).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                    if (length < 0 || buffer->size() < split + 4 + length) return;
                    *handled = true; ++requests;
                    const auto request = QJsonDocument::fromJson(buffer->mid(split + 4, length)).object();
                    const auto task = QJsonDocument::fromJson(request["input"].toArray().first().toObject()["content"].toArray().first().toObject()["text"].toString().toUtf8()).object();
                    QJsonArray items;
                    for (const auto &value : task["itemsToInterpret"].toArray()) {
                        const auto fact = value.toObject();
                        const auto notes = aiOriginalNotes(fact);
                        QString target, instruction;
                        if (notes.contains("按钮")) {
                            target = "首屏左侧的主按钮";
                            instruction = "按钮改为深绿色，减小圆角，文案改为“探索系列”。保持它与左侧标题对齐。";
                        } else if (notes.contains("标题")) {
                            target = "首屏左侧的主标题";
                            instruction = "放大主标题，保留两行排版；副标题颜色调浅，拉开文字层级。";
                        } else {
                            target = "首屏右侧的椅子展示图";
                            instruction = "将展示图向右移动，使右边缘与上下分隔线的右端对齐。保持图片大小与垂直位置。";
                        }
                        items.append(QJsonObject{{"id", fact["id"]}, {"target", target}, {"instruction", instruction}});
                    }
                    const auto text = QString::fromUtf8(QJsonDocument(QJsonObject{{"items", items}}).toJson(QJsonDocument::Compact));
                    const auto body = QJsonDocument(QJsonObject{{"status", "completed"},
                        {"output", QJsonArray{QJsonObject{{"type", "message"}, {"content", QJsonArray{
                            QJsonObject{{"type", "output_text"}, {"text", text}}
                        }}}}}}).toJson(QJsonDocument::Compact);
                    QTimer::singleShot(900, socket, [socket, body] {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl endpoint() const { return QUrl(QString("http://127.0.0.1:%1/replay").arg(server.serverPort())); }
};

class Recording {
  public:
    Recording(QString root, QString output) : root_(std::move(root)), output_(std::move(output)) {}
    void run(const QString &scene) {
        require(scene == "image-set" || scene == "ai-interpretation", "unknown feature scene");
        frames_ = QDir(output_.filePath(scene + "-zh"));
        require(!frames_.exists(), "use a new frames folder; existing recordings are never removed");
        require(QDir().mkpath(frames_.path()), "cannot create frames folder");
        scene_ = scene;
        if (scene == "image-set") group(); else interpret();
        write(frames_.filePath("timeline.json"), QJsonDocument(QJsonObject{{"scene", scene}, {"language", "zh"},
            {"width", 1200}, {"height", 820}, {"frames", timeline_}}).toJson());
        std::printf("%s: %d frames, %.1f seconds\n", qPrintable(scene), int(timeline_.size()), elapsed_ / 1000.0);
    }
  private:
    QImage sample(int page = 0, bool revised = false) {
        QFile file(root_ + "/features/ai-text/workbench/fixtures/screen.svg");
        require(file.open(QIODevice::ReadOnly), "missing sample SVG");
        auto svg = file.readAll();
        // The workbench fixture carries painted note numbers. The editor adds
        // its own real annotations, so remove those example-only SVG groups.
        const auto markers = svg.indexOf("  <g fill=\"#3777cd\"");
        if (markers >= 0) svg = svg.left(markers) + "</svg>";
        if (page == 1) {
            svg.replace("A quieter kind", "The everyday").replace("of home.", "collection.");
            svg.replace("OBJECTS FOR EVERYDAY LIVING", "MADE FOR EVERYDAY MOMENTS");
            svg.replace("EXPLORE THE EDIT", "VIEW COLLECTION").replace("01 / 03", "02 / 03");
            svg.replace("#8b9a77", "#bd9375").replace("#6c7f5b", "#94725e");
        } else if (page == 2) {
            svg.replace("A quieter kind", "Designed with").replace("of home.", "care.");
            svg.replace("OBJECTS FOR EVERYDAY LIVING", "OUR STORY, OUR MATERIALS");
            svg.replace("EXPLORE THE EDIT", "MEET MORROW").replace("01 / 03", "03 / 03");
            svg.replace("#8b9a77", "#8694aa").replace("#6c7f5b", "#62748b");
        }
        if (revised) {
            svg.replace("#879179", "#294a35");
            if (page == 0) svg.replace("font-size=\"68\"", "font-size=\"74\"");
        }
        QSvgRenderer renderer(svg);
        require(renderer.isValid(), "invalid sample SVG");
        QImage image(1200, 800, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        QPainter painter(&image); renderer.render(&painter); painter.end();
        return image;
    }
    AppSettings settings() {
        auto result = defaultSettings(); result.theme = ThemeMode::Light;
        result.language = LanguageMode::SimplifiedChinese; result.regionOcrEnabled = false;
        result.confirmBeforeDiscard = false;
        return result;
    }
    void show(QWidget &widget) {
        widget.resize(1120, 680); widget.show(); QTest::qWait(50);
        widget.setGeometry(40, 48, 1120, 680); QTest::qWait(100);
    }
    void caption(const QString &title, const QString &detail) { title_ = title; detail_ = detail; }
    void frame(int ms) {
        QApplication::processEvents();
        QImage image(2400, 1640, QImage::Format_RGB32); image.setDevicePixelRatio(2); image.fill(QColor("#edf2fa"));
        QPainter painter(&image); painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QFont font("Microsoft YaHei UI"); font.setPixelSize(13); painter.setFont(font); painter.setPen(QColor("#6b7890"));
        painter.drawText(QRect(42, 14, 1116, 22), Qt::AlignLeft, "EditHere · 真实界面录制");
        painter.drawText(QRect(42, 14, 1116, 22), Qt::AlignRight, scene_ == "ai-interpretation" ? "示例素材 · API 响应本地回放" : "示例项目 · 组图评审");
        auto windows = QApplication::topLevelWidgets();
        std::stable_sort(windows.begin(), windows.end(), [](QWidget *a, QWidget *b) {
            return int(qobject_cast<QDialog *>(a) != nullptr) < int(qobject_cast<QDialog *>(b) != nullptr);
        });
        for (auto *widget : windows) {
            if (!widget->isVisible() || !widget->isWindow()) continue;
            painter.drawPixmap(widget->pos(), widget->grab());
        }
        if (!cursor_.isNull()) {
            painter.save(); painter.translate(cursor_); painter.setBrush(Qt::white); painter.setPen(QPen(QColor("#162437"), 1.5));
            QPainterPath pointer; pointer.moveTo(0, 0); pointer.lineTo(0, 22); pointer.lineTo(6, 17); pointer.lineTo(11, 28);
            pointer.lineTo(16, 26); pointer.lineTo(11, 15); pointer.lineTo(20, 15); pointer.closeSubpath(); painter.drawPath(pointer); painter.restore();
        }
        painter.setPen(Qt::NoPen); painter.setBrush(QColor("#172a45")); painter.drawRoundedRect(QRect(40, 746, 1120, 60), 12, 12);
        font.setPixelSize(20); font.setBold(true); painter.setFont(font); painter.setPen(Qt::white);
        painter.drawText(QRect(60, 751, 1080, 26), Qt::AlignLeft, title_);
        font.setPixelSize(14); font.setBold(false); painter.setFont(font); painter.setPen(QColor("#c9d8ee"));
        painter.drawText(QRect(60, 779, 1080, 21), Qt::AlignLeft, detail_); painter.end();
        image = image.scaled(1200, 820, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); image.setDevicePixelRatio(1);
        const auto name = QString("%1.png").arg(timeline_.size(), 5, 10, QChar('0'));
        require(image.save(frames_.filePath(name)), "cannot save recording frame");
        timeline_.append(QJsonObject{{"file", name}, {"ms", ms}}); elapsed_ += ms;
    }
    void move(QWidget *widget, QPoint point) {
        const auto target = widget->mapToGlobal(point);
        const auto start = cursor_.isNull() ? QPointF(target - QPoint(30, 20)) : cursor_;
        for (int step = 1; step <= 6; ++step) {
            const double t = double(step) / 6; cursor_ = start + (QPointF(target) - start) * (t * t * (3 - 2 * t)); frame(40);
        }
    }
    void click(QWidget *widget, QPoint point = {}) {
        require(widget && widget->isVisible() && widget->isEnabled(), "button is unavailable");
        if (point.isNull()) point = widget->rect().center();
        move(widget, point); QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(60); frame(200);
    }
    QPushButton *button(QWidget &widget, const char *name) {
        auto *found = widget.findChild<QPushButton *>(name); require(found, name); return found;
    }
    void type(QPlainTextEdit *input, const QString &text) {
        require(input, "no text editor"); input->setFocus();
        for (int i = 0; i < text.size(); i += 3) { input->insertPlainText(text.mid(i, 3)); frame(70); }
    }
    void note(Editor &editor, const QRect &pixels, const QString &text) {
        click(button(editor, "mode_rect")); auto *canvas = editor.canvas();
        const auto a = (QPointF(pixels.topLeft()) * canvas->zoom()).toPoint();
        const auto b = (QPointF(pixels.bottomRight()) * canvas->zoom()).toPoint();
        move(canvas, a); QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, a);
        for (int i = 1; i <= 8; ++i) { auto point = a + (b - a) * i / 8; QTest::mouseMove(canvas, point); cursor_ = canvas->mapToGlobal(point); frame(40); }
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, b); QTest::qWait(80);
        require(!editor.document().notes.isEmpty(), "note was not created");
        auto *input = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        type(input, text); QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier); frame(900);
    }
    ImageSet project(bool revised = false) {
        ImageSet set; set.name = "Morrow · 网站页面";
        const QStringList names{"首页", "系列介绍", "品牌故事"};
        const QStringList ids{"home", "collection", "story"};
        for (int i = 0; i < 3; ++i) set.pages.append({ids[i], names[i], fromImage(sample(i, revised && i < 2), "file", names[i])});
        return set;
    }
    void group() {
        Editor editor; editor.setPreferences(settings()); editor.setImageSet(project()); editor.setAgentSession(true); show(editor); editor.fit();
        QTest::mouseClick(button(editor, "toggleMagnifier"), Qt::LeftButton);
        auto *tabs = editor.findChild<QTabBar *>("imageSetTabs"); require(tabs, "no image set tabs");
        caption("01 / 集中查看项目页面", "同一组图保留首页、系列介绍和品牌故事，切页分别批注。"); frame(1600);
        caption("02 / 在首页圈出修改位置", "批注与原图分开保存，每一条意见都绑定页面与像素区域。");
        note(editor, QRect(54, 258, 540, 168), "主标题再大一些，保留两行排版。");
        click(tabs, tabs->tabRect(1).center()); editor.fit();
        caption("03 / 切到另一页继续批注", "页面独立保留意见，也可以添加作用于整组页面的要求。");
        note(editor, QRect(60, 546, 252, 74), "按钮改成深绿色，与首页的主按钮统一。");
        click(tabs, tabs->tabRect(2).center()); editor.fit(); frame(1000);
        auto *modalPoll = new QTimer(&editor); modalPoll->setInterval(30);
        QObject::connect(modalPoll, &QTimer::timeout, &editor, [&, modalPoll] {
            auto *dialog = QApplication::activeModalWidget();
            if (!dialog) return;
            modalPoll->stop(); modalPoll->deleteLater();
            dialog->setGeometry(310, 195, 580, 330); QTest::qWait(60);
            caption("整组意见 / 一次表达共同要求", "跨页面的设计要求不用在每一页重复填写。");
            type(dialog->findChild<QPlainTextEdit *>(), "全站主按钮统一使用深绿色。"); frame(700);
            auto *box = dialog->findChild<QDialogButtonBox *>(); click(box->button(QDialogButtonBox::Save));
        });
        modalPoll->start();
        click(button(editor, "imageSetGroupNotes"));
        bool submitted = false;
        QObject::connect(&editor, &Editor::agentFinishRequested, &editor, [&] {
            FeedbackExportOptions options; options.mode = ImageExportMode::File;
            const auto bundle = editor.agentFeedbackBundle(options);
            const auto json = QJsonDocument::fromJson(bundle.json).object();
            require(json["pages"].toArray().size() == 2 && json["pageIndex"].toArray().size() == 3, "unexpected image-set feedback");
            write(frames_.filePath("feedback.json"), bundle.json);
            for (auto it = bundle.files.begin(); it != bundle.files.end(); ++it) write(frames_.filePath(it.key()), it.value());
            write(frames_.filePath("review.edithere"), editor.projectBytes());
            editor.markFeedbackSubmitted(); editor.setAgentSession(false); submitted = true;
        });
        caption("04 / 完成并返回 AI", "本轮回传 2 个批注页面和整组意见；未批注页只留索引。");
        click(button(editor, "agentFinish")); require(submitted, "feedback was not submitted"); frame(1800);
        editor.setImageSet(project(true)); editor.setAgentSession(true); editor.fit();
        caption("05 / 更新截图，继续复审", "示例更新首页与系列页，沿用页面 ID；品牌故事页保持原图。"); frame(1800);
        click(tabs, tabs->tabRect(1).center()); editor.fit(); frame(2000);
        write(frames_.filePath("review-updated.edithere"), editor.projectBytes());
        editor.markFeedbackSubmitted(); editor.hide();
    }
    void interpret() {
        auto document = fromImage(sample(), "file", "Morrow · 首页");
        Note heading; heading.isPoint = false; heading.rect = QRect(54, 258, 540, 168); heading.comment = "主标题再大一些，副标题调浅。";
        Note cta; cta.isPoint = false; cta.rect = QRect(60, 546, 252, 74); cta.comment = "按钮改深绿色，文案改为“探索系列”。";
        document.notes = {heading, cta};
        const QRect source(610, 168, 478, 492);
        document.candidates = {{source, manualTarget()}}; document.layout = createLayout(document.image.size(), document.candidates);
        const auto id = document.layout->groups.first().id;
        transformLayoutGroup(*document.layout, id, QRectF(658, 168, 478, 492));
        Editor editor; editor.setPreferences(settings()); editor.setDocument(document); show(editor); editor.fit();
        QTest::mouseClick(button(editor, "toggleMagnifier"), Qt::LeftButton);
        caption("01 / 截图、批注与布局调整", "将本轮视觉意见整理成另一位 Agent 可以直接阅读的文字需求。"); frame(1700);
        ReplayApi api; ReplayCredentials credentials; auto config = settings();
        config.aiProviders["openai"] = defaultAiConnection(); config.aiProviders["openai"].model = "demo-vision · 本地回放";
        AiInstructionsDialog dialog(config, aiFeedbackSnapshot(document), &editor, &credentials, {}, &credentials);
        dialog.findChild<AiFeedbackClient *>()->setEndpointOverride(api.endpoint());
        auto *trigger = button(editor, "generateInstructions");
        // Only this recording host substitutes the credential source and endpoint.
        // The production editor, snapshot, dialog, client and formatter are reused.
        QObject::disconnect(trigger, &QPushButton::clicked, &editor, &Editor::generateInstructions);
        QObject::connect(trigger, &QPushButton::clicked, &dialog, [&] { show(dialog); });
        click(trigger); QTest::qWait(120);
        caption("02 / 核对画面，开始解析", "左侧保留原图与每条批注；本演示使用本地响应，不调用付费 API。"); frame(1900);
        auto *generate = button(dialog, "aiInstructionsGenerate");
        require(generate->isEnabled(), "local credentials are not ready"); click(generate);
        caption("03 / 解析图文与几何变化", "真实请求交给本机回放服务，再由程序校验并整理为文字。");
        for (int i = 0; i < 9; ++i) { QTest::qWait(160); frame(160); }
        auto *output = dialog.findChild<QPlainTextEdit *>("aiInstructionsText");
        require(output && output->toPlainText().contains("主按钮") && api.requests == 1, "local interpretation did not complete");
        caption("04 / 核对并编辑修改说明", "对象身份与修改要求变成可编辑文字，目标对话无需再接收整张图。"); frame(3400);
        output->moveCursor(QTextCursor::End); output->insertPlainText("\n\n补充：其余页面内容保持不变。"); frame(1500);
        click(button(dialog, "aiInstructionsCopy"));
        require(QApplication::clipboard()->text() == output->toPlainText(), "copied text differs from result");
        caption("05 / 复制文字，交给目标 Agent", "支持复制、编辑与导出；实际使用时需自行配置模型 API 并核对结果。"); frame(3000);
        write(frames_.filePath("instructions.txt"), output->toPlainText().toUtf8());
        dialog.hide(); editor.markFeedbackSubmitted(); editor.hide();
    }
    QString root_, scene_, title_, detail_;
    QDir output_, frames_;
    QPointF cursor_;
    QJsonArray timeline_;
    qint64 elapsed_ = 0;
};
}

int main(int argc, char **argv) {
    std::fprintf(stderr, "[recorder] starting\n"); std::fflush(stderr);
    if (argc != 4) { std::fprintf(stderr, "usage: readme_feature_recorder <image-set|ai-interpretation> <root> <new-output-folder>\n"); return 2; }
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "2");
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    std::fprintf(stderr, "[recorder] creating isolated display\n"); std::fflush(stderr);
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); QStandardPaths::setTestModeEnabled(true);
    std::fprintf(stderr, "[recorder] loading fonts\n"); std::fflush(stderr);
    for (const auto &name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "georgia.ttf"})
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/" + name);
    installLanguage(LanguageMode::SimplifiedChinese); applyTheme(ThemeMode::Light);
    std::fprintf(stderr, "[recorder] recording scene\n"); std::fflush(stderr);
    try { Recording(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3])).run(QString::fromLocal8Bit(argv[1])); }
    catch (const std::exception &error) { std::fprintf(stderr, "readme_feature_recorder: %s\n", error.what()); return 1; }
    return 0;
}
