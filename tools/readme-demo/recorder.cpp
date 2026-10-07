// Records the animated demonstrations in the README from the real interface.
//
// Every window, button, canvas and dialog in the frames is EditHere's own widget,
// driven through the same window-system input path a mouse and keyboard take, on
// an offscreen screen rendered at twice the logical size. Only the desktop behind
// the windows (rendered from tools/readme-demo/scenes), the pointer, the captions
// and the agent's terminal are painted by this tool. The agent scene sends a real
// `annotate` request over the local socket and prints the real reply.
//
//   readme_recorder <zh|en> <scene|all> <project root> <output folder>
//
// tools/readme-demo/render.py builds, runs and turns the frames into GIFs.
#include "agentprotocol.h"
#include "agentserver.h"
#include "canvas.h"
#include "capturetoolbar.h"
#include "controller.h"
#include "editor.h"
#include "explosion.h"
#include "i18n.h"
#include "ocrdialog.h"
#include "overlay.h"
#include "pinwindow.h"
#include "settings.h"
#include "ui.h"
#include "videoplayback.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFutureWatcherBase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QStandardPaths>
#include <QStyleOptionSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace h2d {
namespace {
constexpr int kWidth = 1200, kHeight = 750, kScale = 2;
const QColor kAccent(47, 117, 240);

bool verbose() {
    static const bool on = qEnvironmentVariableIsSet("EDITHERE_RECORDER_TRACE");
    return on;
}
void trace(const char *step) {
    if (verbose()) {
        std::fprintf(stderr, "[recorder] %s\n", step);
        std::fflush(stderr);
    }
}
void require(bool value, const QString &message) {
    if (!value)
        throw std::runtime_error(message.toStdString());
}

double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t < .5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

// The captions shown under each scene, in the two interface languages. The part
// before "|" is the step name and is drawn in the accent colour.
QString caption(const QString &id, bool zh) {
    static const QHash<QString, QPair<QString, QString>> text{
        {"cap.hotkey", {"截图|按下全局快捷键，冻结当前屏幕", "Capture|Press the global shortcut to freeze the screen"}},
        {"cap.hover", {"智能选块|悬停即识别界面元素，滚轮切换更大或更小的范围",
                       "Smart select|Hover to detect elements; scroll to widen or narrow"}},
        {"cap.click", {"截图工具条|单击定下选区，工具条与样式列随之出现",
                       "Capture toolbar|Click to settle; the toolbar and style column appear"}},
        {"cap.style", {"样式|圆角、阴影与边框所见即所得", "Style|Corners, shadow and border, previewed live"}},
        {"cap.pin", {"贴图|钉回原来的位置，置顶对照着改", "Pin|Pinned back where it was, on top of everything"}},
        {"cap.pinmove", {"贴图|随意拖动、缩放，多张同时钉在屏幕上", "Pin|Drag and resize; pin as many as you like"}},
        {"ocr.select", {"文字识别|框选需要读取文字的区域", "Text recognition|Select the region to read"}},
        {"ocr.run", {"文字识别|系统自带引擎在本机识别，不联网", "Text recognition|Runs on the system engine, fully offline"}},
        {"ocr.lines", {"文字识别|逐行列出，点一行就在原图上高亮", "Text recognition|Line by line; pick one to see it on the image"}},
        {"scroll.select", {"长截图|框选滚动区域，点击「长截图」", "Scrolling capture|Select the scrolling area and start"}},
        {"scroll.run", {"长截图|边滚动边拼接，右侧预览随内容延伸",
                        "Scrolling capture|Stitches as you scroll; the preview grows alongside"}},
        {"scroll.done", {"长截图|完成后直接进入批注", "Scrolling capture|Finish straight into annotation"}},
        {"ann.open", {"批注|选好区域，点「批注」进入编辑器", "Annotate|Pick a region and open it in the editor"}},
        {"ann.smart", {"智能选块|点中识别出的元素，直接写意见", "Smart select|Click a detected element and write the note"}},
        {"ann.point", {"点批注|精确指向某一个位置", "Point note|Pin a note to one exact spot"}},
        {"ann.rect", {"框批注|拖出任意范围", "Box note|Drag out any area"}},
        {"ann.global", {"全局意见|针对整个画面的要求", "Global note|Feedback about the whole picture"}},
        {"ann.json", {"导出|位置、范围与意见整理成一份 JSON", "Export|Positions, areas and notes in one JSON file"}},
        {"exp.button", {"大爆炸|一键把画面拆成可移动的组件", "Explode|Split the picture into movable parts"}},
        {"exp.move", {"大爆炸|拖到想要的位置，移动自动记为一条批注",
                      "Explode|Drag a part where it belongs; the move becomes a note"}},
        {"exp.resize", {"大爆炸|拖角点等比缩放，右侧可输入精确数值",
                        "Explode|Drag a corner to resize, or type exact values"}},
        {"exp.fill", {"大爆炸|原位置留空，交给 AI 补上应有的内容",
                      "Explode|The old spot stays empty for your AI to fill"}},
        {"vid.open", {"视频标注|打开视频，播放或拖动时间轴", "Video|Open a video, play or scrub the timeline"}},
        {"vid.pause", {"视频标注|暂停在需要修改的画面上批注", "Video|Pause on the frame and annotate it"}},
        {"vid.second", {"视频标注|继续播放，在另一个时间点再批注", "Video|Play on and annotate another moment"}},
        {"vid.review", {"视频标注|时间轴标签随时回到已批注的画面", "Video|Timeline tags bring annotated frames back"}},
        {"vid.json", {"视频标注|每条意见都带时间戳与帧截图", "Video|Every note carries its timestamp and frame"}},
        {"ai.ask", {"交给 AI|在 AI 工具里一句话发起标注", "Hand off to AI|Ask your AI tool to start a review"}},
        {"ai.open", {"交给 AI|EditHere 打开画面，AI 等待你的意见",
                     "Hand off to AI|EditHere opens the picture while the AI waits"}},
        {"ai.notes", {"交给 AI|圈出位置、写下意见", "Hand off to AI|Mark the spots, write the notes"}},
        {"ai.finish", {"交给 AI|点「完成并返回 AI」，反馈即刻送达", "Hand off to AI|Click Finish and return to AI"}},
        {"ai.apply", {"交给 AI|AI 读取结构化反馈，按意见修改", "Hand off to AI|The AI reads the feedback and makes the changes"}},
    };
    const auto entry = text.value(id);
    require(!entry.first.isEmpty(), "missing caption " + id);
    return zh ? entry.first : entry.second;
}

// The text the demo types into notes, in both languages.
QString words(const QString &id, bool zh) {
    static const QHash<QString, QPair<QString, QString>> text{
        {"ann.smart", {"转化率下降要更醒目，改成红色标签", "Make the conversion drop stand out with a red tag"}},
        {"ann.point", {"7 月数据异常，请核对统计口径", "July looks off, please check the source data"}},
        {"ann.rect", {"图例改成单列，与环形图对齐", "Stack the legend in one column next to the chart"}},
        {"ann.global", {"整体留白再大一些，卡片圆角统一", "More breathing room overall, consistent corners"}},
        {"exp.move", {"导出按钮挪到标题旁边", "Move Export next to the title"}},
        {"exp.resize", {"放大渠道构成卡片", "Make the channel mix card larger"}},
        {"vid.first", {"敌人血条太细，加粗到 6 px", "Enemy health bar is too thin, make it 6 px"}},
        {"vid.second", {"任务面板挡住视野，缩小一半", "The quest panel blocks the view, halve it"}},
        {"ai.ask", {"用 EditHere 让我标注一下仪表盘页面", "Let me mark up the dashboard in EditHere"}},
        {"ai.first", {"突出本月营收，加品牌色描边", "Highlight revenue with a brand-colour outline"}},
        {"ai.second", {"导出按钮改成品牌主色", "Make Export the brand colour"}},
        {"ai.global", {"去掉左下角的升级卡片", "Remove the upgrade card at the bottom left"}},
        {"ai.wait", {"正在打开 EditHere，等你完成标注…", "Opening EditHere and waiting for your review…"}},
        {"ai.read", {"收到 %1 条意见：", "Got %1 notes:"}},
        {"ai.edit", {"正在修改 src/Dashboard.tsx …", "Editing src/Dashboard.tsx …"}},
        {"ai.done", {"已按反馈完成修改", "Done, every note is addressed"}},
    };
    const auto entry = text.value(id);
    require(!entry.first.isEmpty(), "missing words " + id);
    return zh ? entry.first : entry.second;
}

// A terminal of an AI coding agent. It is a plain window so it stacks with the
// application's windows; what it says is scripted, apart from the reply line.
class Terminal final : public QWidget {
  public:
    struct Line {
        QString text;
        QColor color;
    };
    Terminal() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
    }
    void add(const QString &text, QColor color = QColor(226, 232, 240)) {
        lines_.append({text, color});
        update();
    }
    void typeLast(const QString &text) {
        lines_.last().text += text;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        p.setPen(QColor(51, 65, 85));
        p.setBrush(QColor(15, 23, 42));
        p.drawRoundedRect(frame, 12, 12);
        QPainterPath top;
        top.addRoundedRect(frame.adjusted(0, 0, 0, -(frame.height() - 34)), 12, 12);
        p.fillRect(QRectF(1, 22, width() - 2, 12), QColor(30, 41, 59));
        p.fillPath(top, QColor(30, 41, 59));
        int x = 16;
        for (const QColor &dot : {QColor(248, 113, 113), QColor(251, 191, 36), QColor(52, 211, 153)}) {
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(QPointF(x, 17), 5, 5);
            x += 18;
        }
        QFont title;
        title.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
        title.setPixelSize(12);
        p.setFont(title);
        p.setPen(QColor(148, 163, 184));
        p.drawText(QRectF(0, 0, width(), 34), Qt::AlignCenter, "AI agent — ~/lumen");
        QFont mono;
        mono.setFamilies({"Consolas", "Microsoft YaHei UI"});
        mono.setPixelSize(14);
        p.setFont(mono);
        const QFontMetricsF metrics(mono);
        double y = 52;
        const double wrapWidth = width() - 36;
        for (const auto &line : lines_) {
            p.setPen(line.color);
            const QRectF bounds = metrics.boundingRect(QRectF(18, y, wrapWidth, 1000),
                                                       Qt::TextWrapAnywhere, line.text);
            p.drawText(QRectF(18, y, wrapWidth, bounds.height()), Qt::TextWrapAnywhere, line.text);
            y += bounds.height() + 6;
        }
    }

