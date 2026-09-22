#include "editor.h"
#include "imagearea.h"
#include "guide.h"
#include "inlinenoteedit.h"
#include "detector.h"
#include "explosion.h"
#include "platform.h"
#include "projectfiles.h"
#include "settings.h"
#include "ui.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QStyle>
#include <QStandardPaths>
#include <QSet>
#include <QTextDocument>
#include <QToolTip>
#include <QWheelEvent>
#include <QUrl>
#include <cmath>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <functional>
namespace h2d {
namespace {
QString toolbarGlyph(const QString &id) {
    if (id == "saveProject") return "save";
    if (id == "saveImage") return "image-save";
    if (id == "copyJson") return "json-copy";
    if (id == "exportJson") return "json";
    if (id == "capture") return "capture";
    if (id == "fit") return "fit";
    return "image-copy";
}
// 反馈 JSON 临时文件目录：可配置，默认位于应用缓存目录下的 feedback 子目录。
QString feedbackTempDir(const QString &configured = {}) {
    const QString dir = configured.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("feedback")
        : configured;
    QDir().mkpath(dir);
    return dir;
}
// 将反馈 JSON 写入临时文件，返回文件路径。每次复制生成唯一文件名，避免覆盖上一次的引用。
QString writeFeedbackTempFile(const QByteArray &bytes, const QString &configuredDir = {}) {
    const QString dir = feedbackTempDir(configuredDir);
    const QString name = "edithere-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")
                          + "-" + uniqueId().left(4) + ".json";
    const QString path = QDir(dir).filePath(name);
    saveBytes(path, bytes);
    return path;
}
// 清理 7 天前的旧临时文件，保留当前路径对应的文件。
void cleanupOldFeedbackTempFiles(const QString &keepPath, const QString &configuredDir = {}) {
    QDir d(feedbackTempDir(configuredDir));
    const auto cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const auto &name : d.entryList({"edithere-*.json"}, QDir::Files)) {
        const QString path = d.filePath(name);
        if (path == keepPath) continue;
        if (QFileInfo(path).lastModified() < cutoff)
            QFile::remove(path);
    }
}
class JsonPreview final : public QPlainTextEdit {
  public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<void()> copyJson;

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->matches(QKeySequence::Copy)) {
            if (copyJson)
                copyJson();
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override {
        QMenu menu(this);
        auto copy = menu.addAction("复制完整 JSON");
        copy->setEnabled(bool(copyJson) && !toPlainText().isEmpty());
        connect(copy, &QAction::triggered, this, [this] {
            if (copyJson)
                copyJson();
        });
        menu.addAction("全选", this, &QPlainTextEdit::selectAll);
        menu.exec(event->globalPos());
    }
};
} // namespace
Editor::Editor(QWidget *parent) : QWidget(parent) {
    setWindowTitle("EditHere · 改这里");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);
    setMinimumSize(740, 400);
    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto shell = new QWidget(this);
    shell->setObjectName("editorShell");
    outer->addWidget(shell);
    auto layout = new QVBoxLayout(shell);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    auto bar = new DragBar(shell);
    bar->setFixedHeight(42);
    auto header = new QHBoxLayout(bar);
    header->setContentsMargins(14, 0, 6, 0);
    auto brand = new QLabel("EditHere · 改这里", bar);
    brand->setObjectName("brand");
    header->addWidget(brand);
    meta_ = mutedLabel({}, bar);
    header->addSpacing(10);
    header->addWidget(meta_);
    header->addStretch();
    hideAnnotations_ = iconButton("eye", "隐藏画面批注", bar);
    hideAnnotations_->setObjectName("hideAnnotations");
    hideAnnotations_->setCheckable(true);
    header->addWidget(hideAnnotations_);
    connect(hideAnnotations_, &QPushButton::clicked, this, &Editor::toggleAnnotations);
    auto magnifier=iconButton("zoom-in", "关闭放大镜", bar);
    magnifier->setObjectName("toggleMagnifier");
    magnifier->setCheckable(true); magnifier->setChecked(true);
    header->addWidget(magnifier);
    connect(magnifier,&QPushButton::toggled,this,[this,magnifier](bool enabled) {
        canvas_->setMagnifierEnabled(enabled);
        magnifier->setToolTip(enabled ? "关闭放大镜" : "开启放大镜");
    });
    explosion_ = textButton("大爆炸", false, bar);
    explosion_->setProperty("glyphName", "explode");
    explosion_->setIcon(glyph("explode"));
    explosion_->setObjectName("explodeButton");
    explosion_->setEnabled(false);
    explosion_->setCheckable(true);
    header->addWidget(explosion_);
    connect(explosion_, &QPushButton::clicked, this, &Editor::explode);
    auto capture = iconButton("capture", "重新截图", bar);
    capture->setObjectName("captureImage");
    auto help = iconButton("help", "使用引导", bar);
    help->setObjectName("showGuide");
    connect(help, &QPushButton::clicked, this, &Editor::showGuide);
    auto open = iconButton("open", "导入图片或项目", bar);
    open->setObjectName("importDocument");
    auto settings = iconButton("settings", "设置", bar);
    settings->setObjectName("openSettings");
    connect(settings, &QPushButton::clicked, this, [this] {
        finishNoteEdit();
        emit settingsRequested();
    });
    auto minimize = iconButton("minus", "最小化", bar);
    minimize->setObjectName("minimizeWindow");
    fullscreen_ = iconButton("fullscreen", "全屏", bar);
    fullscreen_->setObjectName("fullscreenWindow");
    fullscreen_->setCheckable(true);
    auto close = iconButton("close", "关闭当前截图", bar);
    close->setObjectName("closeWindow");
    header->addWidget(open);
    header->addWidget(capture);
    header->addWidget(settings);
    header->addWidget(help);
    header->addWidget(minimize);
    header->addWidget(fullscreen_);
    header->addWidget(close);
    connect(capture, &QPushButton::clicked, this, &Editor::captureRequested);
    connect(open, &QPushButton::clicked, this, [this] { openFile(); });
    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(fullscreen_, &QPushButton::clicked, this, &Editor::toggleFullscreen);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(bar);
    agentBanner_ = new QWidget(shell);
    agentBanner_->setObjectName("agentSessionBanner");
    auto agentRow = new QHBoxLayout(agentBanner_);
    agentRow->setContentsMargins(14, 8, 14, 8);
    auto agentHint = mutedLabel("AI 正在等待你的修改意见", agentBanner_);
    agentRow->addWidget(agentHint);
    agentRow->addStretch();
    auto agentCancel = textButton("取消", false, agentBanner_);
    agentCancel->setObjectName("agentCancel");
    auto agentFinish = textButton("完成并返回 AI", true, agentBanner_);
    agentFinish->setObjectName("agentFinish");
    agentRow->addWidget(agentCancel);
    agentRow->addWidget(agentFinish);
    connect(agentCancel, &QPushButton::clicked, this, &Editor::agentCancelRequested);
    connect(agentFinish, &QPushButton::clicked, this, &Editor::agentFinishRequested);
    agentBanner_->hide();
    layout->addWidget(agentBanner_);
    auto content = new QHBoxLayout;
    content->setSpacing(0);
    content->setContentsMargins(0, 0, 0, 0);
    imageScroll_ = new ImageArea(shell);
    imageScroll_->setObjectName("imageWell");
    canvas_ = new Canvas;
    imageScroll_->setCanvas(canvas_);
    imageScroll_->viewport()->installEventFilter(this);
    imageScroll_->viewport()->setMouseTracking(true);
    content->addWidget(imageScroll_, 1);
    notesPanel_ = new QWidget(shell);
    notesPanel_->setObjectName("notesPanel");
    notesPanel_->setFixedWidth(360);
    auto side = new QVBoxLayout(notesPanel_);
    side->setContentsMargins(8, 8, 8, 6);
    side->setSpacing(6);
    noteCount_ = new QLabel("批注 0 条", notesPanel_);
    noteCount_->setObjectName("noteCount");
    noteCount_->setProperty("sectionTitle", true);
    auto noteHeader = new QHBoxLayout;
    noteHeader->setContentsMargins(5, 0, 2, 0);
    noteHeader->addWidget(noteCount_);
    noteHeader->addStretch();
    auto globalNote = iconButton("note-add", "添加全局批注", notesPanel_);
    globalNote->setObjectName("addGlobalNote");
    globalNote->setFixedSize(28, 28);
    noteHeader->addWidget(globalNote);
    connect(globalNote, &QPushButton::clicked, this, &Editor::addGlobalNote);
    side->addLayout(noteHeader);
    notesScroll_ = new QScrollArea(notesPanel_);
    notesScroll_->setObjectName("notesScroll");
    notesScroll_->setWidgetResizable(true);
    noteContainer_ = new QWidget;
    noteContainer_->setObjectName("noteContainer");
    noteLayout_ = new QVBoxLayout(noteContainer_);
    noteLayout_->setContentsMargins(0, 2, 0, 0);
    noteLayout_->setSpacing(6);
    noteLayout_->setAlignment(Qt::AlignTop);
    emptyNotes_ = mutedLabel("圈出位置，或添加一条全局意见。", noteContainer_);
    emptyNotes_->setObjectName("emptyNotes");
    emptyNotes_->setWordWrap(true);
    emptyNotes_->hide();
    noteLayout_->addWidget(emptyNotes_);
    notesScroll_->setWidget(noteContainer_);
    side->addWidget(notesScroll_, 1);
    inspectorSeparator_ = new QWidget(notesPanel_);
    inspectorSeparator_->setObjectName("inspectorSeparator");
    inspectorSeparator_->setFixedHeight(1);
    inspectorSeparator_->hide();
    side->addWidget(inspectorSeparator_);
    detailsStack_ = new QStackedWidget(shell);
    detailsStack_->setObjectName("detailsStack");
    detailsStack_->setFixedWidth(360);
    detailsStack_->addWidget(notesPanel_);
    content->addWidget(detailsStack_);
    wave_ = new ExplosionWave(imageScroll_->viewport());
    wave_->setGeometry(imageScroll_->viewport()->rect());
    layout->addLayout(content, 1);
    hint_ = mutedLabel("滚轮 ↑ 更大 ↓ 更小 · 单击批注 · 拖动框选", shell);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setFixedHeight(26);
    hint_->setObjectName("editorHint");
    hint_->hide();
    auto dock = new QHBoxLayout;
    dock_ = dock;
    dock->setContentsMargins(12, 3, 12, 10);
    dock->setSpacing(4);
    dock->addStretch();
    QStringList names{"smart", "point", "rect", "select"},
        labels{"智能选块 (B)", "点标注 (P)", "框选 (R)", "调整批注 (V)"};
    for (int i = 0; i < 4; i++) {
        auto button = iconButton(names[i], labels[i], shell);
        button->setCheckable(true);
        button->setObjectName("mode_" + names[i]);
        modes_.append(button);
        dock->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { setMode(static_cast<Canvas::Mode>(i)); });
    }
    dock->addSpacing(10);
    undo_ = iconButton("undo", "撤销", shell);
    redo_ = iconButton("redo", "重做", shell);
    dock->addWidget(undo_);
    dock->addWidget(redo_);
    connect(undo_, &QPushButton::clicked, this, &Editor::undo);
    connect(redo_, &QPushButton::clicked, this, &Editor::redo);
    dock->addSpacing(10);
    auto minus = iconButton("minus", "缩小", shell), plus = iconButton("plus", "放大", shell);
    zoom_ = textButton("100%", false, shell);
    zoom_->setFixedWidth(64);
    zoom_->setToolTip("适应图片");
    dock->addWidget(minus);
    dock->addWidget(zoom_);
    dock->addWidget(plus);
    connect(minus, &QPushButton::clicked, this, [this] { zoom(canvas_->zoom() / 1.2); });
    connect(plus, &QPushButton::clicked, this, [this] { zoom(canvas_->zoom() * 1.2); });
    connect(zoom_, &QPushButton::clicked, this, &Editor::fit);
    notesToggle_ = iconButton("notes", "收起批注框", shell);
    notesToggle_->setCheckable(true);
    notesToggle_->setObjectName("collapseNotes");
    dock->addWidget(notesToggle_);
    connect(notesToggle_, &QPushButton::clicked, this, [this] {
        detailsStack_->setVisible(!detailsStack_->isVisible());
        if (fitted_)
            QTimer::singleShot(0, this, &Editor::fit);
        updateControls();
    });
    auto separator = new QWidget(shell);
    separator->setObjectName("toolbarSeparator");
    separator->setFixedSize(1, 24);
    dock->addSpacing(5);
    dock->addWidget(separator);
    dock->addSpacing(5);
    for (const auto &definition : toolbarActionDefinitions()) {
        const QString icon = toolbarGlyph(definition.id);
        auto button = iconButton(icon, definition.label, shell);
        button->setObjectName(definition.id);
        outputButtons_.insert(definition.id, button);
        dock->addWidget(button);
        if (definition.id == "saveProject") connect(button, &QPushButton::clicked, this, [this] { saveProject(); });
        else if (definition.id == "saveImage") connect(button, &QPushButton::clicked, this, [this] { saveImage(true); });
        else if (definition.id == "copyJson") connect(button, &QPushButton::clicked, this, &Editor::copyJson);
        else if (definition.id == "exportJson") connect(button, &QPushButton::clicked, this, &Editor::exportJson);
        else if (definition.id == "capture") connect(button, &QPushButton::clicked, this, &Editor::captureRequested);
        else if (definition.id == "fit") connect(button, &QPushButton::clicked, this, &Editor::fit);
        else connect(button, &QPushButton::clicked, this, &Editor::copyImage);
    }
    auto more = iconButton("more", "更多操作", shell);
    more->setObjectName("moreActions");
    dock->addWidget(more);
    connect(more, &QPushButton::clicked, this, [this, more] { showContext(more->mapToGlobal(QPoint(0, 0))); });
    dock->addStretch();
    auto grip = new QSizeGrip(shell);
    dock->addWidget(grip);
    layout->addLayout(dock);
    connect(canvas_, &Canvas::editRequested, this, &Editor::editNote);
    connect(canvas_, &Canvas::hintChanged, hint_, &QLabel::setText);
    connect(canvas_, &Canvas::zoomRequested, this, [this](double value, QPointF anchor) {
        zoomAt(value, QPointF(canvas_->pos()) + anchor);
    });
    connect(canvas_, &Canvas::contextRequested, this, &Editor::showContext);
    connect(canvas_, &Canvas::panRequested, this, &Editor::panImage);
    connect(canvas_, &Canvas::regionRequested, this, &Editor::addManualRegion);
    connect(canvas_, &Canvas::movementAnnotationRequested, this, &Editor::editMovement);
    connect(canvas_, &Canvas::geometryChanged, this, [this](Note n) {
        remember();
        for (auto &stored : doc_.notes)
            if (stored.id == n.id) {
                stored = n;
                break;
            }
        changed();
    });
    connect(canvas_, &Canvas::selectionChanged, this, [this] { renderNotes(); });
    const auto defaults = defaultSettings();
    auto shortcut = [this, &defaults](const QString &id, auto handler) {
        auto s = new QShortcut(defaults.shortcuts.value(id), this);
        s->setObjectName("shortcutAction_" + id);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        shortcuts_.insert(id, s);
        connect(s, &QShortcut::activated, this, handler);
    };
    shortcut("undo", &Editor::undo);
    shortcut("redo", &Editor::redo);
    shortcut("save", [this] { saveProject(); });
    shortcut("open", [this] { openFile(); });
    shortcut("copy", &Editor::copyImage);
    shortcut("copyJson", &Editor::copyJson);
    shortcut("saveImage", [this] { saveImage(true); });
    shortcut("hideAnnotations", &Editor::toggleAnnotations);
    shortcut("addGlobalNote", &Editor::addGlobalNote);
    shortcut("paste", &Editor::pasteImage);
    shortcut("export", &Editor::exportJson);
    shortcut("fit", &Editor::fit);
    shortcut("delete", &Editor::removeSelected);
    shortcut("close", [this] {
        if (guideActive()) {
            dismissGuide();
            return;
        }
        if (isFullScreen()) {
            toggleFullscreen();
            return;
        }
        if (explosionActive_) {
            if (componentEditing_ && (!layoutCanvas_->selected().isEmpty() || layoutCanvas_->drawingMode()))
                layoutCanvas_->cancelInteraction();
            else
                setExplosionActive(false);
        } else
            this->close();
    });
    shortcut("smart", [this] { setMode(Canvas::Smart); });
    shortcut("point", [this] { setMode(Canvas::Point); });
    shortcut("rectangle", [this] { setMode(Canvas::Rectangle); });
    shortcut("adjust", [this] { setMode(Canvas::Adjust); });
    shortcut("explode", &Editor::explode);
    shortcut("component", [this] {
        if (explosionActive_)
            setComponentEditing(true);
    });
    setShortcuts(defaults.shortcuts);
    updateToolbar();
    resize(1260, 850);
}
void Editor::showGuide() {
    finishNoteEdit();
    if (!hasDocument()) setDocument(fromImage(exampleImage(), "demo", "引导示例"));
    if (!guide_) {
        guide_ = new GuideOverlay(this);
        connect(guide_, &GuideOverlay::dismissed, this, &Editor::guideDismissed);
    }
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    guide_->start();
}
void Editor::dismissGuide() {
    if (guide_) guide_->dismiss();
}
bool Editor::guideActive() const {
    return guide_ && guide_->isVisible();
}
void Editor::setShortcuts(const QMap<QString, QKeySequence> &bindings) {
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it) {
        const auto sequence = bindings.value(it.key());
        it.value()->setKey(sequence);
        it.value()->setEnabled(!sequence.isEmpty());
    }
    const QStringList ids{"smart", "point", "rectangle", "adjust"};
    const QStringList labels{"智能选块", "点标注", "框选", "调整批注"};
    auto label = [&](const QString &id, const QString &name) {
        const auto key = bindings.value(id).toString(QKeySequence::NativeText);
        return key.isEmpty() ? name : name + " (" + key + ")";
    };
    for (int i = 0; i < modes_.size(); ++i)
        modes_[i]->setToolTip(label(ids[i], labels[i]));
    explosion_->setToolTip(label("explode", "大爆炸"));

}
void Editor::setDocument(Document document, const QString &projectPath) {
    dismissGuide();
    finishNoteEdit();
    annotationsVisible_ = true;
    hideAnnotations_->setChecked(false);
    canvas_->setAnnotationsVisible(true);
    resetLayoutTools();
    doc_ = std::move(document);
    projectPath_ = projectPath;
    undoHistory_.clear();
    redoHistory_.clear();
    canvas_->setDocument(&doc_);
    setMode(static_cast<Canvas::Mode>(preferences_.defaultTool));
    fitted_ = preferences_.fitImageOnOpen;
    canvas_->setLayoutPreview(doc_.layout.has_value());
    explosion_->setEnabled(doc_.layout.has_value());
    updateLayoutControls();
    detailsStack_->show();
    detailsStack_->setCurrentWidget(notesPanel_);
    meta_->setText(QString("%1 × %2").arg(doc_.image.width()).arg(doc_.image.height()));
    renderNotes();
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    configureNativeWindow(this, false);
    QRect available = QGuiApplication::screenAt(QCursor::pos())
                          ? QGuiApplication::screenAt(QCursor::pos())->availableGeometry()
                          : QGuiApplication::primaryScreen()->availableGeometry();
    if (!isFullScreen() && !isMaximized()) {
        resize(std::min(1260, available.width() - 60), std::min(850, available.height() - 80));
        move(available.center() - rect().center());
    }
    const int generation = ++generation_;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != generation_)
            return;
        if (preferences_.fitImageOnOpen)
            fit();
        else {
            zoom(1.0);
            imageScroll_->centerImage();
        }
    });
    auto watcher = new QFutureWatcher<QVector<Candidate>>(this);
    connect(watcher, &QFutureWatcher<QVector<Candidate>>::finished, this, [this, watcher, generation] {
        if (generation == generation_) {
            auto candidates = watcher->result();
            doc_.candidates += candidates;
            auto whole = manualTarget();
            whole["label"] = "整个图片";
            doc_.candidates.append({QRect(QPoint(0, 0), doc_.image.size()), whole});
            canvas_->refresh();
            explosion_->setEnabled(true);
            updateLayoutControls();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([image = doc_.image] { return detectBlocks(image); }));
    raise();
    activateWindow();
    canvas_->setFocus();
    updateControls();
}
void Editor::toggleFullscreen() {
    stopViewportPan();
    if (!isFullScreen()) {
        beforeFullscreenState_ = windowState() & ~Qt::WindowMinimized;
        beforeFullscreenGeometry_ = isMaximized() ? normalGeometry() : geometry();
        beforeFullscreenOrigin_ = imageScroll_->imageOrigin();
        if (isMaximized()) {
            // Windows 上窗口处于最大化状态时直接 showFullScreen() 不生效，
            // 需要先退出最大化，再在下一次事件循环中进入全屏。
            showNormal();
            QTimer::singleShot(0, this, &QWidget::showFullScreen);
        } else
            showFullScreen();
    } else {
        if (beforeFullscreenState_.testFlag(Qt::WindowMaximized))
            showMaximized();
        else {
            showNormal();
            if (beforeFullscreenGeometry_.isValid())
                setGeometry(beforeFullscreenGeometry_);
        }
        const int generation = generation_;
        const QPointF position = beforeFullscreenOrigin_;
        QTimer::singleShot(0, this, [this, generation, position] {
            if (generation == generation_ && !isFullScreen()) {
                imageScroll_->setImageOrigin(position);
            }
        });
    }
    updateFullscreenButton();
}
void Editor::updateFullscreenButton() {
    if (!fullscreen_) return;
    const bool full = isFullScreen();
    const QString name = full ? "fullscreen-exit" : "fullscreen";
    const QString label = full ? "退出全屏" : "全屏";
    fullscreen_->setChecked(full);
    fullscreen_->setProperty("glyphName", name);
    fullscreen_->setIcon(glyph(name));
    fullscreen_->setToolTip(label);
    fullscreen_->setAccessibleName(label);
}
void Editor::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateFullscreenButton();
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        stopViewportPan();
}
void Editor::panImage(QPoint delta) {
    if (!hasDocument() || delta.isNull()) return;
    fitted_ = false;
    imageScroll_->setImageOrigin(imageScroll_->imageOrigin() + delta);
}
void Editor::stopViewportPan() {
    if (!viewportPanning_) return;
    viewportPanning_ = false;
    imageScroll_->viewport()->unsetCursor();
}
void Editor::fit() {
    if (!hasDocument())
        return;
    fitted_ = true;
    QSize area = imageScroll_->viewport()->size() - QSize(36, 36);
    canvas_->setZoom(std::min({1.0, double(std::max(80, area.width())) / doc_.image.width(),
                               double(std::max(80, area.height())) / doc_.image.height()}));
    if (layoutCanvas_)
        layoutCanvas_->setZoom(canvas_->zoom());
    imageScroll_->centerImage();
    zoom_->setText(QString::number(qRound(canvas_->zoom() * 100)) + "%");
}
void Editor::zoom(double value) {
    zoomAt(value, QPointF(imageScroll_->viewport()->width() / 2.0,
                          imageScroll_->viewport()->height() / 2.0));
}
void Editor::zoomAt(double value, QPointF viewportAnchor) {
    if (!hasDocument())
        return;
    if (value == 0) {
        fit();
        return;
    }
    if (!std::isfinite(value)) return;
    const QPointF imagePoint = (viewportAnchor - imageScroll_->imageOrigin()) / canvas_->zoom();
    fitted_ = false;
    canvas_->setZoom(value);
    if (layoutCanvas_)
        layoutCanvas_->setZoom(canvas_->zoom());
    imageScroll_->setImageOrigin(viewportAnchor - imagePoint * canvas_->zoom());
    zoom_->setText(QString::number(qRound(canvas_->zoom() * 100)) + "%");
}
void Editor::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (wave_)
        wave_->setGeometry(imageScroll_->viewport()->rect());
    updateToolbar();
    if (fitted_)
        QTimer::singleShot(0, this, [this] {
            if (fitted_) fit();
        });
}
void Editor::setPreferences(const AppSettings &settings) {
    preferences_ = settings;
    if (zoom_) updateToolbar();
}
void Editor::updateToolbar() {
    const bool compact = width() < 1100;
    for (const auto &definition : toolbarActionDefinitions()) {
        auto button = outputButtons_.value(definition.id);
        if (!button) continue;
        button->setVisible(preferences_.toolbarActions.contains(definition.id));
        const bool labelled = !compact && (definition.id == "copyJson" || definition.id == "copyImage");
        button->setProperty("tool", !labelled);
        button->setProperty("primary", definition.id == "copyJson");
        button->setText(labelled ? definition.label : QString());
        button->setFixedSize(labelled ? (definition.id == "copyJson" ? 118 : 160) : 34, 34);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->setIcon(glyph(button->property("glyphName").toString(), definition.id == "copyJson" ? QColor(Qt::white) : QColor()));
    }
    // Keep the permanent tools accessible when optional actions exceed the window width.
    if (dock_) {
        dock_->invalidate();
        const QStringList overflowOrder{"fit", "capture", "copyImage", "saveImage", "exportJson", "saveProject", "copyJson"};
        for (const auto &id : overflowOrder) {
            if (dock_->sizeHint().width() <= width()-2) break;
            if (auto button = outputButtons_.value(id); button && !button->isHidden()) {
                button->hide(); dock_->invalidate();
            }
        }
    }
}
void Editor::setMode(Canvas::Mode mode) {
    finishNoteEdit();
    canvas_->setMode(mode);
    if (explosionActive_ && (mode == Canvas::Smart || mode == Canvas::Rectangle)) {
        setComponentEditing(true);
        layoutCanvas_->setDrawing(mode == Canvas::Rectangle);
    } else {
        setComponentEditing(false);
        canvas_->setLayoutPreview(doc_.layout.has_value());
        canvas_->setFocus();
    }
    updateLayoutControls();
    updateControls();
}
void Editor::updateControls() {
    for (int i = 0; i < modes_.size(); i++)
        modes_[i]->setChecked(i == (componentEditing_ ? (layoutCanvas_->drawingMode() ? Canvas::Rectangle : Canvas::Smart) : canvas_->mode()));
    undo_->setEnabled(!undoHistory_.isEmpty());
    redo_->setEnabled(!redoHistory_.isEmpty());
    notesToggle_->setChecked(!detailsStack_->isHidden());
    notesToggle_->setToolTip(detailsStack_->isHidden() ? "展开批注框" : "收起批注框");
    for (auto it = outputButtons_.begin(); it != outputButtons_.end(); ++it)
        it.value()->setEnabled(it.key()=="capture" || hasDocument());
    hideAnnotations_->setEnabled(hasDocument());
}
QString Editor::coords(const Note &n) const {
    if (n.isGlobal) return QStringLiteral("全局");
    if (n.movementSource) return QStringLiteral("位置变化");
    return n.isPoint ? QString("点 (%1,%2)").arg(n.point.x()).arg(n.point.y())
                     : QString("框 (%1,%2)→(%3,%4)").arg(n.rect.x()).arg(n.rect.y())
                           .arg(n.rect.x() + n.rect.width()).arg(n.rect.y() + n.rect.height());
}
void Editor::renderNotes() {
    if (renderingNotes_) return;
    renderingNotes_ = true;
    QSet<QString> retained;
    for (const auto &note : doc_.notes) retained.insert(note.id);
    for (auto it = noteCards_.begin(); it != noteCards_.end();) {
        if (!retained.contains(it.key())) {
            if (it.value()) { noteLayout_->removeWidget(it.value()); it.value()->hide(); it.value()->deleteLater(); }
            noteEditors_.remove(it.key());
            it = noteCards_.erase(it);
        } else ++it;
    }
    emptyNotes_->setVisible(doc_.notes.isEmpty());
    noteCount_->setText(QString("批注 %1 条").arg(doc_.notes.size()));
    int number = 0;
    for (const auto &n : doc_.notes) {
        QWidget *card = noteCards_.value(n.id);
        if (!card) {
            auto frame = new QFrame(noteContainer_);
            card = frame;
            card->setObjectName("noteCard");
            card->setProperty("noteId", n.id);
            card->setFocusPolicy(Qt::ClickFocus);
            card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
            auto l = new QVBoxLayout(card);
            l->setContentsMargins(7, 5, 7, 5);
            l->setSpacing(3);
            auto row = new QHBoxLayout;
            row->setSpacing(4);
            auto badge = new QLabel(card);
            badge->setObjectName("noteBadge");
            badge->setAlignment(Qt::AlignCenter);
            badge->setFixedSize(22, 22);
            row->addWidget(badge);
            auto position = mutedLabel({}, card);
            position->setObjectName("noteCoordinates");
            position->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            row->addWidget(position, 1);
            auto edit = iconButton("edit", "编辑批注", card), remove = iconButton("trash", "删除批注", card);
            edit->setFixedSize(24, 24); remove->setFixedSize(24, 24);
            edit->setIconSize({16, 16}); remove->setIconSize({16, 16});
            row->addWidget(edit); row->addWidget(remove);
            l->addLayout(row);
            auto text = new InlineNoteEdit(card);
            text->setObjectName("noteText_" + n.id);
            text->setProperty("noteId", n.id);
            text->setAccessibleName("批注内容");
            text->setPlaceholderText("写下你的想法…");
            text->setTabChangesFocus(true);
            text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            l->addWidget(text);
            auto fold = new QPushButton(card);
            fold->setObjectName("foldNote");
            fold->setProperty("noteId", n.id);
            fold->setCursor(Qt::PointingHandCursor);
            fold->setFocusPolicy(Qt::NoFocus);
            fold->hide();
            l->addWidget(fold, 0, Qt::AlignRight);
            text->presentationChanged = [text, fold] {
                fold->setVisible(text->needsCollapse());
                fold->setText(text->isExpanded() ? "收起" : "展开");
                fold->setAccessibleName(text->isExpanded() ? "收起批注全文" : "展开批注全文");
            };
            connect(fold, &QPushButton::clicked, this, [this,text,id=n.id] {
                finishNoteEdit();
                if (text->hasFocus()) text->clearFocus();
                text->setExpanded(!text->isExpanded());
                if (text->isExpanded()) revealNote(id);
            });
            text->setExpanded(n.id == editingId_);
            noteCards_.insert(n.id, card); noteEditors_.insert(n.id, text);
            text->started = [this, id=n.id] { beginNoteEdit(id); };
            text->finished = [this, id=n.id] { QTimer::singleShot(0, this, [this,id] { if (editingId_ == id) finishNoteEdit(); }); };
            text->cancelled = [this] { cancelNoteEdit(); };
            connect(text, &QPlainTextEdit::textChanged, this, [this,text,id=n.id] {
                text->fitContent();
                if (renderingNotes_) return;
                beginNoteEdit(id);
                QString value = text->toPlainText();
                if (value.size() > 10000) { value.truncate(10000); QSignalBlocker blocker(text); text->setPlainText(value); }
                for (auto &note : doc_.notes) if (note.id == id) { note.comment=value; break; }
                doc_.dirty = true;
                canvas_->refresh();
                if (layoutCanvas_) layoutCanvas_->setAnnotations(doc_.notes);
                revealNote(id);
            });
            connect(text, &QPlainTextEdit::cursorPositionChanged, this, [this,text,id=n.id] {
                if (!renderingNotes_ && text->hasFocus() && editingId_ == id) revealNote(id);
            });
            connect(edit, &QPushButton::clicked, this, [this,id=n.id] { focusNote(id); });
            connect(remove, &QPushButton::clicked, this, [this,id=n.id] {
                finishNoteEdit(); remember();
                doc_.notes.removeIf([&](const Note &note) { return note.id==id; });
                canvas_->select({}); changed();
            });
        }
        card->setProperty("selected", canvas_->selected().contains(n.id));
        card->style()->unpolish(card); card->style()->polish(card);
        card->findChild<QLabel *>("noteBadge")->setText(QString::number(++number));
        auto position = card->findChild<QLabel *>("noteCoordinates");
        position->setText(coords(n)); position->setToolTip(coords(n));
        auto text = static_cast<InlineNoteEdit *>(noteEditors_.value(n.id).data());
        if (!text->hasFocus() && text->toPlainText()!=n.comment) { QSignalBlocker blocker(text); text->setPlainText(n.comment); }
        text->setAccessibleName(QString("批注 %1 内容").arg(number));
        text->fitContent();
        noteLayout_->insertWidget(number-1,card,0,Qt::AlignTop);
    }
    // Display movements without text annotations as orphan cards.
    if (doc_.layout) {
        const auto markers = movementMarkers(*doc_.layout, doc_.notes);
        for (const auto &marker : markers) {
            if (marker.noteIndex >= 0)
                continue; // already shown as a note card
            const QString key = "__mv_" + QString::number(qint64(marker.source.left())) + "_"
                                + QString::number(qint64(marker.source.top()));
            QWidget *card = noteCards_.value(key);
            if (!card) {
                auto frame = new QFrame(noteContainer_);
                card = frame;
                card->setObjectName("noteCard");
                card->setProperty("noteId", key);
                card->setProperty("orphanMovement", true);
                card->setFocusPolicy(Qt::ClickFocus);
                card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
                auto l = new QVBoxLayout(card);
                l->setContentsMargins(7, 5, 7, 5);
                l->setSpacing(3);
                auto row = new QHBoxLayout;
                row->setSpacing(4);
                auto badge = new QLabel(card);
                badge->setObjectName("noteBadge");
                badge->setAlignment(Qt::AlignCenter);
                badge->setFixedSize(22, 22);
                row->addWidget(badge);
                auto position = mutedLabel({}, card);
                position->setObjectName("noteCoordinates");
                position->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                row->addWidget(position, 1);
                l->addLayout(row);
                auto text = new InlineNoteEdit(card);
                text->setObjectName("noteText_" + key);
                text->setProperty("noteId", key);
                text->setAccessibleName("批注内容");
                text->setPlaceholderText("点击此处为这次移动添加文字…");
                text->setTabChangesFocus(true);
                text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                l->addWidget(text);
                text->started = [this, src = marker.source, dst = marker.destination] {
                    editMovement(src, dst);
                };
                text->finished = [this, key] { QTimer::singleShot(0, this, [this,key] { finishNoteEdit(); }); };
                text->cancelled = [this] { cancelNoteEdit(); };
                connect(text, &QPlainTextEdit::textChanged, this, [this,text,key] {
                    text->fitContent();
                    if (renderingNotes_) return;
                    QString value = text->toPlainText();
                    if (value.size() > 10000) { value.truncate(10000); QSignalBlocker blocker(text); text->setPlainText(value); }
                });
                noteCards_.insert(key, card);
                noteEditors_.insert(key, text);
            }
            card->setProperty("selected", false);
            card->style()->unpolish(card); card->style()->polish(card);
            card->findChild<QLabel *>("noteBadge")->setText(QString::number(marker.number));
            auto position = card->findChild<QLabel *>("noteCoordinates");
            const QString coords = QString("移动 (%1,%2) → (%3,%4)")
                                       .arg(int(marker.source.x())).arg(int(marker.source.y()))
                                       .arg(int(marker.destination.x())).arg(int(marker.destination.y()));
            position->setText(coords); position->setToolTip(coords);
            noteLayout_->insertWidget(marker.number - 1, card, 0, Qt::AlignTop);
        }
    }
    renderingNotes_ = false;
}
void Editor::beginNoteEdit(const QString &id) {
    if (finishingEdit_ || editingId_==id) return;
    finishNoteEdit();
    collapseOtherNotes(id);
    editingBaseline_={doc_.notes,doc_.layout}; editingWasDirty_=doc_.dirty; editingId_=id;
    canvas_->select({id});
}
void Editor::finishNoteEdit() {
    if (editingId_.isEmpty() || finishingEdit_) return;
    finishingEdit_=true;
    const QString id=editingId_;
    for (auto &note : doc_.notes) if (note.id==id) {
        note.comment=note.comment.trimmed();
        if (note.comment.isEmpty() && id!=draftId_)
            for (const auto &before : editingBaseline_.notes) if (before.id==id) note=before;
        break;
    }
    if (id==draftId_) doc_.notes.removeIf([&](const Note &note) { return note.id==id && note.comment.isEmpty(); });
    const bool modified=doc_.notes!=editingBaseline_.notes;
    if (modified) {
        for (auto &note : doc_.notes) if (note.id==id) note.updatedAt=timestamp();
        undoHistory_.append(editingBaseline_); if (undoHistory_.size()>60) undoHistory_.removeFirst(); redoHistory_.clear();
    }
    doc_.dirty=editingWasDirty_ || modified;
    editingId_.clear(); draftId_.clear();
    finishingEdit_=false;
    changed(false);
}
void Editor::cancelNoteEdit() {
    if (editingId_.isEmpty()) return;
    doc_.notes=editingBaseline_.notes; doc_.dirty=editingWasDirty_;
    editingId_.clear(); draftId_.clear();
    canvas_->select({}); changed(false);
}
void Editor::collapseOtherNotes(const QString &id) {
    for (auto it=noteEditors_.begin();it!=noteEditors_.end();++it) {
        if (it.key()==id || !it.value()) continue;
        auto text=static_cast<InlineNoteEdit *>(it.value().data());
        text->clearFocus();
        text->setExpanded(false);
    }
}
void Editor::revealNote(const QString &id) {
    QTimer::singleShot(0,this,[this,id] {
        auto text=noteEditors_.value(id);
        if (!text || text->isHidden() || !noteCards_.value(id) || (!editingId_.isEmpty() && editingId_!=id)) return;
        noteLayout_->activate();
        // The range update follows the new card/expanded editor geometry in Qt's layout queue.
        QTimer::singleShot(0,this,[this,id] {
            auto text=noteEditors_.value(id);
            if (!text || !noteCards_.value(id) || (!editingId_.isEmpty() && editingId_!=id)) return;
            const QPoint position=text->mapTo(noteContainer_,text->hasFocus() ? text->cursorRect().center() : QPoint(0,0));
            notesScroll_->ensureVisible(position.x(),position.y(),8,38);
        });
    });
}
void Editor::focusNote(const QString &id) {
    beginNoteEdit(id);
    collapseOtherNotes(id);
    detailsStack_->show();
    canvas_->select(id);
    renderNotes();
    if (auto text=noteEditors_.value(id)) {
        static_cast<InlineNoteEdit *>(text.data())->setExpanded(true);
        noteCards_.value(id)->show();
        text->setFocus(Qt::OtherFocusReason);
        revealNote(id);
    }
    // Newly inserted cards may not be focusable until their first layout pass.
    QTimer::singleShot(0,this,[this,id] {
        if (editingId_!=id) return;
        if (auto text=noteEditors_.value(id)) { text->setFocus(Qt::OtherFocusReason); revealNote(id); }
    });
    updateControls();
}
void Editor::editNote(Note note, bool fresh, QPoint) {
    finishNoteEdit();
    if (fresh) {
        if (doc_.notes.size()>=MaxNotes) { showError("最多支持 1000 条批注"); return; }
        editingBaseline_={doc_.notes,doc_.layout}; editingWasDirty_=doc_.dirty;
        editingId_=note.id; draftId_=note.id;
        doc_.notes.append(note);
    }
    focusNote(note.id);
}
void Editor::addGlobalNote() {
    if (!hasDocument()) return;
    Note note; note.isGlobal=true;
    editNote(note,true,{});
}
void Editor::editMovement(QRectF source, QRectF destination) {
    for (const auto &note : doc_.notes) if (note.movementSource && *note.movementSource==source) { focusNote(note.id); return; }
    Note note; note.isPoint=false; note.rect=destination.toAlignedRect(); note.movementSource=source;
    editNote(note,true,{});
}
void Editor::addManualRegion(QRect area) {
    finishNoteEdit();
    try {
        if (!doc_.layout) doc_.layout=createLayout(doc_.image.size(),doc_.candidates);
        if (!splitBaseline_) splitBaseline_=doc_.layout;
        remember();
        addLayoutRegion(*doc_.layout, area);
        if (layoutCanvas_) layoutCanvas_->setState(*doc_.layout);
        canvas_->setLayoutPreview(true);
        changed();
        Note note; note.isPoint=false; note.rect=area;
        editNote(note,true,{});
    } catch(const std::exception &error) { showError(QString::fromUtf8(error.what())); }
}
void Editor::toggleAnnotations() {
    annotationsVisible_=!annotationsVisible_;
    hideAnnotations_->setChecked(!annotationsVisible_);
    hideAnnotations_->setToolTip(annotationsVisible_ ? "隐藏画面批注" : "显示画面批注");
    hideAnnotations_->setProperty("glyphName",annotationsVisible_ ? "eye" : "eye-off");
    hideAnnotations_->setIcon(glyph(annotationsVisible_ ? "eye" : "eye-off"));
    canvas_->setAnnotationsVisible(annotationsVisible_);
    if(layoutCanvas_) layoutCanvas_->setAnnotationsVisible(annotationsVisible_);
}
bool Editor::eventFilter(QObject *object, QEvent *event) {
    if (object == imageScroll_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto mouse = static_cast<QMouseEvent *>(event);
            if (hasDocument() && mouse->button() == Qt::MiddleButton &&
                mouse->buttons() == Qt::MiddleButton) {
                viewportPanning_ = true;
                viewportPanPosition_ = mouse->globalPosition().toPoint();
                imageScroll_->viewport()->setCursor(Qt::ClosedHandCursor);
                event->accept();
                return true;
            }
            if (viewportPanning_) return true;
        } else if (event->type() == QEvent::MouseMove && viewportPanning_) {
            const auto mouse = static_cast<QMouseEvent *>(event);
            if (!(mouse->buttons() & Qt::MiddleButton)) {
                stopViewportPan();
                return false;
            }
            const auto position = mouse->globalPosition().toPoint();
            panImage(position - viewportPanPosition_);
            viewportPanPosition_ = position;
            event->accept();
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && viewportPanning_) {
            if (static_cast<QMouseEvent *>(event)->button() == Qt::MiddleButton)
                stopViewportPan();
            event->accept();
            return true;
        } else if (event->type() == QEvent::Hide || event->type() == QEvent::FocusOut ||
                   event->type() == QEvent::UngrabMouse) {
            stopViewportPan();
        }
        if (viewportPanning_ && event->type() == QEvent::Wheel) return true;
    }
    if (object==imageScroll_->viewport() && event->type()==QEvent::Wheel && hasDocument()) {
        const auto wheel=static_cast<QWheelEvent *>(event);
        QWidget *active=imageScroll_->widget();
        const auto point=active->mapFrom(imageScroll_->viewport(),wheel->position().toPoint());
        if (!active->rect().contains(point)) {
            const int delta=wheel->angleDelta().y()!=0 ? wheel->angleDelta().y() : wheel->pixelDelta().y();
            if(delta!=0) zoomAt(canvas_->zoom() * (delta>0 ? 1.12 : 1/1.12), wheel->position());
            wheel->accept(); return true;
        }
    }
    return QWidget::eventFilter(object,event);
}
void Editor::toast(const QString &message) {
    hint_->setText(message);
    QToolTip::showText(mapToGlobal(QPoint(width()/2,height()-65)),message,this,{},2000);
}
void Editor::copyJson() {
    finishNoteEdit();
    if (!hasDocument()) return;
    try {
        const QByteArray bytes = serializeFeedback(doc_, preferences_.embedOriginal, true);
        const QString path = writeFeedbackTempFile(bytes, preferences_.feedbackDir);
        cleanupOldFeedbackTempFiles(path, preferences_.feedbackDir);
        auto mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        QApplication::clipboard()->setMimeData(mime);
        toast(preferences_.embedOriginal ? "JSON 文件已复制，包含完整原图" : "JSON 文件已复制，未包含原图");
    } catch(const std::exception &error) { showError(QString::fromUtf8(error.what())); }
}
void Editor::changed(bool contentChanged) {
    if (contentChanged)
        doc_.dirty = true;
    canvas_->refresh();
    if (layoutCanvas_)
        layoutCanvas_->setAnnotations(doc_.notes);
    renderNotes();
    updateControls();
}
void Editor::remember() {
    finishNoteEdit();
    undoHistory_.append({doc_.notes, doc_.layout});
    if (undoHistory_.size() > 60)
        undoHistory_.removeFirst();
    redoHistory_.clear();
}
void Editor::restore(Snapshot snapshot) {
    const bool layoutChanged = doc_.layout != snapshot.layout;
    const bool contentChanged = doc_.dirty || doc_.notes != snapshot.notes ||
                                (doc_.layout ? exportLayoutChanges(*doc_.layout) : QJsonArray()) !=
                                    (snapshot.layout ? exportLayoutChanges(*snapshot.layout) : QJsonArray());
    doc_.notes = std::move(snapshot.notes);
    doc_.layout = snapshot.layout ? std::move(snapshot.layout) : splitBaseline_;
    if (layoutCanvas_ && doc_.layout)
        layoutCanvas_->setState(*doc_.layout);
    if (layoutChanged)
        canvas_->setLayoutPreview(doc_.layout.has_value());
    canvas_->select({});
    changed(contentChanged);
    updateLayoutControls();
}
void Editor::undo() {
    finishNoteEdit();
    if (undoHistory_.isEmpty())
        return;
    redoHistory_.append({doc_.notes, doc_.layout});
    restore(undoHistory_.takeLast());
}
void Editor::redo() {
    finishNoteEdit();
    if (redoHistory_.isEmpty())
        return;
    undoHistory_.append({doc_.notes, doc_.layout});
    restore(redoHistory_.takeLast());
}
void Editor::updateLayoutControls() {
    explosion_->setChecked(explosionActive_);
    explosion_->setIcon(glyph("explode",explosionActive_ ? QColor(Qt::white) : QColor()));
    if (!componentEditing_)
        hint_->setText(doc_.layout ? "在调整后的画面批注 · 滚轮切换范围 · 单击或拖动框选"
                                   : "滚轮切换范围 · 单击批注 · 拖动框选");
    updateControls();
}
void Editor::setComponentEditing(bool enabled) {
    enabled = enabled && explosionActive_ && layoutCanvas_;
    componentEditing_ = enabled;
    if (enabled) {
        layoutCanvas_->setAnnotations(doc_.notes);
        layoutCanvas_->setZoom(canvas_->zoom());
        switchCanvas(layoutCanvas_);
        inspectorSeparator_->show();
        inspectorScroll_->show();
        detailsStack_->setCurrentWidget(notesPanel_);
        detailsStack_->show();
        layoutCanvas_->setFocus();
        hint_->setText("悬停滚轮选范围 · 拖边改宽高 · 拖角等比 · 点标注或框选可添加意见");
    } else {
        if (layoutCanvas_)
            layoutCanvas_->cancelInteraction();
        canvas_->setLayoutPreview(doc_.layout.has_value());
        switchCanvas(canvas_);
        detailsStack_->setCurrentWidget(notesPanel_);
        if (inspectorScroll_) inspectorScroll_->hide();
        inspectorSeparator_->hide();
        canvas_->setFocus();
    }
    updateLayoutControls();
}
void Editor::switchCanvas(QWidget *target) {
    stopViewportPan();
    if (imageScroll_->widget() == target)
        return;
    const QPointF origin = imageScroll_->imageOrigin();
    if (auto previous = imageScroll_->takeWidget()) {
        previous->hide();
        previous->setParent(this);
    }
    imageScroll_->setCanvas(target);
    target->show();
    imageScroll_->setImageOrigin(origin);
}
void Editor::resetLayoutTools() {
    explosionActive_ = false;
    componentEditing_ = false;

    splitBaseline_.reset();
    inspectorSeparator_->hide();
    if (layoutCanvas_) {
        disconnect(layoutCanvas_, nullptr, this, nullptr);
        switchCanvas(canvas_);
        delete inspectorScroll_;
        layoutInspector_ = nullptr;
        inspectorScroll_ = nullptr;
        delete layoutCanvas_;
        layoutCanvas_ = nullptr;
    }
    detailsStack_->setCurrentWidget(notesPanel_);
    explosion_->setChecked(false);
    wave_->hide();
}
void Editor::setExplosionActive(bool enabled) {
    if (!hasDocument() || enabled == explosionActive_)
        return;
    if (enabled) {
        if (!doc_.layout)
            doc_.layout = createLayout(doc_.image.size(), doc_.candidates);
        if (!splitBaseline_)
            splitBaseline_ = doc_.layout;
        if (!layoutCanvas_) {
            layoutCanvas_ = new LayoutCanvas(doc_.image, *doc_.layout, this);
            inspectorScroll_ = new QScrollArea(notesPanel_);
            inspectorScroll_->setObjectName("componentInspectorScroll");
            inspectorScroll_->setMaximumHeight(220);
            inspectorScroll_->setWidgetResizable(true);
            layoutInspector_ = new LayoutInspector(layoutCanvas_);
            inspectorScroll_->setWidget(layoutInspector_);
            auto resizeInspector = [this] {
                if(inspectorScroll_ && layoutInspector_) inspectorScroll_->setFixedHeight(std::clamp(layoutInspector_->sizeHint().height(),50,210));
            };
            connect(layoutCanvas_,&LayoutCanvas::selectionChanged,this,resizeInspector);
            resizeInspector();
            static_cast<QVBoxLayout *>(notesPanel_->layout())->addWidget(inspectorScroll_);
            connect(layoutCanvas_, &LayoutCanvas::changed, this, [this] {
                if (!layoutCanvas_ || doc_.layout == layoutCanvas_->state())
                    return;
                const bool contentChanged = doc_.dirty || exportLayoutChanges(*doc_.layout) !=
                                                              exportLayoutChanges(layoutCanvas_->state());
                remember();
                const auto notes = remapNotes(doc_.notes, *doc_.layout, layoutCanvas_->state());
                const bool notesChanged = doc_.notes != notes;
                doc_.notes = notes;
                doc_.layout = layoutCanvas_->state();
                changed(contentChanged || notesChanged);
            });
            connect(layoutCanvas_, &LayoutCanvas::hintChanged, this, [this](const QString &hint) {
                if (componentEditing_)
                    hint_->setText(hint);
            });
            connect(layoutCanvas_, &LayoutCanvas::zoomRequested, this, [this](double value, QPointF anchor) {
                zoomAt(value, QPointF(layoutCanvas_->pos()) + anchor);
            });
            connect(layoutCanvas_, &LayoutCanvas::panRequested, this, &Editor::panImage);
            connect(layoutCanvas_, &LayoutCanvas::movementAnnotationRequested,this,&Editor::editMovement);
            layoutCanvas_->setAnnotationsVisible(annotationsVisible_);
            connect(layoutCanvas_, &LayoutCanvas::noteEditRequested, this,
                    [this](const QString &id, QPoint position) {
                        for (const auto &note : doc_.notes)
                            if (note.id == id) {
                                editNote(note, false, position);
                                break;
                            }
                    });
            connect(layoutCanvas_, &LayoutCanvas::annotationRequested, this,
                    [this](QRect area, QPoint position) {
                        Note note;
                        note.isPoint = false;
                        note.rect = area;
                        editNote(note, true, position);
                    });
        }
        explosionActive_ = true;
        canvas_->setLayoutPreview(true);
        setComponentEditing(true);
        wave_->setGeometry(imageScroll_->viewport()->rect());
        wave_->start();
    } else {
        explosionActive_ = false;
        setComponentEditing(false);
        wave_->hide();
    }
    updateLayoutControls();
}
void Editor::explode() {
    finishNoteEdit();
    if (!hasDocument())
        return;
    try {
        setExplosionActive(!explosionActive_);
    } catch (const std::exception &error) {
        explosion_->setChecked(explosionActive_);
        showError(QString::fromUtf8(error.what()));
    }
}
void Editor::removeSelected() {
    finishNoteEdit();
    if (componentEditing_)
        return;
    const auto id = canvas_->selected();
    if (id.isEmpty())
        return;
    remember();
    doc_.notes.removeIf([&](const Note &n) { return n.id == id; });
    canvas_->select({});
    changed();
}
void Editor::copyImage() {
    finishNoteEdit();
    if (hasDocument()) {
        QApplication::clipboard()->setImage(previewImage(doc_));
        toast("带批注图片已复制");
    }
}
void Editor::showError(const QString &text) {
    qWarning() << "EditHere showError:" << text; // 临时诊断：捕获保存失败的真实原因
    QMessageBox::warning(this, "EditHere", text);
}
bool Editor::saveProject() {
    finishNoteEdit();
    if (!hasDocument())
        return true;
    QString path = QFileDialog::getSaveFileName(this, "保存 EditHere 项目",
                                                projectPath_.isEmpty() ? "设计反馈.edithere" : projectPath_,
                                                "EditHere 项目 (*.edithere)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".edithere", Qt::CaseInsensitive))
        path += ".edithere";
    try {
        const auto bytes = serializeDocument(doc_, true);
        saveBytes(path, bytes);
        projectPath_ = path;
        doc_.dirty = false;
        QString associationError;
        if (!QStandardPaths::isTestModeEnabled())
            registerProjectFileAssociation(&associationError);
        toast(associationError.isEmpty() ? "项目已保存，可双击继续编辑" : "项目已保存，可从 EditHere 导入继续编辑");
        return true;
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}
void Editor::setAgentSession(bool active) {
    agentSession_ = active;
    agentBanner_->setVisible(active);
}
bool Editor::hasUnsavedChanges() {
    finishNoteEdit();
    // Agent requests also protect images that exist only in this editor.
    return doc_.dirty || (hasDocument() && projectPath_.isEmpty() &&
                          (doc_.source == "screen" || doc_.source == "clipboard"));
}
QByteArray Editor::agentFeedback(bool embed) {
    finishNoteEdit();
    return serializeFeedback(doc_, embed);
}
bool Editor::allowReplace() {
    if (agentSession_) {
        toast("请先完成或取消当前 AI 批注任务");
        return false;
    }
    finishNoteEdit();
    if (!doc_.dirty)
        return true;
    auto answer = QMessageBox::question(
        this, "保留当前修改？", "当前批注或布局修改尚未保存。是否先保存项目？",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveProject();
}
void Editor::openFile(const QString &provided) {
    QString path = provided;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, "打开图片或项目", {},
                                            "图片或项目 (*.png *.jpg *.jpeg *.webp *.bmp *.json *.edithere)");
    if (path.isEmpty())
        return;
    try {
        Document next = loadDocument(path);
        if (!allowReplace())
            return;
        setDocument(std::move(next));
        if (path.endsWith(".edithere", Qt::CaseInsensitive))
            projectPath_ = path;
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::pasteImage() {
    QImage image = QApplication::clipboard()->image();
    if (image.isNull())
        return;
    try {
        auto doc = fromImage(image, "clipboard", "剪贴板图片");
        if (allowReplace())
            setDocument(std::move(doc));
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::saveImage(bool annotated) {
    finishNoteEdit();
    if (!hasDocument())
        return;
    const bool layoutPreview = !annotated && doc_.layout.has_value();
    QString path = QFileDialog::getSaveFileName(this,
                                                annotated       ? "保存带批注图片"
                                                : layoutPreview ? "保存调整效果"
                                                                : "保存原图",
                                                annotated       ? "preview.png"
                                                : layoutPreview ? "layout.png"
                                                                : doc_.imageFile,
                                                "PNG 图片 (*.png)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    try {
        saveBytes(path, annotated       ? encodePng(previewImage(doc_))
                        : layoutPreview ? encodePng(renderLayout(doc_.image, *doc_.layout))
                                        : doc_.png);
        hint_->setText("图片已保存");
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::exportJson() {
    finishNoteEdit();
    if (!hasDocument())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("查看 JSON");
    dialog.resize(730, 640);
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    auto title = new QLabel(QString("查看 JSON  ·  批注 %1 条").arg(doc_.notes.size()), &dialog);
    QFont font = title->font();
    font.setPointSize(15);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    auto embed = new QCheckBox("包含原图，可独立还原", &dialog);
    embed->setObjectName("embedOriginal");
    embed->setChecked(preferences_.embedOriginal);
    layout->addWidget(embed);
    auto compress = new QCheckBox("压缩示意图（保持尺寸，可能轻微损失细节）", &dialog);
    compress->setObjectName("compressFeedbackImage"); compress->setChecked(true);
    layout->addWidget(compress);
    auto json = new JsonPreview(&dialog);
    json->setAccessibleName("标准化 JSON");
    json->setReadOnly(true);
    json->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    json->setFont(QFont("Consolas", 10));
    layout->addWidget(json, 1);
    auto status = mutedLabel({}, &dialog);
    status->setObjectName("exportStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto row = new QHBoxLayout;
    auto save = textButton("保存 JSON 与图片", false, &dialog), close = textButton("关闭", false, &dialog),
         copy = textButton("复制 JSON", true, &dialog);
    save->setProperty("glyphName","save");save->setIcon(glyph("save"));
    close->setProperty("glyphName","close");close->setIcon(glyph("close"));
    copy->setProperty("glyphName","json-copy");copy->setIcon(glyph("json-copy",Qt::white));
    save->setObjectName("saveFeedbackBundle");
    row->addWidget(save);
    row->addStretch();
    row->addWidget(close);
    row->addWidget(copy);
    layout->addLayout(row);
    QByteArray exportBytes;
    auto refresh = [&] {
        try {
            exportBytes = serializeFeedback(doc_, embed->isChecked(),compress->isChecked());
            const auto feedback = QJsonDocument::fromJson(exportBytes).object();
            QStringList lines{"{"};
            if (embed->isChecked())
                lines.append(
                    "  \"image\": \"" + feedback["image"].toString().section(',',0,0) + ",[图片编码已折叠]\",");
            lines.append("  \"annotationSpace\": \"result\",");
            lines.append("  \"objects\": [");
            const auto objects = feedback["objects"].toArray();
            for (qsizetype i = 0; i < objects.size(); ++i)
                lines.append("    " +
                             QString::fromUtf8(
                                 QJsonDocument(objects[i].toObject()).toJson(QJsonDocument::Compact)) +
                             (i + 1 < objects.size() ? "," : ""));
            lines.append("  ]");
            lines.append("}");
            json->setPlainText(lines.join('\n'));
            copy->setEnabled(true);
            save->setEnabled(true);
            status->setText(QString("%1 · %2 字符 · 批注坐标对应调整后的画面")
                                .arg(embed->isChecked() ? "图片编码仅在预览中折叠，复制/保存包含图片"
                                                        : "未包含原图，重新打开需同名 PNG")
                                .arg(QString::fromUtf8(exportBytes).size()));
        } catch (const std::exception &error) {
            exportBytes.clear();
            json->clear();
            copy->setEnabled(false);
            save->setEnabled(false);
            status->setText(QString::fromUtf8(error.what()));
        }
    };
    refresh();
    connect(embed, &QCheckBox::toggled, &dialog, refresh);
    connect(compress, &QCheckBox::toggled, &dialog, refresh);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto copyJson = [&] {
        if (exportBytes.isEmpty())
            return;
        try {
            const QString path = writeFeedbackTempFile(exportBytes, preferences_.feedbackDir);
            cleanupOldFeedbackTempFiles(path, preferences_.feedbackDir);
            auto mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(path)});
            QApplication::clipboard()->setMimeData(mime);
            status->setText(embed->isChecked() ? "JSON 文件已复制，包含完整原图" : "JSON 文件已复制，未包含原图");
        } catch (const std::exception &error) {
            status->setText(QString::fromUtf8(error.what()));
        }
    };
    json->copyJson = copyJson;
    connect(copy, &QPushButton::clicked, &dialog, copyJson);
    connect(save, &QPushButton::clicked, &dialog, [&] {
        if (exportBytes.isEmpty())
            return;
        QString base = QFileDialog::getExistingDirectory(&dialog, "选择导出目录");
        if (base.isEmpty())
            return;
        try {
            validateProjectStorageSize(exportBytes.size(), embed->isChecked() ? 0 : doc_.png.size());
            QString folder =
                QDir(base).filePath("EditHere-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                                    "-" + uniqueId().left(4));
            if (!QDir().mkpath(folder))
                throw std::runtime_error("无法创建导出目录");
            QDir dir(folder);
            saveBytes(dir.filePath("feedback.png"), doc_.png);
            saveBytes(dir.filePath("feedback.json"), exportBytes);
            // Optional visual previews must not prevent saving the original and feedback.
            QStringList unavailable;
            try {
                saveBytes(dir.filePath("annotations.png"), encodePng(previewImage(doc_)));
            } catch (const std::exception &e) {
                unavailable.append("批注预览未保存：" + QString::fromUtf8(e.what()));
            }
            if (doc_.layout && !exportFeedback(doc_)["objects"].toArray().isEmpty()) {
                try {
                    saveBytes(dir.filePath("result.png"), encodePng(renderLayout(doc_.image, *doc_.layout)));
                } catch (const std::exception &e) {
                    unavailable.append("调整效果图未保存：" + QString::fromUtf8(e.what()));
                }
            }
            status->setText(unavailable.isEmpty()
                                ? "已保存到：" + folder
                                : "JSON 与原图已保存到：" + folder + "\n" + unavailable.join('\n'));
        } catch (const std::exception &e) {
            status->setText(QString::fromUtf8(e.what()));
        }
    });
    dialog.exec();
}
void Editor::showContext(QPoint p) {
    finishNoteEdit();
    QMenu menu(this);
    auto settings = menu.addAction(glyph("settings"), "设置…", this, &Editor::settingsRequested);
    settings->setObjectName("editorSettings");
    menu.addAction(glyph("settings"),"自定义工具栏…",this,&Editor::toolbarSettingsRequested);
    bool hiddenActions=false;
    for (const auto &definition : toolbarActionDefinitions()) {
        auto button=outputButtons_.value(definition.id);
        if (!button || !button->isHidden()) continue;
        if (!hiddenActions) { menu.addSeparator(); hiddenActions=true; }
        auto action=menu.addAction(glyph(toolbarGlyph(definition.id)),definition.label,button,&QPushButton::click);
        action->setObjectName("more_"+definition.id);
        action->setEnabled(button->isEnabled());
    }
    menu.addSeparator();
    menu.addAction(glyph("open"),"导入图片或项目",this,[this] { openFile(); });
    menu.addAction(glyph("close"),"关闭当前截图",this,&QWidget::close);
    menu.exec(p);
}
void Editor::closeEvent(QCloseEvent *e) {
    e->ignore();
    if (agentSession_) {
        finishNoteEdit();
        emit agentCancelRequested();
        hide();
        emit hiddenToTray();
        return;
    }
    if (guideActive()) { dismissGuide(); return; }
    if (!allowReplace())
        return;
    ++generation_;
    stopViewportPan();
    resetLayoutTools();
    doc_ = {};
    undoHistory_.clear();
    redoHistory_.clear();
    projectPath_.clear();
    canvas_->setDocument(nullptr);
    hide();
    emit hiddenToTray();
}
void Editor::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void Editor::dropEvent(QDropEvent *e) {
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        openFile(urls.first().toLocalFile());
        e->acceptProposedAction();
    }
}
} // namespace h2d