  private:
    QVector<Line> lines_;
};
} // namespace

class ReadmeRecorder {
  public:
    ReadmeRecorder(const QString &language, const QString &root, const QString &output)
        : zh_(language == "zh"), root_(root), material_(root + "/tools/readme-demo/material"),
          output_(output) {
        for (const QString &kind : {"arrow", "link", "move", "nwse", "nesw", "ew", "ns", "beam"}) {
            QImage image(material_.filePath("cursors/" + kind + ".png"));
            require(!image.isNull(), "missing cursor " + kind);
            cursors_.insert(kind, image);
        }
        hotspots_ = {{"arrow", {0, 0}}, {"link", {13, 0}}, {"move", {22, 22}}, {"nwse", {16, 16}},
                     {"nesw", {16, 16}}, {"ew", {24, 9}}, {"ns", {9, 22}}, {"beam", {32, 32}}};
        icon_ = QImage(root + "/assets/icons/edithere-256.png");
    }

    void run(const QString &scene) {
        const QStringList all{"hero", "capture", "ocr", "scrolling", "annotate", "explode", "video"};
        for (const auto &name : scene == "all" ? all : QStringList{scene}) {
            require(all.contains(name), "unknown scene " + name);
            begin(name);
            if (name == "hero") hero();
            else if (name == "capture") capture();
            else if (name == "ocr") ocr();
            else if (name == "scrolling") scrolling();
            else if (name == "annotate") annotate();
            else if (name == "explode") explode();
            else video();
            end();
        }
    }

  private:
    // ------------------------------------------------------------------ session

    void begin(const QString &scene) {
        trace("begin");
        scene_ = scene;
        frames_ = QDir(output_.filePath(scene + "-" + (zh_ ? "zh" : "en")));
        if (frames_.exists())
            frames_.removeRecursively();
        require(QDir().mkpath(frames_.path()), "cannot create " + frames_.path());
        timeline_ = {};
        last_ = {};
        frameNumber_ = 0;
        clock_ = 0;
        caption_.clear();
        ripples_.clear();
        titleOpacity_ = 0;
        cursor_ = {640, 430};
        fadeTo_ = {};
        fade_ = 0;
        state_ = std::make_unique<QTemporaryDir>();
        require(state_->isValid(), "no temporary folder");
        AppSettings settings = defaultSettings();
        settings.language = zh_ ? LanguageMode::SimplifiedChinese : LanguageMode::English;
        settings.ocrLanguage = zh_ ? OcrLanguageMode::SimplifiedChinese : OcrLanguageMode::English;
        settings.theme = ThemeMode::Light;
        settings.shortcuts["capture"] = {}; // Never register a real global hotkey.
        settings.shortcuts["annotate"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        settings.confirmBeforeDiscard = false;
        trace("controller");
        owner_ = std::make_unique<Controller>(nullptr, settings, state_->filePath("settings.ini"));
        trace("controller ready");
        owner_->shortcut_.stop();
        owner_->history_ = CaptureHistory(state_->filePath("history"));
        owner_->captureIo_.prepare = [](QObject *, std::function<void()> ready, bool) { ready(); };
        owner_->captureIo_.grab = [this](CaptureCallback done) {
            ScreenFrame frame;
            frame.name = "stage";
            frame.logicalGeometry = {0, 0, kWidth, kHeight};
            frame.nativeGeometry = {0, 0, kWidth * kScale, kHeight * kScale};
            frame.image = screen_;
            frame.nativePixels = true;
            frame.elementProbingAllowed = false; // No accessibility probe of the real desktop.
            done({frame}, {});
        };
        setScreen(material("desktop"));
    }

    void end() {
        QFile file(frames_.filePath("timeline.json"));
        require(file.open(QIODevice::WriteOnly), "cannot write timeline");
        file.write(QJsonDocument(QJsonObject{{"scene", scene_}, {"language", zh_ ? "zh" : "en"},
                                             {"width", kWidth}, {"height", kHeight},
                                             {"frames", timeline_}})
                       .toJson());
        file.close();
        if (terminal_) {
            delete terminal_;
            terminal_ = nullptr;
        }
        owner_->clearOverlays();
        for (auto *pin : owner_->pins_)
            pin->close();
        if (owner_->ocrDialog_)
            owner_->ocrDialog_->close();
        owner_->editor_.hide();
        owner_.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        state_.reset();
        std::printf("%s-%s: %d frames, %.1f s\n", qPrintable(scene_), zh_ ? "zh" : "en",
                    int(timeline_.size()), clock_ / 1000.0);
        std::fflush(stdout);
    }

    QImage material(const QString &name) const {
        QImage image(material_.filePath(name + "-" + (zh_ ? "zh" : "en") + ".png"));
        require(!image.isNull(), "missing material " + name);
        return image.convertToFormat(QImage::Format_RGB32);
    }
    void setScreen(const QImage &image) {
        require(image.size() == QSize(kWidth * kScale, kHeight * kScale), "the desktop is not 2400 x 1500");
        screen_ = image;
    }
    Editor &editor() { return owner_->editor_; }

    // ------------------------------------------------------------------ frames

    int rank(QWidget *w) const {
        if (w == terminal_) return terminalRank_;
        if (qobject_cast<Editor *>(w)) return 10;
        if (qobject_cast<QDialog *>(w)) return 30;
        if (qobject_cast<PinWindow *>(w)) return 40;
        if (qobject_cast<Overlay *>(w)) return 50;
        const QString name = w->objectName();
        if (name == "scrollCaptureShade") return 60;
        if (name == "scrollCaptureRegion") return 61;
        if (name == "scrollRegionHandle") return 62;
        if (name == "scrollCaptureProgress") return 63;
        if (name == "scrollPreviewArea") return 64;
        if (w->inherits("QTipLabel")) return 90;
        if (w->windowType() == Qt::Popup || qobject_cast<QMenu *>(w)) return 80;
        return 20;
    }
    QList<QWidget *> windows() const {
        QList<QWidget *> list;
        for (auto *w : QApplication::topLevelWidgets())
            if (w->isVisible() && !w->size().isEmpty() && w->windowType() != Qt::Desktop)
                list.append(w);
        std::stable_sort(list.begin(), list.end(), [this](QWidget *a, QWidget *b) { return rank(a) < rank(b); });
        return list;
    }
    QWidget *windowAt(QPoint global) const {
        if (pressed_)
            return pressed_;
        const auto list = windows();
        for (auto it = list.crbegin(); it != list.crend(); ++it) {
            if ((*it)->geometry().contains(global) && *it != terminal_ &&
                !(*it)->windowFlags().testFlag(Qt::WindowTransparentForInput))
                return *it;
            if ((*it)->windowType() == Qt::Popup && (*it)->geometry().contains(global))
                return *it;
        }
        return nullptr;
    }

    // During scrolling capture the controller hides its own surfaces for the instant
    // a frame is read; on a real screen they are excluded from capture instead and
    // never blink. Waiting for them to come back keeps the frames truthful.
    void settleScrollSurfaces() {
        for (int i = 0; i < 40 && owner_->scrollSource_; ++i) {
            const bool hidden = (owner_->scrollShade_ && !owner_->scrollShade_->isVisible()) ||
                                (owner_->scrollRegion_ && !owner_->scrollRegion_->isVisible());
            if (!hidden)
                return;
            QTest::qWait(4);
        }
    }

    static void shadow(QPainter &p, const QRectF &r, double radius) {
        p.save();
        p.setPen(Qt::NoPen);
        for (int i = 16; i >= 1; --i) {
            p.setBrush(QColor(15, 23, 42, 4));
            p.drawRoundedRect(r.adjusted(-i, -i + 8, i, i + 8), radius + i, radius + i);
        }
        p.restore();
    }

    QString cursorKind(QString *special) const {
        QWidget *top = windowAt(cursor_.toPoint());
        if (!top)
            return "arrow";
        QWidget *w = top->childAt(top->mapFromGlobal(cursor_.toPoint()));
        if (!w)
            w = top;
        for (; w; w = w->parentWidget()) {
            if (!w->testAttribute(Qt::WA_SetCursor))
                continue;
            switch (w->cursor().shape()) {
            case Qt::PointingHandCursor:
            case Qt::OpenHandCursor:
            case Qt::ClosedHandCursor: return "link";
            case Qt::SizeAllCursor: return "move";
            case Qt::SizeFDiagCursor: return "nwse";
            case Qt::SizeBDiagCursor: return "nesw";
            case Qt::SizeHorCursor:
            case Qt::SplitHCursor: return "ew";
            case Qt::SizeVerCursor:
            case Qt::SplitVCursor: return "ns";
            case Qt::IBeamCursor: return "beam";
            case Qt::CrossCursor: *special = "cross"; return {};
            case Qt::BlankCursor: *special = "none"; return {};
            default: return "arrow";
            }
        }
        return "arrow";
    }

    QImage compose() {
        QImage image(kWidth * kScale, kHeight * kScale, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(kScale);
        QPainter p(&image);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        const QRectF stage(0, 0, kWidth, kHeight);
        p.drawImage(stage, screen_);
        if (fade_ > 0 && !fadeTo_.isNull()) {
            p.setOpacity(fade_);
            p.drawImage(stage, fadeTo_);
            p.setOpacity(1);
        }
        for (auto *w : windows()) {
            const QRect g = w->geometry();
            if (w == terminal_ || qobject_cast<Editor *>(w))
                shadow(p, g, 14);
            if (auto *dialog = qobject_cast<QDialog *>(w)) {
                const QRectF frame = QRectF(g).adjusted(0, -32, 0, 0);
                shadow(p, frame, 10);
                QPainterPath path;
                path.addRoundedRect(frame, 10, 10);
                p.fillPath(path, QColor(243, 245, 248));
                QFont font;
                font.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
                font.setPixelSize(13);
                p.setFont(font);
                p.setPen(QColor(51, 65, 85));
                p.drawText(frame.adjusted(16, 0, 0, -g.height()), Qt::AlignVCenter | Qt::AlignLeft,
                           dialog->windowTitle());
                p.drawText(QRectF(frame.right() - 44, frame.top(), 44, 32), Qt::AlignCenter, QStringLiteral("✕"));
            }
            p.drawPixmap(g.topLeft(), w->grab());
        }
        for (const auto &ripple : ripples_) {
            const double t = (clock_ - ripple.second) / 450.0;
            if (t < 0 || t > 1)
                continue;
            QColor fill = kAccent;
            fill.setAlphaF(.28 * (1 - t));
            QColor ring = kAccent;
            ring.setAlphaF(.75 * (1 - t));
            p.setPen(QPen(ring, 2));
            p.setBrush(fill);
            p.drawEllipse(ripple.first, 7 + 20 * t, 7 + 20 * t);
        }
        if (showCursor_) {
            QString special;
            const QString kind = cursorKind(&special);
            if (special == "cross") {
                p.setPen(QPen(QColor(255, 255, 255, 230), 3.2));
                p.drawLine(cursor_ - QPointF(10, 0), cursor_ + QPointF(10, 0));
                p.drawLine(cursor_ - QPointF(0, 10), cursor_ + QPointF(0, 10));
                p.setPen(QPen(QColor(17, 24, 39), 1.2));
                p.drawLine(cursor_ - QPointF(10, 0), cursor_ + QPointF(10, 0));
                p.drawLine(cursor_ - QPointF(0, 10), cursor_ + QPointF(0, 10));
            } else if (special.isEmpty()) {
                const QPointF hotspot = hotspots_.value(kind) / 2.0;
                p.drawImage(QRectF(cursor_ - hotspot, QSizeF(32, 32)), cursors_.value(kind));
            }
        }
        if (!caption_.isEmpty()) {
            const auto parts = caption_.split('|');
            QFont kicker, body;
            kicker.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
            body.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
            kicker.setPixelSize(15);
            kicker.setWeight(QFont::Bold);
            body.setPixelSize(15);
            body.setWeight(QFont::Medium);
            const QFontMetricsF km(kicker), bm(body);
            const double gap = 12, padX = 20, h = 40;
            const double w = km.horizontalAdvance(parts[0]) + gap + 1 + gap + bm.horizontalAdvance(parts.value(1)) + padX * 2;
            const QRectF pill((kWidth - w) / 2, kHeight - h - 16, w, h);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(15, 23, 42, 222));
            p.drawRoundedRect(pill, h / 2, h / 2);
            double x = pill.left() + padX;
            p.setFont(kicker);
            p.setPen(QColor(125, 176, 255));
            p.drawText(QRectF(x, pill.top(), km.horizontalAdvance(parts[0]) + 2, h), Qt::AlignVCenter, parts[0]);
            x += km.horizontalAdvance(parts[0]) + gap;
            p.fillRect(QRectF(x, pill.top() + 12, 1, h - 24), QColor(255, 255, 255, 60));
            x += 1 + gap;
            p.setFont(body);
            p.setPen(QColor(241, 245, 249));
            p.drawText(QRectF(x, pill.top(), pill.right() - x, h), Qt::AlignVCenter, parts.value(1));
        }
        if (titleOpacity_ > 0) {
            p.setOpacity(titleOpacity_);
            QLinearGradient back(0, 0, kWidth, kHeight);
            back.setColorAt(0, QColor(248, 250, 255));
            back.setColorAt(1, QColor(226, 235, 255));
            p.fillRect(stage, back);
            p.drawImage(QRectF(kWidth / 2.0 - 48, 214, 96, 96), icon_);
            QFont brand;
            brand.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
            brand.setPixelSize(56);
            brand.setWeight(QFont::DemiBold);
            p.setFont(brand);
            QLinearGradient ink(kWidth / 2.0 - 160, 0, kWidth / 2.0 + 160, 0);
            ink.setColorAt(0, QColor(49, 85, 217));
            ink.setColorAt(1, QColor(97, 173, 255));
            p.setPen(QPen(QBrush(ink), 1));
            p.drawText(QRectF(0, 330, kWidth, 80), Qt::AlignCenter, "EditHere");
            QFont line = brand;
            line.setPixelSize(24);
            line.setWeight(QFont::Medium);
            p.setFont(line);
            p.setPen(QColor(30, 41, 59));
            p.drawText(QRectF(0, 418, kWidth, 40), Qt::AlignCenter,
                       zh_ ? "让 AI 看懂，你想怎么改。" : "Show your AI exactly what to change.");
            p.setOpacity(1);
        }
        return image;
    }

    // Frames are rendered at twice the stage size and stored at the stage size, which
    // is what keeps text and edges smooth. A frame identical to the one before only
    // lengthens it.
    void save(const QImage &image, qint64 ms) {
        clock_ += ms;
        QImage stored = image.scaled(kWidth, kHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGB32);
        stored.setDevicePixelRatio(1);
        if (!timeline_.isEmpty() && stored == last_) {
            auto entry = timeline_.last().toObject();
            entry["ms"] = entry["ms"].toInteger() + ms;
            timeline_[timeline_.size() - 1] = entry;
            return;
        }
        const QString file = QString("%1.png").arg(frameNumber_++, 5, 10, QChar('0'));
        require(stored.save(frames_.filePath(file), "PNG", 30), "cannot save frame");
        timeline_.append(QJsonObject{{"file", file}, {"ms", ms}});
        last_ = std::move(stored);
    }
    void snap(qint64 ms) {
        if (verbose() && frameNumber_ % 25 == 0)
            std::fprintf(stderr, "[recorder] frame %d\n", frameNumber_);
        QCoreApplication::processEvents();
        settleScrollSurfaces();
        save(compose(), ms);
    }
    // Lets the application run in real time and samples it, so animations and video
    // playback keep their real speed in the recording.
    void film(qint64 ms, int step = 60) {
        QElapsedTimer timer;
        timer.start();
        qint64 last = 0;
        QImage frame;
        while (last < ms) {
            QTest::qWait(step);
            settleScrollSurfaces();
            QImage next = compose();
            const qint64 now = timer.elapsed();
            if (!frame.isNull())
                save(frame, now - last);
            frame = std::move(next);
            last = now;
        }
        save(frame, step);
    }
    void hold(qint64 ms) { snap(ms); }
    void say(const QString &id) { caption_ = caption(id, zh_); }

    // ------------------------------------------------------------------ input

    void deliverMove() {
        QCursor::setPos(cursor_.toPoint());
        if (QWidget *w = windowAt(cursor_.toPoint()); w && w->windowHandle())
            QTest::mouseMove(w->windowHandle(), w->mapFromGlobal(cursor_.toPoint()));
    }
    void moveTo(QPointF target, int ms = 600, int step = 40) {
        const QPointF from = cursor_;
        const int count = std::max(1, ms / step);
        for (int i = 1; i <= count; ++i) {
            cursor_ = from + (target - from) * ease(double(i) / count);
            deliverMove();
            snap(ms / count);
        }
    }
    void press(Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QWidget *w = windowAt(cursor_.toPoint());
        require(w && w->windowHandle(), "nothing to press on");
        ripples_.append({cursor_, clock_});
        QTest::mousePress(w->windowHandle(), Qt::LeftButton, modifiers, w->mapFromGlobal(cursor_.toPoint()));
        pressed_ = w;
    }
    void release(Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QPointer<QWidget> w = pressed_;
        pressed_ = nullptr;
        if (w && w->windowHandle())
            QTest::mouseRelease(w->windowHandle(), Qt::LeftButton, modifiers, w->mapFromGlobal(cursor_.toPoint()));
    }
    void click(QPointF target, int ms = 550) {
        moveTo(target, ms);
        snap(90);
        press();
        snap(70);
        release();
        snap(70);
        snap(150);
    }
    void click(QWidget *widget, int ms = 550) {
        require(widget && widget->isVisible(), "click target is not visible");
        click(QPointF(widget->mapToGlobal(widget->rect().center())), ms);
    }
    void drag(QPointF from, QPointF to, int ms = 900) {
        moveTo(from, 500);
        snap(120);
        press();
        snap(80);
        moveTo(to, ms, 35);
        snap(120);
        release();
        snap(120);
    }
    void wheel(int notches) {
        QWidget *top = windowAt(cursor_.toPoint());
        require(top, "nothing to scroll");
        QWidget *target = top->childAt(top->mapFromGlobal(cursor_.toPoint()));
        if (!target)
            target = top;
        const QPointF local = target->mapFromGlobal(cursor_);
        QWheelEvent event(local, cursor_, QPoint(), QPoint(0, 120 * notches), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &event);
    }
    void key(QWidget *target, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(target, key, modifiers);
    }
    // Types into the note that was just opened, one character a frame.
    void typeNote(const QString &text, int perCharacter = 55) {
        QPlainTextEdit *input = nullptr;
        for (int i = 0; i < 50 && !input; ++i) {
            if (editor().document().notes.isEmpty() && !editor().videoProject().frames.size()) {
                QTest::qWait(10);
                continue;
            }
            const auto &notes = editor().document().notes;
            if (!notes.isEmpty())
                input = editor().findChild<QPlainTextEdit *>("noteText_" + notes.last().id);
            if (!input)
                QTest::qWait(10);
        }
        require(input, "no note editor");
        input->setFocus();
        snap(200);
        for (int i = 0; i < text.size(); ++i) {
            input->insertPlainText(text.mid(i, 1));
            if (i % 2 == 1 || i == text.size() - 1)
                snap(perCharacter * 2);
        }
        snap(500);
        QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
        snap(300);
    }

    template <typename Predicate> void waitFor(Predicate ready, int ms, const char *what) {
        QElapsedTimer timer;
        timer.start();
        while (!ready() && timer.elapsed() < ms)
            QTest::qWait(10);
        require(ready(), QString("timed out waiting for ") + what);
    }

    // ------------------------------------------------------------------ helpers

    Overlay *startCapture() {
        trace("capture");
        owner_->capture();
        owner_->captureForeground_ = 0; // Never hand focus back to the real desktop.
        waitFor([this] { return !owner_->overlays_.isEmpty() && owner_->overlays_.first()->isVisible(); }, 2000,
                "the capture window");
        Overlay *overlay = owner_->overlays_.first();
        waitFor([overlay] { return overlay->findChildren<QFutureWatcherBase *>().isEmpty(); }, 8000,
                "block detection");
        deliverMove();
        return overlay;
    }
    QPushButton *captureButton(QWidget *owner, const QString &glyph) {
        for (auto *button : owner->findChildren<QPushButton *>())
            if (button->property("glyphName").toString() == glyph && button->isVisible())
                return button;
        throw std::runtime_error(("no capture button " + glyph).toStdString());
    }
    QPushButton *button(QWidget *owner, const char *name) {
        auto *b = owner->findChild<QPushButton *>(name);
        require(b, QString("no button ") + name);
        return b;
    }
    void placeEditor(const QRect &geometry = QRect(36, 22, 1128, 690)) {
        QTest::qWait(30);
        editor().setGeometry(geometry);
        QTest::qWait(40);
        editor().fit();
        QTest::qWait(60);
    }
    // Where a point of the original screen ends up on the editor's canvas. The
    // capture began at `origin` in logical screen units.
    QPointF onCanvas(QPointF screen, QPointF origin, bool layout = false) {
        QWidget *canvas = layout ? static_cast<QWidget *>(editor().layoutCanvas()) : editor().canvas();
        const double zoom = layout ? editor().layoutCanvas()->zoom() : editor().canvas()->zoom();
        const QPointF pixel = (screen - origin) * kScale;
        return canvas->mapToGlobal((pixel * zoom).toPoint());
    }
    // Takes the region between two screen points the way a user does: drag, then
    // pick an action from the bar.
    Overlay *select(QPointF from, QPointF to) {
        Overlay *overlay = startCapture();
        drag(from, to, 900);
        snap(500);
        return overlay;
    }
    void openInEditor(Overlay *overlay) {
        click(captureButton(overlay, "edit"));
        waitFor([this] { return editor().hasDocument() && editor().isVisible(); }, 3000, "the editor");
        placeEditor();
        waitFor([this] { return editor().findChildren<QFutureWatcherBase *>().isEmpty(); }, 8000,
                "the editor's block detection");
        cursor_ = editor().mapToGlobal(QPoint(560, 400));
        snap(600);
    }
    // A modal dialog runs its own event loop, so the script for it is started from a
    // timer that waits for the dialog to appear and closes it when the script ends.
    void showModal(QPushButton *trigger, std::function<void(QWidget *)> script) {
        auto *poll = new QTimer;
        poll->setInterval(50);
        QObject::connect(poll, &QTimer::timeout, [this, poll, script] {
            QWidget *dialog = QApplication::activeModalWidget();
            if (!dialog)
                return;
            poll->stop();
            poll->deleteLater();
            try {
                dialog->setGeometry(QRect(180, 82, 840, 590));
                QTest::qWait(150);
                script(dialog);
            } catch (const std::exception &error) {
                failure_ = QString::fromUtf8(error.what());
            }
            dialog->close();
        });
        poll->start();
        click(trigger);
        require(failure_.isEmpty(), failure_);
    }

    // ------------------------------------------------------------------ scenes

    void capture() {
        say("cap.hotkey");
        hold(1300);
        Overlay *overlay = startCapture();
        snap(700);
        say("cap.hover");
        moveTo({330, 172}, 700);
        film(700);
        wheel(1);
        film(900);
        wheel(-1);
        snap(300);
        moveTo({1120, 300}, 700);
        film(800);
        moveTo({1130, 312}, 300);
        say("cap.click");
        press();
        snap(70);
        release();
        snap(1400);
        say("cap.style");
        click(captureButton(overlay, "corner"));
        auto *panel = qobject_cast<StylePanel *>(QApplication::activePopupWidget());
        if (!panel)
            for (auto *w : QApplication::topLevelWidgets())
                if (qobject_cast<StylePanel *>(w) && w->isVisible())
                    panel = qobject_cast<StylePanel *>(w);
        require(panel, "no corner panel");
        auto *slider = panel->findChild<QSlider *>();
        require(slider, "no corner slider");
        QStyleOptionSlider option;
        option.initFrom(slider);
        option.minimum = slider->minimum();
        option.maximum = slider->maximum();
        option.sliderPosition = option.sliderValue = slider->value();
        const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
        const QPointF grip = slider->mapToGlobal(handle.center());
        drag(grip, grip + QPointF(slider->width() * .45, 0), 1100);
        snap(900);
        key(panel, Qt::Key_Escape);
        snap(300);
        click(captureButton(overlay, "shadow"), 450);
        snap(1300);
        if (auto *popup = QApplication::activePopupWidget())
            key(popup, Qt::Key_Escape);
        snap(300);
        say("cap.pin");
        click(captureButton(overlay, "pin"));
        waitFor([this] { return !owner_->pins_.isEmpty(); }, 2000, "the pin");
        snap(1400);
        say("cap.pinmove");
        PinWindow *pin = owner_->pins_.first();
        const QPointF centre = pin->geometry().center();
        drag(centre, centre + QPointF(-540, 190), 1300);
        moveTo(centre + QPointF(-420, 120), 600);
        hold(1800);
    }

    void ocr() {
        say("ocr.select");
        hold(800);
        Overlay *overlay = select({206, 118}, {694, 470});
        say("ocr.run");
        click(captureButton(overlay, "ocr"));
        waitFor([this] { return owner_->ocrDialog_ && owner_->ocrDialog_->isVisible(); }, 3000, "the OCR window");
        OcrDialog *dialog = owner_->ocrDialog_;
        dialog->setGeometry(QRect(170, 96, 860, 520));
        auto *lines = dialog->findChild<QListWidget *>();
        require(lines, "no OCR line list");
        snap(400);
        QElapsedTimer timer;
        timer.start();
        while (lines->count() == 0 && timer.elapsed() < 20000)
            film(300);
        require(lines->count() > 0, "OCR returned no lines; is the language pack installed?");
        snap(900);
        say("ocr.lines");
        for (int row : {1, 3}) {
            if (row >= lines->count())
                break;
            const QRect item = lines->visualItemRect(lines->item(row));
            click(QPointF(lines->viewport()->mapToGlobal(item.center())));
            hold(1300);
        }
        moveTo(QPointF(dialog->mapToGlobal(QPoint(dialog->width() - 300, dialog->height() - 24))), 700);
        hold(1500);
    }

    // The documentation page scrolled to `offset` logical pixels, as the screen shows it.
    QImage docsScreen(double offset) const {
        QImage screen = docs_.copy();
        QPainter p(&screen);
        const int top = 40 * kScale, height = 662 * kScale;
        const int y = std::clamp(int(std::round(offset * kScale)), 0, article_.height() - height);
        p.drawImage(QPoint(0, top), article_.copy(0, y, article_.width(), height));
        return screen;
    }
    void scrolling() {
        docs_ = material("desktop-docs");
        article_ = material("article");
        setScreen(docsScreen(0));
        scrollOffset_ = 0;
        owner_->scrollIo_.supported = [](QString *) { return true; };
        owner_->scrollIo_.prepare = [](QObject *context, std::function<void()> ready) {
            QTimer::singleShot(30, context, std::move(ready));
        };
        owner_->scrollIo_.begin = [](const ScreenFrame &, const QRect &, QString *) { return true; };
        owner_->scrollIo_.end = [](bool) {};
        owner_->scrollIo_.prepareFrame = [] {};
        owner_->scrollIo_.automaticSupported = [](QString *) { return true; };
        owner_->scrollIo_.targetAt = [](QPoint) { return ScrollCaptureTarget{42, 7}; };
        owner_->scrollIo_.focus = [](const ScrollCaptureTarget &) {};
        owner_->scrollIo_.step = [](const ScrollCaptureTarget &, QPoint, int, QString *) { return true; };
        owner_->scrollIo_.grab = [this](const QRect &region, ScrollRegionCallback done) {
            done(screen_.copy(region), {});
        };
        say("scroll.select");
        hold(800);
        Overlay *overlay = select({196, 98}, {1004, 648});
        click(captureButton(overlay, "scroll"));
        waitFor([this] { return owner_->scrollProgress_ && owner_->scrollProgress_->isVisible(); }, 3000,
                "the scrolling capture panel");
        film(900);
        say("scroll.run");
        moveTo({640, 420}, 500);
        // Scroll the page the way a wheel does: in notches, with the page easing
        // between them, while the controller samples the screen on its own clock.
        for (int notch = 0; notch < 14; ++notch) {
            const double from = scrollOffset_, to = std::min(1700.0, scrollOffset_ + 120);
            QElapsedTimer timer;
            timer.start();
            qint64 last = 0;
            while (last < 260) {
                QTest::qWait(20);
                const qint64 now = timer.elapsed();
                scrollOffset_ = from + (to - from) * ease(std::min(1.0, now / 220.0));
                setScreen(docsScreen(scrollOffset_));
                if (now - last >= 60) {
                    snap(now - last);
                    last = now;
                }
            }
            scrollOffset_ = to;
            setScreen(docsScreen(scrollOffset_));
        }
        film(1200);
        say("scroll.done");
        auto *finish = owner_->scrollProgress_->findChild<QPushButton *>("scrollFinish");
        click(finish);
        waitFor([this] { return editor().hasDocument() && editor().isVisible(); }, 3000, "the long picture");
        placeEditor();
        cursor_ = editor().mapToGlobal(QPoint(640, 420));
        hold(2600);
    }

    void annotate() {
        say("ann.open");
        hold(700);
        const QPointF origin(200, 42);
        Overlay *overlay = select(origin, {1188, 648});
        openInEditor(overlay);
        say("ann.smart");
        moveTo(onCanvas({876, 196}, origin), 700);
        film(500);
        moveTo(onCanvas({880, 200}, origin), 200);
        press();
        snap(70);
        release();
        snap(300);
        typeNote(words("ann.smart", zh_));
        say("ann.point");
        click(button(&editor(), "mode_point"));
        click(onCanvas({614, 352}, origin));
        typeNote(words("ann.point", zh_));
        say("ann.rect");
        click(button(&editor(), "mode_rect"));
        drag(onCanvas({830, 408}, origin), onCanvas({1170, 460}, origin), 900);
        typeNote(words("ann.rect", zh_));
        say("ann.global");
        click(button(&editor(), "addGlobalNote"));
        typeNote(words("ann.global", zh_));
        hold(800);
        say("ann.json");
        showModal(button(&editor(), "exportJson"), [this](QWidget *dialog) {
            snap(500);
            if (auto *text = dialog->findChild<QPlainTextEdit *>()) {
                moveTo(QPointF(text->mapToGlobal(text->rect().center())), 600);
                for (int i = 0; i < 6; ++i) {
                    wheel(-2);
                    snap(260);
                }
            }
            hold(1800);
        });
        hold(600);
    }

    void explode() {
        say("ann.open");
        hold(600);
        const QPointF origin(200, 42);
        Overlay *overlay = select(origin, {1188, 648});
        openInEditor(overlay);
        say("exp.button");
        auto *explode = button(&editor(), "explodeButton");
        moveTo(QPointF(explode->mapToGlobal(explode->rect().center())), 700);
        snap(150);
        press();
        snap(60);
        release();
        film(1800);
        LayoutCanvas *canvas = editor().layoutCanvas();
        require(canvas, "Explode did not open its canvas");
        say("exp.move");
        moveTo(onCanvas({1088, 82}, origin, true), 700);
        film(500);
        press();
        snap(80);
        release();
        snap(500);
        drag(onCanvas({1088, 82}, origin, true), onCanvas({520, 74}, origin, true), 1300);
        snap(400);
        typeNote(words("exp.move", zh_));
        say("exp.resize");
        moveTo(onCanvas({1120, 300}, origin, true), 700);
        film(400);
        press();
        snap(70);
        release();
        snap(600);
        const QRectF bounds = canvas->selectionBounds();
        require(!bounds.isEmpty(), "nothing selected for resizing");
        const QPointF corner = canvas->mapToGlobal((bounds.bottomRight() * canvas->zoom()).toPoint());
        drag(corner, corner + QPointF(26, 40), 1100);
        snap(400);
        typeNote(words("exp.resize", zh_));
        say("exp.fill");
        moveTo(onCanvas({1088, 130}, origin, true), 800);
        hold(2400);
    }

    // Where a point of the game scene appears in the clip at `seconds`: the clip
    // pans and zooms slowly across the 1400 x 788 picture into a 1280 x 720 frame.
    static QPointF inClip(QPointF scene, double seconds) {
        const double t = std::clamp(seconds / 8.0, 0.0, 1.0);
        const double zoom = 1.0 + .06 * t;
        const double cw = 1400 / zoom, ch = 788 / zoom;
        const QPointF crop((1400 - cw) * (.2 + .6 * t), (788 - ch) * .5);
        return (scene - crop) * (1280.0 / cw);
    }
    QPointF onVideo(QPointF scene, double seconds) {
        Canvas *canvas = editor().canvas();
        return canvas->mapToGlobal((inClip(scene, seconds) * canvas->zoom()).toPoint());
    }
    double pausedSeconds(VideoPlayback *playback) {
        waitFor([playback] { return !playback->playing() && !playback->positioning(); }, 5000, "the pause");
        return playback->positionMs() / 1000.0;
    }
    void video() {
        say("vid.open");
        editor().loadMedia(material_.filePath("gameplay.mp4"));
        auto *playback = editor().findChild<VideoPlayback *>();
        require(playback, "no video controls");
        placeEditor();
        waitFor([playback] { return playback->durationMs() > 0 && !playback->positioning(); }, 15000,
                "the video to open");
        QTest::qWait(300);
        placeEditor();
        cursor_ = editor().mapToGlobal(QPoint(600, 420));
        snap(900);
        auto *play = button(&editor(), "videoPlayPause");
        click(play);
        film(2300, 70);
        say("vid.pause");
        moveTo(QPointF(play->mapToGlobal(play->rect().center())), 200);
        press();
        release();
        double at = pausedSeconds(playback);
        snap(700);
        click(button(&editor(), "mode_point"));
        click(onVideo({527, 314}, at));
        waitFor([this] { return editor().hasDocument(); }, 4000, "the frame screenshot");
        typeNote(words("vid.first", zh_));
        say("vid.second");
        click(play);
        film(3400, 70);
        moveTo(QPointF(play->mapToGlobal(play->rect().center())), 200);
        press();
        release();
        at = pausedSeconds(playback);
        snap(600);
        click(button(&editor(), "mode_rect"));
        drag(onVideo({116, 150}, at), onVideo({426, 288}, at), 900);
        typeNote(words("vid.second", zh_));
        say("vid.review");
        auto *timeline = editor().findChild<QSlider *>("videoTimeline");
        require(timeline, "no timeline");
        const auto frames = editor().videoProject().frames;
        require(!frames.isEmpty(), "no annotated frames");
        QStyleOptionSlider option;
        option.initFrom(timeline);
        option.orientation = Qt::Horizontal;
        option.minimum = timeline->minimum();
        option.maximum = timeline->maximum();
        option.sliderPosition = option.sliderValue =
            qRound(double(frames.first().timestampUs / 1000) / playback->durationMs() * option.maximum);
        const QRect handle = timeline->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, timeline);
        const QRect groove = timeline->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, timeline);
        const QPointF tag = timeline->mapToGlobal(QPoint(handle.center().x(), groove.center().y() - 5));
        moveTo(tag, 800);
        film(600);
        press();
        snap(60);
        release();
        film(1400);
        say("vid.json");
        showModal(button(&editor(), "exportJson"), [this](QWidget *dialog) {
            snap(400);
            if (auto *text = dialog->findChild<QPlainTextEdit *>())
                moveTo(QPointF(text->mapToGlobal(QPoint(text->width() / 2, 120))), 600);
            hold(2600);
        });
        hold(500);
    }

    void hero() {
        // Title card, then the desktop with the agent's terminal.
        titleOpacity_ = 1;
        showCursor_ = false;
        hold(1600);
        for (int i = 1; i <= 8; ++i) {
            titleOpacity_ = 1 - i / 8.0;
            snap(45);
        }
        titleOpacity_ = 0;
        showCursor_ = true;
        auto *terminal = new Terminal;
        terminal_ = terminal;
        terminalRank_ = 5; // Under the editor while the user reviews.
        terminal->setGeometry(QRect(370, 190, 500, 330));
        terminal->show();
        cursor_ = {700, 600};
        say("ai.ask");
        snap(500);
        terminal->add("> ", QColor(125, 176, 255));
        const QString ask = words("ai.ask", zh_);
        for (int i = 0; i < ask.size(); ++i) {
            terminal->typeLast(ask.mid(i, 1));
            if (i % 2 == 1 || i == ask.size() - 1)
                snap(90);
        }
        snap(600);
        terminal->add("● " + words("ai.wait", zh_), QColor(148, 163, 184));
        snap(400);
        terminal->add("$ edithere-cli annotate dashboard.png --output feedback.json", QColor(203, 213, 225));
        snap(900);

        // A real annotate request over a private socket, exactly as the CLI sends it.
        AgentServer server(*owner_);
        const QString socketName = "EditHere-readme-" + uniqueId();
        require(server.listen(socketName), "cannot listen on " + socketName);
        const QString input = QDir(state_->path()).filePath("dashboard.png");
        require(screen_.copy(0, 40 * kScale, kWidth * kScale, 662 * kScale).save(input), "cannot write the input");
        const QString output = QDir(state_->path()).filePath("feedback.json");
        QLocalSocket client;
        client.connectToServer(socketName);
        require(client.waitForConnected(2000), "cannot connect to the agent server");
        client.write(encodeAgentMessage({{"protocol", 1}, {"command", "annotate"}, {"input", input},
                                         {"output", output}, {"timeout", 600}, {"embed", true}}));
        client.flush();
        waitFor([this] { return editor().isVisible() && editor().hasDocument(); }, 4000, "the agent session");
        placeEditor(QRect(150, 30, 1010, 640));
        cursor_ = editor().mapToGlobal(QPoint(500, 380));
        say("ai.open");
        hold(1600);

        say("ai.notes");
        const QPointF origin(0, 40);
        moveTo(onCanvas({406, 142}, origin), 700);
        film(400);
        moveTo(onCanvas({410, 144}, origin), 150);
        press();
        snap(60);
        release();
        snap(250);
        typeNote(words("ai.first", zh_), 45);
        click(button(&editor(), "mode_point"));
        click(onCanvas({1088, 82}, origin));
        typeNote(words("ai.second", zh_), 45);
        click(button(&editor(), "addGlobalNote"));
        typeNote(words("ai.global", zh_), 45);
        say("ai.finish");
        auto *finish = button(&editor(), "agentFinish");
        moveTo(QPointF(finish->mapToGlobal(finish->rect().center())), 800);
        hold(700);
        press();
        snap(60);
        release();
        QByteArray buffer;
        QJsonObject reply;
        QString error;
        auto state = AgentFrameState::Incomplete;
        for (int i = 0; i < 300 && state == AgentFrameState::Incomplete; ++i) {
            QTest::qWait(10);
            buffer += client.readAll();
            state = takeAgentMessage(buffer, reply, error);
        }
        require(state == AgentFrameState::Complete && reply["ok"].toBool(), "no completion reply: " + error);
        snap(500);

        // Back in the terminal: the CLI prints the reply, the agent reads the file.
        editor().hide();
        terminalRank_ = 35;
        terminal->setGeometry(QRect(300, 120, 600, 430));
        reply["output"] = "feedback.json";
        terminal->add(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)), QColor(52, 211, 153));
        say("ai.apply");
        snap(900);
        QFile feedback(output);
        require(feedback.open(QIODevice::ReadOnly), "the feedback file is missing");
        const auto objects = QJsonDocument::fromJson(feedback.readAll()).object()["objects"].toArray();
        QStringList notes;
        for (const auto &object : objects)
            for (const auto &note : object.toObject()["annotations"].toArray())
                notes << note.toString();
        terminal->add("● " + words("ai.read", zh_).arg(notes.size()), QColor(226, 232, 240));
        snap(300);
        for (const auto &note : notes) {
            terminal->add("   · " + note, QColor(203, 213, 225));
            snap(380);
        }
        terminal->add("● " + words("ai.edit", zh_), QColor(148, 163, 184));
        snap(800);
        fadeTo_ = material("desktop-after");
        for (int i = 1; i <= 10; ++i) {
            fade_ = i / 10.0;
            snap(60);
        }
        terminal->add("✓ " + words("ai.done", zh_), QColor(52, 211, 153));
        snap(1000);
        for (int i = 1; i <= 8; ++i) {
            terminal->setWindowOpacity(1);
            terminal->move(terminal->x(), terminal->y() + 45);
            snap(40);
        }
        terminal->hide();
        moveTo({1088, 140}, 800);
        hold(2600);
    }

    bool zh_;
    QString root_;
    QDir material_, output_, frames_;
    QString scene_, caption_, failure_;
    std::unique_ptr<QTemporaryDir> state_;
    std::unique_ptr<Controller> owner_;
    QImage screen_, fadeTo_, docs_, article_, icon_, last_;
    double fade_ = 0, titleOpacity_ = 0, scrollOffset_ = 0;
    bool showCursor_ = true;
    QPointF cursor_;
    QPointer<QWidget> pressed_;
    QHash<QString, QImage> cursors_;
    QHash<QString, QPointF> hotspots_;
    QVector<QPair<QPointF, qint64>> ripples_;
    QJsonArray timeline_;
    int frameNumber_ = 0;
    qint64 clock_ = 0;
    QWidget *terminal_ = nullptr;
    int terminalRank_ = 25;
};
} // namespace h2d

int main(int argc, char **argv) {
    h2d::trace("start");
    if (argc != 5) {
        std::fprintf(stderr, "usage: readme_recorder <zh|en> <scene|all> <project root> <output folder>\n");
        return 2;
    }
    const QString output = QDir(QString::fromLocal8Bit(argv[4])).absolutePath();
    QDir().mkpath(output);
    // One 1200 x 750 screen drawn at twice the size, so the GIFs stay crisp.
    const QString config = QDir(output).filePath("screen.json");
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return 2;
    file.write(R"({"synchronousWindowSystemEvents": false, "windowFrameMargins": false,
        "screens": [{"name": "stage", "x": 0, "y": 0, "width": 2400, "height": 1500,
                     "logicalDpi": 96, "logicalBaseDpi": 96}]})");
    file.close();
    // EDITHERE_RECORDER_OFFSCREEN renders without a display; by default the windows
    // appear on the real desktop, which needs it left alone while recording.
    if (qEnvironmentVariableIsSet("EDITHERE_RECORDER_OFFSCREEN")) {
        qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + config).toLocal8Bit());
        qputenv("QT_SCALE_FACTOR", "2");
    }
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    h2d::trace("application");
    app.setApplicationName("EditHere README Recorder");
    app.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
    for (const char *font : {"segoeui.ttf", "segoeuib.ttf", "seguisb.ttf", "msyh.ttc", "msyhbd.ttc", "consola.ttf"})
        QFontDatabase::addApplicationFont(fonts + font);
    const QString language = QString::fromLocal8Bit(argv[1]);
    h2d::installLanguage(language == "zh" ? h2d::LanguageMode::SimplifiedChinese : h2d::LanguageMode::English);
    h2d::applyTheme(h2d::ThemeMode::Light);
    try {
        h2d::ReadmeRecorder recorder(language, QString::fromLocal8Bit(argv[3]), output);
        recorder.run(QString::fromLocal8Bit(argv[2]));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "readme_recorder: %s\n", error.what());
        return 1;
    }
    return 0;
}
