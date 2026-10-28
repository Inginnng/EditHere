#include "editor.h"
#include "agentprotocol.h"
#include "aifeedbacksnapshot.h"
#include "aiinstructionsdialog.h"
#include <QActionGroup>
#include "ocr.h"
#include "diagnostics.h"
#include "videoplayback.h"
#include "imagearea.h"
#include "guide.h"
#include "inlinenoteedit.h"
#include "detector.h"
#include "explosion.h"
#include "platform.h"
#include "projectfiles.h"
#include "settings.h"
#include "ui.h"
#include "fonts.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDebug>
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
#include <QTransform>
#include <QWheelEvent>
#include <QUrl>
#include <cmath>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
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
int annotationCount(const Document &doc) {
    int count = doc.notes.size();
    if (doc.layout)
        for (const auto &marker : movementMarkers(*doc.layout, doc.notes))
            if (marker.noteIndex < 0) ++count;
    return count;
}
QString toolbarGlyph(const QString &id) {
    if (id == "saveProject") return "save";
    if (id == "saveImage") return "image-save";
    if (id == "copyJson") return "json-copy-file";
    if (id == "copyJsonText") return "json-copy-text";
    if (id == "exportJson") return "json";
    if (id == "capture") return "capture";
    if (id == "fit") return "fit";
    if (id == "copyImage") return "image-copy";
    qWarning() << "Unknown toolbar action:" << id;
    return {};
}
// 反馈 JSON 临时文件目录：可配置，默认位于应用缓存目录下的 feedback 子目录。
QString feedbackTempDir(const QString &configured = {}) {
    const QString dir = configured.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("feedback")
        : configured;
    QDir().mkpath(dir);
    return dir;
}
// Each clipboard file bundle gets an owned directory, so relative references
// remain valid when several exports coexist. All URLs are copied together.
QString writeFeedbackTempBundle(const FeedbackBundle &bundle, const QString &configuredDir = {}) {
    const QDir parent(feedbackTempDir(configuredDir));
    const QString name = "edithere-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" + uniqueId();
    if (!parent.mkdir(name)) throw std::runtime_error("Unable to create feedback clipboard folder.");
    const QDir directory(parent.filePath(name));
    const QString marker = directory.filePath(".edithere-feedback-bundle");
    try {
        saveBytes(marker, "1\n");
        const QString path = directory.filePath("feedback.json");
        writeNewFeedbackBundle(path, bundle);
        return path;
    } catch (...) {
        QFile::remove(marker); parent.rmdir(name); throw;
    }
}
QList<QUrl> feedbackBundleUrls(const QString &jsonPath, const FeedbackBundle &bundle) {
    QList<QUrl> urls{QUrl::fromLocalFile(jsonPath)};
    const QDir directory = QFileInfo(jsonPath).dir();
    for (auto it = bundle.files.cbegin(); it != bundle.files.cend(); ++it) urls.append(QUrl::fromLocalFile(directory.filePath(it.key())));
    return urls;
}
void cleanupOldFeedbackTempFiles(const QString &keepPath, const QString &configuredDir = {}) {
    QDir parent(feedbackTempDir(configuredDir));
    const auto cutoff = QDateTime::currentDateTime().addDays(-7);
    for (const auto &name : parent.entryList({"edithere-*.json"}, QDir::Files)) {
        const QFileInfo file(parent.filePath(name));
        if (file.absoluteFilePath() != keepPath && !file.isSymLink() && file.lastModified() < cutoff) QFile::remove(file.absoluteFilePath());
    }
    for (const auto &name : parent.entryList({"edithere-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QFileInfo folder(parent.filePath(name));
        if (folder.isSymLink() || folder.lastModified() >= cutoff || QFileInfo(keepPath).absolutePath() == folder.absoluteFilePath()) continue;
        const QDir directory(folder.absoluteFilePath());
        const QFileInfo marker(directory.filePath(".edithere-feedback-bundle"));
        if (!marker.isFile() || marker.isSymLink()) continue;
        const auto entries = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        bool owned = true;
        for (const auto &entry : entries) if (!entry.isFile() || entry.isSymLink() ||
            !(entry.fileName() == ".edithere-feedback-bundle" || entry.fileName() == "feedback.json" || safeFeedbackImageName(entry.fileName()))) { owned = false; break; }
        if (!owned) continue;
        for (const auto &entry : entries) QFile::remove(entry.absoluteFilePath());
        parent.rmdir(name);
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
        auto copy = menu.addAction(QCoreApplication::translate("h2d", "复制完整 JSON"));
        copy->setEnabled(bool(copyJson) && !toPlainText().isEmpty());
        connect(copy, &QAction::triggered, this, [this] {
            if (copyJson)
                copyJson();
        });
        menu.addAction(QCoreApplication::translate("h2d", "全选"), this, &QPlainTextEdit::selectAll);
        menu.exec(event->globalPos());
    }
};
} // namespace
Editor::Editor(QWidget *parent) : QWidget(parent) {
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
    auto brand = new QLabel(bar);
    brand->setObjectName("brand");
    header->addWidget(brand);
    meta_ = mutedLabel({}, bar);
    meta_->setObjectName("captureContextLabel"); meta_->setTextFormat(Qt::PlainText);
    meta_->setMaximumWidth(360); meta_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    header->addSpacing(10);
    header->addWidget(meta_);
    header->addStretch();
    auto jsonHelp = textButton(tr("无法使用？"), false, bar);
    jsonHelp->setObjectName("jsonHelp");
    header->addWidget(jsonHelp);
    connect(jsonHelp, &QPushButton::clicked, this, &Editor::showJsonHelp);
    hideAnnotations_ = iconButton("eye", {}, bar);
    hideAnnotations_->setObjectName("hideAnnotations");
    hideAnnotations_->setCheckable(true);
    header->addWidget(hideAnnotations_);
    connect(hideAnnotations_, &QPushButton::clicked, this, &Editor::toggleAnnotations);
    auto magnifier=iconButton("magnifier", {}, bar);
    magnifier->setObjectName("toggleMagnifier");
    magnifier->setCheckable(true); magnifier->setChecked(true);
    header->addWidget(magnifier);
    connect(magnifier,&QPushButton::toggled,this,[this,magnifier](bool enabled) {
        canvas_->setMagnifierEnabled(enabled);
        magnifier->setToolTip(enabled ? tr("关闭放大镜") : tr("开启放大镜"));
        magnifier->setAccessibleName(magnifier->toolTip());
    });
    explosion_ = textButton({}, false, bar);
    explosion_->setProperty("glyphName", "explode");
    explosion_->setIcon(glyph("explode"));
    explosion_->setObjectName("explodeButton");
    explosion_->setEnabled(false);
    explosion_->setCheckable(true);
    header->addWidget(explosion_);
    connect(explosion_, &QPushButton::clicked, this, &Editor::explode);
    auto capture = iconButton("capture", {}, bar);
    capture->setObjectName("captureImage");
    auto help = iconButton("help", {}, bar);
    help->setObjectName("showGuide");
    connect(help, &QPushButton::clicked, this, &Editor::showGuide);
    auto open = iconButton("open", {}, bar);
    open->setObjectName("importDocument");
    auto settings = iconButton("settings", {}, bar);
    settings->setObjectName("openSettings");
    connect(settings, &QPushButton::clicked, this, [this] {
        finishNoteEdit();
        emit settingsRequested();
    });
    auto minimize = iconButton("window-minimize", {}, bar);
    minimize->setObjectName("minimizeWindow");
    fullscreen_ = iconButton("fullscreen", {}, bar);
    fullscreen_->setObjectName("fullscreenWindow");
    fullscreen_->setCheckable(true);
    auto close = iconButton("close", {}, bar);
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
    auto agentHint = mutedLabel({}, agentBanner_);
    agentHint->setObjectName("agentSessionHint");
    agentRow->addWidget(agentHint);
    agentRow->addStretch();
    auto agentCancel = textButton({}, false, agentBanner_);
    agentCancel->setObjectName("agentCancel");
    auto agentFinish = textButton({}, true, agentBanner_);
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
    videoPlayback_ = new VideoPlayback(shell);
    videoPlayback_->videoWidget()->setParent(imageScroll_->viewport());
    videoPlayback_->videoWidget()->hide();
    canvas_->installEventFilter(this);
    content->addWidget(imageScroll_, 1);
    notesPanel_ = new QWidget(shell);
    notesPanel_->setObjectName("notesPanel");
    notesPanel_->setFixedWidth(360);
    auto side = new QVBoxLayout(notesPanel_);
    side->setContentsMargins(8, 8, 8, 6);
    side->setSpacing(6);
    noteCount_ = new QLabel(notesPanel_);
    noteCount_->setObjectName("noteCount");
    noteCount_->setProperty("sectionTitle", true);
    auto noteHeader = new QHBoxLayout;
    noteHeader->setContentsMargins(5, 0, 2, 0);
    noteHeader->addWidget(noteCount_);
    noteHeader->addStretch();
    auto globalNote = iconButton("note-add", {}, notesPanel_);
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
    emptyNotes_ = mutedLabel({}, noteContainer_);
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
    // What the middle of the window says when there is no picture in it yet. It sits
    // on the viewport rather than in a layout so that it covers exactly the place a
    // picture is going to land, which is the place to aim a drop at.
    emptyWell_ = new QWidget(imageScroll_->viewport());
    emptyWell_->setObjectName("emptyWell");
    auto well = new QVBoxLayout(emptyWell_);
    well->setContentsMargins(24, 24, 24, 24);
    well->setSpacing(16);
    well->addStretch();
    emptyHint_ = mutedLabel({}, emptyWell_);
    emptyHint_->setObjectName("emptyHint");
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setWordWrap(true);
    emptyHint_->setStyleSheet("font-size:13px;");
    // The label takes the whole width and centres its own text: a wrapped QLabel
    // asked for a sizeHint slightly narrower than the line really is, and on a
    // scaled display that clipped the tail into a second line.
    well->addWidget(emptyHint_);
    // A drop is not the only way in: the button opens the same file chooser the tray
    // entry does, so the window is usable without anything to drag into it.
    emptyImport_ = textButton({}, true, emptyWell_);
    emptyImport_->setObjectName("emptyImport");
    emptyImport_->setMinimumWidth(184);
    emptyImport_->setFixedHeight(34);
    connect(emptyImport_, &QPushButton::clicked, this, [this] { openFile(); });
    well->addWidget(emptyImport_, 0, Qt::AlignCenter);
    well->addStretch();
    emptyWell_->installEventFilter(this);
    emptyWell_->setGeometry(imageScroll_->viewport()->rect());
    emptyWell_->hide();
    layout->addLayout(content, 1);
    layout->addWidget(videoPlayback_);
    videoPlayback_->hide();
    connect(videoPlayback_, &VideoPlayback::framePaused, this, &Editor::showVideoPreview);
    connect(videoPlayback_, &VideoPlayback::frameCaptured, this, &Editor::displayVideoFrame);
    connect(videoPlayback_, &VideoPlayback::frameCaptureFailed, this, &Editor::toast);
    connect(videoPlayback_, &VideoPlayback::playbackStarted, this, [this] {
        if (!video_) return;
        finishNoteEdit(); commitVideoFrame(); resetLayoutTools();
        showVideoPreview(videoPlayback_->frameSize(), videoPlayback_->positionMs() * 1000);
        detailsStack_->setEnabled(false); updateControls();
    });
    connect(videoPlayback_, &VideoPlayback::positioningStarted, this, [this] {
        if (!video_) return;
        finishNoteEdit(); commitVideoFrame();
        showVideoPreview(videoPlayback_->frameSize(), videoPlayback_->positionMs() * 1000);
        updateControls();
    });
    connect(videoPlayback_, &VideoPlayback::metadataChanged, this, [this](qint64 duration) {
        if (video_) { video_->durationMs = duration; updateVideoFrames(); }
    });
    connect(videoPlayback_, &VideoPlayback::reviewRequested, this, &Editor::reviewVideoFrame);
    connect(videoPlayback_, &VideoPlayback::failed, this, [this](const QString &error) {
        diagnostics::write(diagnostics::Level::Error, "video.playback", error);
        videoPlayback_->videoWidget()->hide();
        toast(error); detailsStack_->setEnabled(true); updateControls();
    });
    connect(videoPlayback_, &VideoPlayback::sourceRelocationRequested, this, [this] {
        if (!video_) return;
        const QString path = QFileDialog::getOpenFileName(this, tr("重新指定视频"), {},
            tr("视频 (*.mp4 *.mov *.mkv *.webm *.avi *.m4v);;所有文件 (*)"));
        if (path.isEmpty()) return;
        finishNoteEdit(); commitVideoFrame();
        video_->source = QFileInfo(path).absoluteFilePath(); video_->dirty = true;
        videoPlayback_->open(video_->source, videoFrameUs_ >= 0 ? videoFrameUs_ / 1000 : video_->positionMs);
    });
    qApp->installEventFilter(this);
    hint_ = mutedLabel({}, shell);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setFixedHeight(26);
    hint_->setObjectName("editorHint");
    hint_->hide();
    auto dock = new QHBoxLayout;
    dock_ = dock;
    dock->setContentsMargins(12, 3, 12, 10);
    dock->setSpacing(4);
    dock->addStretch();
    QStringList names{"smart", "point", "rect", "select"};
    for (int i = 0; i < 4; i++) {
        auto button = iconButton(names[i], {}, shell);
        button->setCheckable(true);
        button->setObjectName("mode_" + names[i]);
        modes_.append(button);
        dock->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { setMode(static_cast<Canvas::Mode>(i)); });
    }
    dock->addSpacing(10);
    undo_ = iconButton("undo", {}, shell);
    redo_ = iconButton("redo", {}, shell);
    dock->addWidget(undo_);
    dock->addWidget(redo_);
    connect(undo_, &QPushButton::clicked, this, &Editor::undo);
    connect(redo_, &QPushButton::clicked, this, &Editor::redo);
    dock->addSpacing(10);
    auto minus = iconButton("zoom-out", {}, shell), plus = iconButton("zoom-in", {}, shell);
    minus->setObjectName("zoomOut");
    plus->setObjectName("zoomIn");
    zoom_ = textButton("100%", false, shell);
    zoom_->setFixedWidth(64);
    dock->addWidget(minus);
    dock->addWidget(zoom_);
    dock->addWidget(plus);
    connect(minus, &QPushButton::clicked, this, [this] { zoom(canvas_->zoom() / 1.2); });
    connect(plus, &QPushButton::clicked, this, [this] { zoom(canvas_->zoom() * 1.2); });
    connect(zoom_, &QPushButton::clicked, this, &Editor::fit);
    notesToggle_ = iconButton("notes", {}, shell);
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
        if (icon.isEmpty()) continue;
        auto button = iconButton(icon, {}, shell);
        button->setObjectName(definition.id);
        outputButtons_.insert(definition.id, button);
        dock->addWidget(button);
        if (definition.id == "saveProject") connect(button, &QPushButton::clicked, this, [this] { saveProject(); });
        else if (definition.id == "saveImage") connect(button, &QPushButton::clicked, this, [this] { saveImage(true); });
        else if (definition.id == "copyJsonText") {
            connect(button, &QPushButton::clicked, this, &Editor::copyJsonText);
            copyJsonTextOptions_ = makeCopyJsonTextOptions(shell, "copyJsonTextOptions");
            dock->addWidget(copyJsonTextOptions_);
        }
        else if (definition.id == "copyJson") connect(button, &QPushButton::clicked, this, &Editor::copyJson);
        else if (definition.id == "exportJson") connect(button, &QPushButton::clicked, this, &Editor::exportJson);
        else if (definition.id == "capture") connect(button, &QPushButton::clicked, this, &Editor::captureRequested);
        else if (definition.id == "fit") connect(button, &QPushButton::clicked, this, &Editor::fit);
        else if (definition.id == "copyImage") connect(button, &QPushButton::clicked, this, &Editor::copyImage);
    }
    auto more = iconButton("more", {}, shell);
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
                if (!n.movementSource && (stored.isPoint != n.isPoint || stored.point != n.point || stored.rect != n.rect)) {
                    n.recognizedText = {};
                    recognizedRegions_.remove(n.id);
                }
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
    resize(1260, 850);
    // Every label of the permanent chrome is set here, so a live language change
    // only has to call this again.
    retranslate();
}
void Editor::retranslate() {
    const auto brandLabel = tr("EditHere · 改这里");
    setWindowTitle(brandLabel);
    if (auto brand = findChild<QLabel *>("brand"))
        brand->setText(brandLabel);
    // Header and dock buttons carry an icon, so their tooltip is the only label
    // they have; it doubles as the accessible name.
    auto tooltip = [this](const char *name, const QString &text) {
        if (auto button = findChild<QPushButton *>(name)) {
            button->setToolTip(text);
            button->setAccessibleName(text);
        }
    };
    tooltip("captureImage", tr("重新截图"));
    tooltip("showGuide", tr("使用引导"));
    tooltip("importDocument", tr("导入图片、视频或项目"));
    tooltip("openSettings", tr("设置"));
    tooltip("minimizeWindow", tr("最小化"));
    tooltip("fullscreenWindow", tr("全屏"));
    tooltip("closeWindow", tr("关闭当前截图"));
    tooltip("addGlobalNote", tr("添加全局批注"));
    tooltip("zoomOut", tr("缩小"));
    tooltip("zoomIn", tr("放大"));
    tooltip("moreActions", tr("更多操作"));
    updateAnnotationVisibility();
    if (auto magnifier = findChild<QPushButton *>("toggleMagnifier"))
        tooltip("toggleMagnifier", magnifier->isChecked() ? tr("关闭放大镜") : tr("开启放大镜"));
    if (auto agentHint = findChild<QLabel *>("agentSessionHint"))
        agentHint->setText(tr("AI 正在等待你的修改意见"));
    if (auto cancel = findChild<QPushButton *>("agentCancel"))
        cancel->setText(tr("取消"));
    if (auto finish = findChild<QPushButton *>("agentFinish"))
        finish->setText(tr("完成并返回 AI"));
    emptyNotes_->setText(tr("圈出位置，或添加一条全局意见。"));
    if (emptyHint_)
        emptyHint_->setText(tr("可拖入图片或视频，也可粘贴图片"));
    if (emptyImport_)
        emptyImport_->setText(tr("导入图片、视频或项目"));
    if (videoPlayback_) videoPlayback_->retranslate();
    if (video_) updateVideoFrames();
    zoom_->setToolTip(tr("适应图片"));
    auto primary = [](QPushButton *button, const QString &text) {
        button->setToolTip(text);
        button->setAccessibleName(text);
    };
    primary(undo_, tr("撤销"));
    primary(redo_, tr("重做"));
    if (explosion_)
        explosion_->setText(tr("大爆炸"));
    updateToolLabels();
    updateFullscreenButton();
    updateToolbar();
    if (canvas_)
        canvas_->retranslate();
    if (layoutCanvas_)
        layoutCanvas_->retranslate();
    if (layoutInspector_)
        layoutInspector_->retranslate();
    // Note cards cache translated placeholders, fold labels and accessibility
    // names, so drop them and let renderNotes() build them again.
    for (auto it = noteCards_.begin(); it != noteCards_.end(); ++it)
        if (it.value()) {
            noteLayout_->removeWidget(it.value());
            it.value()->hide();
            it.value()->deleteLater();
        }
    noteCards_.clear();
    noteEditors_.clear();
    if (hasDocument())
        renderNotes();
    else {
        noteCount_->setText(tr("批注 %1 条").arg(0));
        emptyNotes_->hide();
    }
    updateLayoutControls();
}
void Editor::updatePageContextLabel() {
    const QJsonObject context = video_ && (videoPreview_ || !hasDocument() || !doc_.capture.contains("page"))
                                    ? video_->capture : doc_.capture;
    const QString page = context["page"].toString();
    const QSize size = hasDocument() ? doc_.image.size() : videoPreview_ ? canvas_->imageSize() : QSize();
    QString base = size.isEmpty() ? QString() : QString("%1 × %2").arg(size.width()).arg(size.height());
    if (video_) base += " · " + videoTimeLabel(videoPreview_ ? video_->positionMs : std::max<qint64>(0, videoFrameUs_) / 1000);
    const QString full = page.isEmpty() ? base : base.isEmpty() ? page : base + " · " + page;
    meta_->setText(QFontMetrics(meta_->font()).elidedText(full, Qt::ElideRight, 350));
    meta_->setToolTip(full);
    meta_->setProperty("pageContext", page);
}
void Editor::setPageContext(const QString &page) {
    const QString trimmed = page.trimmed();
    validatePageContext(trimmed);
    auto apply = [&](QJsonObject &capture, const QString &source, const QString &at) {
        if (capture.isEmpty()) capture = {{"source", source}, {"capturedAt", at}};
        capture["page"] = trimmed; capture["pageSource"] = "agent"; validateCaptureContext(capture);
    };
    if (video_) {
        apply(video_->capture, "video", timestamp());
        for (auto &frame : video_->frames) { apply(frame.document.capture, frame.document.source, frame.document.createdAt); frame.document.dirty = true; }
        video_->dirty = true;
    }
    if (hasDocument()) { apply(doc_.capture, doc_.source, doc_.createdAt); doc_.dirty = true; commitVideoFrame(); }
    updatePageContextLabel();
}
void Editor::showGuide() {
    finishNoteEdit();
    if (!hasDocument()) setDocument(fromImage(exampleImage(), "demo", tr("引导示例")));
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
    updateToolLabels();
}
void Editor::updateToolLabels() {
    const QStringList ids{"smart", "point", "rectangle", "adjust"};
    const QStringList labels{tr("智能选块"), tr("点标注"), tr("框选"), tr("调整批注")};
    auto label = [this](const QString &id, const QString &name) {
        const auto shortcut = shortcuts_.value(id);
        const auto key = shortcut ? shortcut->key().toString(QKeySequence::NativeText) : QString();
        return key.isEmpty() ? name : name + " (" + key + ")";
    };
    for (int i = 0; i < modes_.size(); ++i)
        modes_[i]->setToolTip(label(ids[i], labels[i]));
    explosion_->setToolTip(label("explode", tr("大爆炸")));
}
void Editor::setDocument(Document document, const QString &projectPath) {
    dismissGuide();
    finishNoteEdit();
    const bool preserveView = switchingVideoFrame_ && canvas_->imageSize() == document.image.size();
    const auto previousMode = canvas_->mode();
    const auto previousOrigin = imageScroll_->imageOrigin();
    const bool previousFitted = fitted_;
    if (!switchingVideoFrame_) clearVideo();
    if (!preserveView) {
        annotationsVisible_ = true;
        updateAnnotationVisibility();
    }
    resetLayoutTools();
    cancelRegionRecognition();
    doc_ = std::move(document);
    normalizeNoteParents(doc_);
    for (const auto &note : doc_.notes) recognizedRegions_.insert(note.id, recognitionKey(note));
    videoPreview_ = false;
    if (!switchingVideoFrame_) projectPath_ = projectPath;
    detailsStack_->setEnabled(true);
    undoHistory_.clear();
    redoHistory_.clear();
    canvas_->setDocument(&doc_);
    if (preserveView) canvas_->setMode(previousMode);
    else setMode(static_cast<Canvas::Mode>(preferences_.defaultTool));
    fitted_ = preserveView ? previousFitted : preferences_.fitImageOnOpen;
    canvas_->setLayoutPreview(doc_.layout.has_value());
    explosion_->setEnabled(doc_.layout.has_value());
    updateLayoutControls();
    if (!preserveView) detailsStack_->show();
    detailsStack_->setCurrentWidget(notesPanel_);
    meta_->setText(QString("%1 × %2").arg(doc_.image.width()).arg(doc_.image.height()));
    updatePageContextLabel();
    renderNotes();
    if (!preserveView) {
        if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
        show();
    }
    if (!preserveView) configureNativeWindow(this, false);
    QRect available = QGuiApplication::screenAt(QCursor::pos())
                          ? QGuiApplication::screenAt(QCursor::pos())->availableGeometry()
                          : QGuiApplication::primaryScreen()->availableGeometry();
    if (!switchingVideoFrame_ && !isFullScreen() && !isMaximized()) {
        resize(std::min(1260, available.width() - 60), std::min(850, available.height() - 80));
        move(available.center() - rect().center());
    }
    const int generation = ++generation_;
    if (preserveView) imageScroll_->setImageOrigin(previousOrigin);
    else QTimer::singleShot(0, this, [this, generation] {
        if (generation != generation_)
            return;
        if (preferences_.fitImageOnOpen)
            fit();
        else {
            zoom(1.0);
            imageScroll_->centerImage();
        }
    });
    // Only analyze a video frame once the user stops stepping through the timeline.
    if (switchingVideoFrame_) QTimer::singleShot(250, this, [this, generation] { detectCurrentFrame(generation); });
    else detectCurrentFrame(generation);
    if (!preserveView) {
        raise(); activateWindow(); canvas_->setFocus();
    }
    syncVideoGeometry();
    if (videoPlayback_) videoPlayback_->videoWidget()->hide();
    updateControls();
    updateEmptyState();
}
void Editor::detectCurrentFrame(int generation) {
    if (generation != generation_) return;
    if (video_ && (videoPlayback_->playing() || videoPlayback_->positioning())) {
        QTimer::singleShot(250, this, [this, generation] { detectCurrentFrame(generation); });
        return;
    }
    auto watcher = new QFutureWatcher<QVector<Candidate>>(this);
    connect(watcher, &QFutureWatcher<QVector<Candidate>>::finished, this, [this, watcher, generation] {
        if (generation == generation_) {
            auto candidates = watcher->result();
            doc_.candidates += candidates;
            auto whole = manualTarget();
            whole["label"] = QT_TRANSLATE_NOOP("EditHere", "整个图片");
            doc_.candidates.append({QRect(QPoint(0, 0), doc_.image.size()), whole});
            canvas_->refresh();
            explosion_->setEnabled(!video_ || (!videoPlayback_->playing() && !videoPlayback_->positioning()));
            updateLayoutControls();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([image = doc_.image] { return detectBlocks(image); }));
}
void Editor::clearVideo() {
    video_.reset(); videoFrameUs_ = -1;
    videoPreview_ = false;
    canvas_->setPreviewSize({});
    if (videoPlayback_) { videoPlayback_->clear(); videoPlayback_->hide(); videoPlayback_->videoWidget()->hide(); }
    if (detailsStack_) detailsStack_->setEnabled(true);
}
void Editor::loadMedia(const QString &path) {
    if (isVideoFile(path)) {
        if (!QFileInfo(path).isFile()) throw std::runtime_error(tr("视频文件不存在").toStdString());
        VideoProject project;
        project.source = QFileInfo(path).absoluteFilePath();
        project.capture = {{"source", "video"}, {"capturedAt", timestamp()}, {"fileName", QFileInfo(path).fileName().left(1024)}};
        setVideoProject(std::move(project));
    } else if (isVideoProjectFile(path)) {
        setVideoProject(loadVideoProject(path), path.endsWith(".edithere", Qt::CaseInsensitive) ? path : QString());
    } else {
        setDocument(loadDocument(path), path.endsWith(".edithere", Qt::CaseInsensitive) ? path : QString());
    }
}
void Editor::setVideoProject(VideoProject project, const QString &path) {
    dismissGuide(); finishNoteEdit(); clearVideo(); resetLayoutTools(); ++generation_;
    video_ = std::move(project); projectPath_ = path;
    doc_ = {}; canvas_->setDocument(nullptr); undoHistory_.clear(); redoHistory_.clear();
    videoPlayback_->show();
    renderNotes(); updateControls(); updateEmptyState();
    show(); configureNativeWindow(this, false); raise(); activateWindow();
    if (!video_->frames.isEmpty()) {
        // Review is available independently of source-video availability.
        const auto frame = video_->frames.first();
        switchingVideoFrame_ = true;
        setDocument(frame.document);
        switchingVideoFrame_ = false;
        videoFrameUs_ = frame.timestampUs;
        videoPlayback_->reviewAt(frame.timestampUs);
    }
    const QUrl source(video_->source);
    // A saved project can move from Windows to another OS. QFileInfo there
    // treats C:/... as relative while QUrl treats C as a protocol; retain it as
    // a missing local source rather than asking the media backend to open it.
    const bool windowsPath = video_->source.size() >= 3 && video_->source[0].isLetter() &&
                             video_->source[1] == ':' &&
                             (video_->source[2] == '/' || video_->source[2] == '\\');
    const bool local = windowsPath || QFileInfo(video_->source).isAbsolute() ||
                       source.scheme().isEmpty() || source.isLocalFile();
    const QString localPath = source.isLocalFile() ? source.toLocalFile() : video_->source;
    if (!local || QFileInfo(localPath).isFile())
        videoPlayback_->open(video_->source, video_->positionMs);
    else toast(tr("源视频未找到，可查看已保存的批注画面，或重新指定视频。"));
    updateVideoFrames();
    updatePageContextLabel();
}
void Editor::commitVideoFrame() {
    if (!video_ || videoFrameUs_ < 0 || doc_.image.isNull()) return;
    if (doc_.dirty) video_->dirty = true;
    const bool annotated = !exportFeedback(doc_)["objects"].toArray().isEmpty();
    auto it = std::find_if(video_->frames.begin(), video_->frames.end(), [this](const VideoFrame &frame) {
        return frame.timestampUs == videoFrameUs_;
    });
    if (annotated) {
        if (it == video_->frames.end()) video_->frames.append({videoFrameUs_, doc_});
        else it->document = doc_;
    } else if (it != video_->frames.end()) video_->frames.erase(it);
    std::sort(video_->frames.begin(), video_->frames.end(), [](const VideoFrame &a, const VideoFrame &b) {
        return a.timestampUs < b.timestampUs;
    });
    updateVideoFrames();
}
void Editor::showVideoPreview(const QSize &size, qint64 timestampUs) {
    if (!video_ || size.isEmpty()) return;
    const bool preserveView = canvas_->imageSize() == size;
    const bool changeSurface = !videoPreview_ || !preserveView;
    const auto previousOrigin = imageScroll_->imageOrigin();
    if (!videoPreview_) {
        finishNoteEdit(); commitVideoFrame(); resetLayoutTools();
        doc_ = {}; videoFrameUs_ = -1; undoHistory_.clear(); redoHistory_.clear();
        ++generation_; // Invalidate analysis belonging to the previous captured frame.
        renderNotes();
    }
    videoPreview_ = true;
    if (changeSurface) canvas_->setPreviewSize(size);
    if (preserveView) imageScroll_->setImageOrigin(previousOrigin);
    else {
        fitted_ = preferences_.fitImageOnOpen;
        if (fitted_) fit();
        else { canvas_->setZoom(1); imageScroll_->centerImage(); }
        const int generation = generation_;
        QTimer::singleShot(0, this, [this, generation] {
            if (generation == generation_ && videoPreview_ && fitted_) fit();
        });
    }
    video_->positionMs = std::max<qint64>(0, timestampUs / 1000);
    meta_->setText(tr("%1 × %2 · %3").arg(size.width()).arg(size.height()).arg(videoTimeLabel(video_->positionMs)));
    updatePageContextLabel();
    syncVideoGeometry();
    auto view = videoPlayback_->videoWidget();
    if (!view->isVisible()) { view->show(); view->raise(); }
    detailsStack_->setEnabled(!videoPlayback_->playing() && !videoPlayback_->positioning());
    updateControls(); updateEmptyState();
}
bool Editor::ensureVideoAnnotationFrame() {
    if (!video_ || !videoPreview_) return hasDocument();
    if (videoPlayback_->playing() || videoPlayback_->positioning()) return false;
    const bool captured = videoPlayback_->captureCurrentFrame();
    // A pixel readback can succeed while creating its editable document fails.
    // Leave the video visible and let the next annotation attempt retry.
    if (!hasDocument()) videoPlayback_->cancelFrameCapture();
    return captured && hasDocument();
}
void Editor::displayVideoFrame(const QImage &image, qint64 timestampUs) {
    if (!video_) return;
    try {
        finishNoteEdit(); commitVideoFrame();
        // Keep the current draft and undo history when pausing again on the same frame.
        if (videoFrameUs_ == timestampUs && hasDocument()) {
            videoPlayback_->videoWidget()->hide(); detailsStack_->setEnabled(true); updateControls(); return;
        }
        Document next;
        const auto it = std::find_if(video_->frames.cbegin(), video_->frames.cend(), [timestampUs](const VideoFrame &frame) {
            return frame.timestampUs == timestampUs;
        });
        if (it != video_->frames.cend()) next = it->document;
        else {
            next = fromImage(image.convertToFormat(QImage::Format_RGB32), "video", tr("视频画面 %1").arg(videoTimeLabel(timestampUs / 1000)), true);
            if (!video_->capture.isEmpty()) {
                const auto capturedAt = next.createdAt; next.capture = video_->capture; next.capture["capturedAt"] = capturedAt;
            }
        }
        switchingVideoFrame_ = true;
        setDocument(std::move(next));
        switchingVideoFrame_ = false;
        videoFrameUs_ = timestampUs;
        video_->positionMs = timestampUs / 1000;
        meta_->setText(tr("%1 × %2 · %3").arg(doc_.image.width()).arg(doc_.image.height()).arg(videoTimeLabel(timestampUs / 1000)));
        updatePageContextLabel();
        updateVideoFrames();
    } catch (const std::exception &error) {
        switchingVideoFrame_ = false;
        showError(QString::fromUtf8(error.what()));
    }
}
void Editor::reviewVideoFrame(qint64 timestampUs) {
    if (!video_) return;
    finishNoteEdit(); commitVideoFrame();
    const auto it = std::find_if(video_->frames.cbegin(), video_->frames.cend(), [timestampUs](const VideoFrame &frame) {
        return frame.timestampUs == timestampUs;
    });
    if (it == video_->frames.cend()) return;
    const auto document = it->document;
    videoPlayback_->reviewAt(timestampUs);
    switchingVideoFrame_ = true; setDocument(document); switchingVideoFrame_ = false;
    videoFrameUs_ = timestampUs; video_->positionMs = timestampUs / 1000;
    meta_->setText(tr("%1 × %2 · %3").arg(doc_.image.width()).arg(doc_.image.height()).arg(videoTimeLabel(timestampUs / 1000)));
    updatePageContextLabel();
    updateVideoFrames();
}
void Editor::updateVideoFrames() {
    if (!video_) return;
    QVector<QPair<qint64, int>> frames;
    for (const auto &frame : video_->frames) frames.append({frame.timestampUs, annotationCount(frame.document)});
    videoPlayback_->setAnnotatedFrames(frames);
}
void Editor::syncVideoGeometry() {
    if (!videoPlayback_ || canvas_->imageSize().isEmpty()) return;
    auto view = videoPlayback_->videoWidget();
    view->setGeometry(QRect(canvas_->mapTo(imageScroll_->viewport(), QPoint()), canvas_->size()));
}
VideoProject Editor::videoProject() {
    finishNoteEdit(); commitVideoFrame();
    if (!video_) throw std::runtime_error("No video project is open");
    if (videoPreview_) video_->positionMs = videoPlayback_->positionMs();
    return *video_;
}
int Editor::totalAnnotationCount() {
    if (video_) { finishNoteEdit(); commitVideoFrame(); return videoAnnotationCount(*video_); }
    return annotationCount(doc_);
}
QByteArray Editor::projectBytes() {
    finishNoteEdit(); commitVideoFrame();
    if (video_ && videoPreview_) video_->positionMs = videoPlayback_->positionMs();
    return video_ ? serializeVideoProject(*video_) : serializeDocument(doc_, true);
}
QByteArray Editor::feedbackBytes(bool embed, bool compress) {
    finishNoteEdit(); commitVideoFrame();
    FeedbackExportOptions options; options.mode = embed ? ImageExportMode::Embedded : ImageExportMode::None;
    options.compress = compress; options.includeRecognizedText = preferences_.includeRecognizedText; options.includeWindowTitle = preferences_.includeWindowTitle;
    return video_ ? serializeVideoFeedback(*video_, options) : serializeFeedback(doc_, options);
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
    const QString label = full ? tr("退出全屏") : tr("全屏");
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
    if (event->type() == QEvent::LanguageChange) {
        // Cards cache their own placeholder and fold labels, so commit the pending
        // edit and let renderNotes() build them again in the new language.
        finishNoteEdit();
        retranslate();
    }
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        stopViewportPan();
}
void Editor::panImage(QPoint delta) {
    if (canvas_->imageSize().isEmpty() || delta.isNull()) return;
    fitted_ = false;
    imageScroll_->setImageOrigin(imageScroll_->imageOrigin() + delta);
}
void Editor::stopViewportPan() {
    if (!viewportPanning_) return;
    viewportPanning_ = false;
    imageScroll_->viewport()->unsetCursor();
}
void Editor::fit() {
    if (canvas_->imageSize().isEmpty())
        return;
    fitted_ = true;
    QSize area = imageScroll_->viewport()->size() - QSize(36, 36);
    const auto imageSize = canvas_->imageSize();
    canvas_->setZoom(std::min({1.0, double(std::max(80, area.width())) / imageSize.width(),
                               double(std::max(80, area.height())) / imageSize.height()}));
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
    if (canvas_->imageSize().isEmpty())
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
    if (emptyWell_)
        emptyWell_->setGeometry(imageScroll_->viewport()->rect());
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
    if (copyJsonTextOptions_) {
        copyJsonTextOptions_->setToolTip(tr("复制 JSON 内容选项"));
        copyJsonTextOptions_->setAccessibleName(tr("复制 JSON 内容选项"));
    }
    for (const auto &definition : toolbarActionDefinitions()) {
        auto button = outputButtons_.value(definition.id);
        if (!button) continue;
        button->setVisible(preferences_.toolbarActions.contains(definition.id) ||
                           (definition.id == "copyJsonText" && preferences_.toolbarActions.contains("copyJson")));
        button->setToolTip(definition.id == "copyJson"
            ? tr("直接复制文件（携带原图信息），适用于可以发送文件的 Agent。")
            : definition.id == "copyJsonText"
                ? (preferences_.copyJsonTextWithImage
                    ? tr("复制 JSON 文本（携带原图信息），适用于无法发送文件的 Agent。")
                    : tr("复制 JSON 文本（默认不携带原图信息），适用于无法发送文件的 Agent。"))
                : definition.label);
        button->setAccessibleName(definition.label);
        const bool labelled = definition.id == "copyJson" || definition.id == "copyJsonText" ||
                              definition.id == "exportJson" || (!compact && definition.id == "copyImage");
        button->setProperty("tool", !labelled);
        button->setProperty("primary", definition.id == "copyJson");
        button->setText(labelled ? definition.label : QString());
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->setIcon(glyph(button->property("glyphName").toString(), definition.id == "copyJson" ? QColor(Qt::white) : QColor()));
        // Let the polished style account for the icon, spacing, padding and border.
        // A fixed allowance beyond the text width clips labels inside the padded content area.
        button->setFixedSize(labelled ? button->sizeHint().width() : 34, 34);
    }
    // Keep the permanent tools accessible when optional actions exceed the window width.
    if (dock_) {
        if (copyJsonTextOptions_)
            copyJsonTextOptions_->setVisible(!outputButtons_.value("copyJsonText")->isHidden());
        dock_->invalidate();
        const QStringList overflowOrder{"fit", "capture", "copyImage", "saveImage", "exportJson", "saveProject", "copyJsonText", "copyJson"};
        for (const auto &id : overflowOrder) {
            if (dock_->sizeHint().width() <= width()-2) break;
            if (auto button = outputButtons_.value(id); button && !button->isHidden()) {
                button->hide();
                if (id == "copyJsonText" && copyJsonTextOptions_) copyJsonTextOptions_->hide();
                dock_->invalidate();
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
    const bool playingVideo = video_ && (videoPlayback_->playing() || videoPlayback_->positioning());
    const bool canAnnotate = hasDocument() || (videoPreview_ && !canvas_->imageSize().isEmpty());
    for (int i = 0; i < modes_.size(); i++)
        modes_[i]->setEnabled(!playingVideo && canAnnotate);
    if (auto global = findChild<QPushButton *>("addGlobalNote")) global->setEnabled(!playingVideo && canAnnotate);
    if (playingVideo) explosion_->setEnabled(false);
    else if (videoPreview_) explosion_->setEnabled(canAnnotate);
    for (int i = 0; i < modes_.size(); i++)
        modes_[i]->setChecked(i == (componentEditing_ ? (layoutCanvas_->drawingMode() ? Canvas::Rectangle : Canvas::Smart) : canvas_->mode()));
    bool pendingDraft = false;
    if (!editingId_.isEmpty() && !draftId_.isEmpty()) {
        for (const auto &note : doc_.notes)
            if (note.id == draftId_) { pendingDraft = !note.comment.trimmed().isEmpty(); break; }
    }
    undo_->setEnabled(!undoHistory_.isEmpty() || pendingDraft);
    redo_->setEnabled(!redoHistory_.isEmpty());
    notesToggle_->setChecked(!detailsStack_->isHidden());
    notesToggle_->setToolTip(detailsStack_->isHidden() ? tr("展开批注框") : tr("收起批注框"));
    for (auto it = outputButtons_.begin(); it != outputButtons_.end(); ++it)
        it.value()->setEnabled(it.key()=="capture" || hasDocument() || video_.has_value());
    if (copyJsonTextOptions_) copyJsonTextOptions_->setEnabled(hasDocument() || video_.has_value());
    hideAnnotations_->setEnabled(hasDocument());
}
QString Editor::coords(const Note &n) const {
    if (n.isGlobal) return tr("全局");
    if (n.movementSource) return tr("位置变化");
    return n.isPoint ? tr("点 (%1,%2)").arg(n.point.x()).arg(n.point.y())
                     : tr("框 (%1,%2)→(%3,%4)").arg(n.rect.x()).arg(n.rect.y())
                           .arg(n.rect.x() + n.rect.width()).arg(n.rect.y() + n.rect.height());
}
void Editor::renderNotes() {
    if (renderingNotes_) return;
    renderingNotes_ = true;
    const auto markers = doc_.layout ? movementMarkers(*doc_.layout, doc_.notes) : QVector<MovementMarker>{};
    const auto orphanKey = [](const QRectF &source) {
        return "__mv_" + QString::number(qint64(source.left())) + "_" + QString::number(qint64(source.top()));
    };
    QSet<QString> retained;
    for (const auto &note : doc_.notes) retained.insert(note.id);
    // A focus-out or OCR refresh can happen between a button press and release.
    // Keep movement-only cards alive while their movement still exists.
    for (const auto &marker : markers)
        if (marker.noteIndex < 0) retained.insert(orphanKey(marker.source));
    for (auto it = noteCards_.begin(); it != noteCards_.end();) {
        if (!retained.contains(it.key())) {
            if (it.value()) {
                noteLayout_->removeWidget(it.value());
                // A frame switch can leave the old card queued for deletion for
                // one event turn. Do not let its answer buttons shadow the
                // visible card when callers look them up by object name.
                for (auto button : it.value()->findChildren<QPushButton *>())
                    if (button->objectName() == "keepAsChild" || button->objectName() == "keepAsStandalone")
                        button->setObjectName({});
                it.value()->hide(); it.value()->deleteLater();
            }
            noteEditors_.remove(it.key());
            it = noteCards_.erase(it);
        } else ++it;
    }
    emptyNotes_->setVisible(annotationCount(doc_) == 0);
    noteCount_->setText(tr("批注 %1 条").arg(annotationCount(doc_)));
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
            auto edit = iconButton("edit", tr("编辑批注"), card), remove = iconButton("trash", tr("删除批注"), card);
            edit->setFixedSize(24, 24); remove->setFixedSize(24, 24);
            edit->setIconSize({16, 16}); remove->setIconSize({16, 16});
            row->addWidget(edit); row->addWidget(remove);
            l->addLayout(row);
            auto text = new InlineNoteEdit(card);
            text->setObjectName("noteText_" + n.id);
            text->setProperty("noteId", n.id);
            text->setAccessibleName(tr("批注内容"));
            text->setPlaceholderText(tr("写下你的想法…"));
            text->setTabChangesFocus(true);
            text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            l->addWidget(text);
            // The question is only revealed on hover so an unresolved note stays
            // compact; ✅/❌ answer it. Until one is chosen the export is blocked.
            auto prompt = new QWidget(card);
            prompt->setObjectName("parentPrompt");
            prompt->setCursor(Qt::PointingHandCursor);
            auto promptLayout = new QHBoxLayout(prompt);
            promptLayout->setContentsMargins(0, 2, 0, 0); promptLayout->setSpacing(4);
            auto question = mutedLabel(tr("归属待确认"), prompt);
            question->setObjectName("parentPromptText");
            question->setWordWrap(true);
            promptLayout->addWidget(question, 1);
            auto keepChild = new QPushButton(QStringLiteral("✅"), prompt);
            keepChild->setObjectName("keepAsChild");
            keepChild->setAccessibleName(tr("是，作为子批注"));
            auto keepAlone = new QPushButton(QStringLiteral("❌"), prompt);
            keepAlone->setObjectName("keepAsStandalone");
            keepAlone->setAccessibleName(tr("否，独立对象"));
            for (auto button : {keepChild, keepAlone}) {
                button->setFixedSize(24, 20);
                button->setCursor(Qt::PointingHandCursor);
                promptLayout->addWidget(button);
            }
            prompt->installEventFilter(this);
            l->addWidget(prompt);
            connect(keepChild, &QPushButton::clicked, this, [this, id = n.id] {
                finishNoteEdit();
                std::optional<Note> parent;
                for (const auto &note : doc_.notes) if (note.id == id && note.parent)
                    parent = missingMovementNote(note.parent->source);
                if (doc_.notes.size() + (parent ? 1 : 0) > MaxNotes) {
                    showError(tr("最多支持 1000 条批注")); return;
                }
                remember();
                if (parent) doc_.notes.append(*parent);
                for (auto &note : doc_.notes) if (note.id == id) {
                    if (note.parent) note.parent->state = ParentState::Child;
                    break;
                }
                doc_.dirty = true; changed();
            });
            connect(keepAlone, &QPushButton::clicked, this, [this, id = n.id] {
                finishNoteEdit(); remember();
                for (auto &note : doc_.notes) if (note.id == id) {
                    note.parent.reset();
                    break;
                }
                doc_.dirty = true; changed();
            });
            auto recognition = new QWidget(card);
            recognition->setObjectName("recognizedTextSection");
            auto recognitionLayout = new QVBoxLayout(recognition);
            recognitionLayout->setContentsMargins(0, 4, 0, 0); recognitionLayout->setSpacing(2);
            auto recognitionRow = new QHBoxLayout;
            auto recognitionLabel = mutedLabel(tr("区域文字"), recognition);
            recognitionLabel->setObjectName("recognizedTextLabel"); recognitionRow->addWidget(recognitionLabel, 1);
            auto clearRecognition = iconButton("trash", tr("删除识别文字"), recognition);
            clearRecognition->setObjectName("clearRecognizedText"); clearRecognition->setFixedSize(24, 24);
            recognitionRow->addWidget(clearRecognition); recognitionLayout->addLayout(recognitionRow);
            auto recognized = new InlineNoteEdit(recognition);
            recognized->setObjectName("recognizedText_" + n.id); recognized->setProperty("noteId", n.id);
            recognized->setAccessibleName(tr("识别的区域文字")); recognized->setTabChangesFocus(true);
            recognized->setExpanded(false); recognitionLayout->addWidget(recognized);
            l->addWidget(recognition);
            recognized->started = [this, id=n.id] { beginNoteEdit(id); };
            recognized->finished = [this, id=n.id] { QTimer::singleShot(0, this, [this,id] { if (editingId_ == id) finishNoteEdit(); }); };
            recognized->cancelled = [this] { cancelNoteEdit(); };
            connect(recognized, &QPlainTextEdit::textChanged, this, [this,recognized,id=n.id] {
                recognized->fitContent();
                if (renderingNotes_) return;
                beginNoteEdit(id);
                auto value = recognized->toPlainText().left(500);
                if (!value.isEmpty() && value.back().isHighSurrogate()) value.chop(1);
                if (recognized->toPlainText() != value) { QSignalBlocker blocker(recognized); recognized->setPlainText(value); }
                for (auto &note : doc_.notes) if (note.id == id) {
                    if (value.trimmed().isEmpty()) updateRecognizedText(id, {{"text", ""}, {"edited", true}});
                    else { auto metadata = note.recognizedText; metadata.insert("text", value); metadata.insert("edited", true); metadata.remove("confidence"); updateRecognizedText(id, metadata); }
                    break;
                }
                doc_.dirty = true;
            });
            connect(clearRecognition, &QPushButton::clicked, this, [this,id=n.id] {
                finishNoteEdit(); remember();
                updateRecognizedText(id, {{"text", ""}, {"edited", true}});
                changed();
            });
            auto fold = new QPushButton(card);
            fold->setObjectName("foldNote");
            fold->setProperty("noteId", n.id);
            fold->setCursor(Qt::PointingHandCursor);
            fold->setFocusPolicy(Qt::NoFocus);
            fold->hide();
            l->addWidget(fold, 0, Qt::AlignRight);
            text->presentationChanged = [text, fold] {
                fold->setVisible(text->needsCollapse());
                fold->setText(text->isExpanded() ? tr("收起") : tr("展开"));
                fold->setAccessibleName(text->isExpanded() ? tr("收起批注全文") : tr("展开批注全文"));
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
                for (auto &note : doc_.notes) if (note.id == id) {
                    note.comment=value;
                    note.updatedAt=timestamp();
                    editingTimestampUpdated_=true;
                    break;
                }
                doc_.dirty = true;
                canvas_->refresh();
                if (layoutCanvas_) layoutCanvas_->setAnnotations(doc_.notes);
                revealNote(id);
                updateControls();
            });
            connect(text, &QPlainTextEdit::cursorPositionChanged, this, [this,text,id=n.id] {
                if (!renderingNotes_ && text->hasFocus() && editingId_ == id) revealNote(id);
            });
            connect(edit, &QPushButton::clicked, this, [this,id=n.id] { focusNote(id); });
            connect(remove, &QPushButton::clicked, this, [this,id=n.id] {
                deleteNote(id);
            });
        }
        if (n.movementSource) {
            card->installEventFilter(this);
            card->setProperty("movementSource", *n.movementSource);
            card->setProperty("movementDestination", QRectF(n.rect));
            card->setCursor(Qt::PointingHandCursor);
            for (auto label : card->findChildren<QLabel *>()) label->installEventFilter(this);
        } else {
            card->setProperty("movementSource", QVariant());
            card->setProperty("movementDestination", QVariant());
            card->setCursor(Qt::ArrowCursor);
        }
        card->setProperty("selected", canvas_->selected().contains(n.id));
        card->setProperty("unresolvedParent", n.parent && n.parent->state == ParentState::Unknown);
        card->setProperty("childNote", n.parent && n.parent->state == ParentState::Child);
        if (auto prompt = card->findChild<QWidget *>("parentPrompt"))
            prompt->setVisible(n.parent && n.parent->state == ParentState::Unknown);
        card->style()->unpolish(card); card->style()->polish(card);
        card->findChild<QLabel *>("noteBadge")->setText(QString::number(++number));
        auto position = card->findChild<QLabel *>("noteCoordinates");
        const QString coordinates = coords(n);
        position->setText(n.parent && n.parent->state == ParentState::Child
                              ? tr("属于上级 · %1").arg(coordinates) : coordinates);
        position->setToolTip(coordinates);
        auto text = static_cast<InlineNoteEdit *>(noteEditors_.value(n.id).data());
        if (!text->hasFocus() && text->toPlainText()!=n.comment) { QSignalBlocker blocker(text); text->setPlainText(n.comment); }
        text->setAccessibleName(tr("批注 %1 内容").arg(number));
        text->fitContent();
        auto recognition = card->findChild<QWidget *>("recognizedTextSection");
        auto recognized = static_cast<InlineNoteEdit *>(recognition->findChild<QPlainTextEdit *>("recognizedText_" + n.id));
        const auto recognizedText = n.recognizedText["text"].toString();
        if (!recognized->hasFocus() && recognized->toPlainText() != recognizedText) { QSignalBlocker blocker(recognized); recognized->setPlainText(recognizedText); }
        recognized->fitContent();
        recognition->setVisible(!recognizedText.isEmpty());
        noteLayout_->insertWidget(number-1,card,0,Qt::AlignTop);
    }
    // Display movements without text annotations as orphan cards.
    if (doc_.layout) {
        for (const auto &marker : markers) {
            if (marker.noteIndex >= 0)
                continue; // already shown as a note card
            const QString key = orphanKey(marker.source);
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
                auto remove = iconButton("trash", tr("删除批注"), card);
                remove->setFixedSize(24, 24); row->addWidget(remove);
                connect(remove, &QPushButton::clicked, this, [this,card,key] {
                    deleteNote(key, card->property("movementSource").toRectF());
                });
                l->addLayout(row);
                auto text = mutedLabel(tr("点击此处为这次移动添加文字…"), card);
                text->setWordWrap(true);
                l->addWidget(text);
                card->setCursor(Qt::PointingHandCursor);
                card->installEventFilter(this);
                for (auto label : card->findChildren<QLabel *>())
                    label->installEventFilter(this);
                noteCards_.insert(key, card);
            }
            card->setProperty("movementSource", marker.source);
            card->setProperty("movementDestination", marker.destination);
            card->setProperty("selected", false);
            card->style()->unpolish(card); card->style()->polish(card);
            card->findChild<QLabel *>("noteBadge")->setText(QString::number(marker.number));
            auto position = card->findChild<QLabel *>("noteCoordinates");
            const QString coords = tr("移动 (%1,%2) → (%3,%4)")
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
    editingBaseline_={doc_.notes,doc_.layout}; editingWasDirty_=doc_.dirty; editingTimestampUpdated_=false; editingId_=id;
    canvas_->select({id});
}
void Editor::finishNoteEdit() {
    if (editingId_.isEmpty() || finishingEdit_) return;
    finishingEdit_=true;
    const QString id=editingId_;
    for (auto &note : doc_.notes) if (note.id==id) {
        note.comment=note.comment.trimmed();
        if (note.comment.isEmpty() && id!=draftId_)
            for (const auto &before : editingBaseline_.notes) if (before.id==id) note.comment=before.comment;
        break;
    }
    if (id==draftId_) {
        const auto draft = std::find_if(doc_.notes.cbegin(), doc_.notes.cend(),
                                       [&](const Note &note) { return note.id == id; });
        // An empty/cancelled child also rolls back its automatically added card.
        if (draft != doc_.notes.cend() && draft->comment.isEmpty()) doc_.notes = editingBaseline_.notes;
    }
    const bool modified=doc_.notes!=editingBaseline_.notes;
    if (modified) {
        for (auto &note : doc_.notes) if (note.id==id) {
            const auto before = std::find_if(editingBaseline_.notes.cbegin(), editingBaseline_.notes.cend(),
                                             [&](const Note &candidate) { return candidate.id == id; });
            if (id != draftId_ && !editingTimestampUpdated_ &&
                (before == editingBaseline_.notes.cend() || note.updatedAt == before->updatedAt))
                note.updatedAt = timestamp();
            break;
        }
        undoHistory_.append(editingBaseline_); if (undoHistory_.size()>60) undoHistory_.removeFirst(); redoHistory_.clear();
    }
    doc_.dirty=editingWasDirty_ || modified;
    editingId_.clear(); draftId_.clear(); editingTimestampUpdated_ = false;
    finishingEdit_=false;
    changed(false);
}
void Editor::cancelNoteEdit() {
    if (editingId_.isEmpty()) return;
    doc_.notes=editingBaseline_.notes; doc_.dirty=editingWasDirty_;
    editingId_.clear(); draftId_.clear(); editingTimestampUpdated_ = false;
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
        // A movement note describes the component itself, so it can never become
        // a child of that same component.
        if (!note.isGlobal && !note.isPoint && !note.movementSource && !note.parent)
            note.parent = classifyParent(note.rect, doc_.layout);
        const auto parent = note.parent && note.parent->state == ParentState::Child
            ? missingMovementNote(note.parent->source) : std::optional<Note>{};
        if (doc_.notes.size() + 1 + (parent ? 1 : 0) > MaxNotes) {
            showError(tr("最多支持 1000 条批注")); return;
        }
        editingBaseline_={doc_.notes,doc_.layout}; editingWasDirty_=doc_.dirty; editingTimestampUpdated_=false;
        editingId_=note.id; draftId_=note.id;
        if (parent) doc_.notes.append(*parent);
        doc_.notes.append(note);
        normalizeNoteParents(doc_);
        scheduleRegionRecognition();
    }
    focusNote(note.id);
}
std::optional<Note> Editor::missingMovementNote(const QRectF &source) const {
    for (const auto &note : doc_.notes)
        if (note.movementSource && noteSourceKey(*note.movementSource) == noteSourceKey(source)) return {};
    if (!doc_.layout) return {};
    for (const auto &movement : layoutMovements(*doc_.layout)) {
        if (noteSourceKey(movement.source) != noteSourceKey(source)) continue;
        Note parent;
        parent.isPoint = false;
        parent.movementSource = movement.source;
        parent.rect = movement.destination.toAlignedRect();
        return parent;
    }
    return {};
}
void Editor::updateRecognizedText(const QString &id, const QJsonObject &text) {
    std::optional<QRectF> source;
    for (const auto &note : doc_.notes) if (note.id == id) { source = note.movementSource; break; }
    for (auto &note : doc_.notes)
        if (note.id == id || (source && note.movementSource == source)) note.recognizedText = text;
}
QByteArray Editor::recognitionKey(const Note &note) const {
    QJsonObject value{{"global", note.isGlobal}, {"point", note.isPoint},
                      {"x", note.point.x()}, {"y", note.point.y()}, {"rect", rectJson(note.rect)}};
    if (note.movementSource) {
        const auto r = *note.movementSource;
        value = {{"sourceX", r.x()}, {"sourceY", r.y()}, {"sourceW", r.width()}, {"sourceH", r.height()}};
    }
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}
void Editor::cancelRegionRecognition() {
    ++recognitionGeneration_;
    recognitionScheduled_ = false;
    recognizedRegions_.clear();
    if (regionOcr_) regionOcr_->cancel();
}
void Editor::scheduleRegionRecognition() {
    if (recognitionScheduled_ || !hasDocument()) return;
    recognitionScheduled_ = true;
    const int generation = recognitionGeneration_;
    QTimer::singleShot(200, this, [this, generation] {
        if (generation != recognitionGeneration_) return;
        recognitionScheduled_ = false;
        recognizeNextRegion();
    });
}
void Editor::recognizeNextRegion() {
    if (!hasDocument() || (regionOcr_ && regionOcr_->busy())) return;
    for (auto &note : doc_.notes) {
        // OCR belongs to the selected screenshot content. Rearranging that
        // content must preserve both saved text and an in-flight first result.
        if (note.isGlobal || !note.recognizedText.isEmpty() || recognizedRegions_.contains(note.id)) continue;
        const auto key = recognitionKey(note);
        recognizedRegions_.insert(note.id, key);
        QImage picture;
        try { picture = noteRecognitionImage(doc_, note); }
        catch (const std::exception &) { continue; }
        if (picture.isNull()) continue;
        if (!regionOcr_) { regionOcr_ = new OcrEngine(this); regionOcr_->setObjectName("regionTextRecognizer"); }
        const QString id = note.id, documentId = doc_.id;
        const int generation = recognitionGeneration_;
        QPointer<Editor> self(this);
        regionOcr_->recognize(picture, preferences_.ocrLanguage,
            [self, generation, documentId, id, key](OcrResult result) {
                if (!self || generation != self->recognitionGeneration_ || documentId != self->doc_.id) return;
                for (auto &current : self->doc_.notes) {
                    if (current.id != id || current.recognizedText.value("edited") == true ||
                        !self->recognizedRegions_.contains(id) || self->recognizedRegions_.value(id) != key) continue;
                    QString text = result.text().trimmed().left(500);
                    if (!text.isEmpty() && text.back().isHighSurrogate()) text.chop(1);
                    if (result.ok && !text.isEmpty()) {
#ifdef Q_OS_WIN
                        const QString engine = "windows-ocr";
#elif defined(Q_OS_APPLE)
                        const QString engine = "apple-vision";
#else
                        const QString engine = "tesseract";
#endif
                        current.recognizedText = {{"text", text}, {"engine", engine}};
                        if (!result.engineLanguage.isEmpty()) current.recognizedText.insert("language", result.engineLanguage.left(100));
                        self->updateRecognizedText(id, current.recognizedText);
                        // An OCR completion must survive cancelling a concurrent comment draft.
                        for (auto &baseline : self->editingBaseline_.notes)
                            if (baseline.id == id && baseline.recognizedText.value("edited") != true) baseline.recognizedText = current.recognizedText;
                        if (!self->editingId_.isEmpty()) self->editingWasDirty_ = true;
                        self->changed();
                    }
                    break;
                }
                self->scheduleRegionRecognition();
            });
        renderNotes();
        return;
    }
}
void Editor::addGlobalNote() {
    if (!ensureVideoAnnotationFrame()) return;
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
    annotationsVisible_ = !annotationsVisible_;
    updateAnnotationVisibility();
}
void Editor::updateAnnotationVisibility() {
    hideAnnotations_->setChecked(!annotationsVisible_);
    hideAnnotations_->setToolTip(annotationsVisible_ ? tr("隐藏画面批注") : tr("显示画面批注"));
    hideAnnotations_->setAccessibleName(hideAnnotations_->toolTip());
    hideAnnotations_->setProperty("glyphName", annotationsVisible_ ? "eye" : "eye-off");
    hideAnnotations_->setIcon(glyph(annotationsVisible_ ? "eye" : "eye-off"));
    canvas_->setAnnotationsVisible(annotationsVisible_);
    if (layoutCanvas_) layoutCanvas_->setAnnotationsVisible(annotationsVisible_);
}
bool Editor::eventFilter(QObject *object, QEvent *event) {
    if (auto owner = qobject_cast<QWidget *>(object); owner && owner->window() != this)
        return QWidget::eventFilter(object, event);
    // An unresolved note shows a compact "归属待确认" chip that turns into the
    // actual question while the pointer is over it.
    if ((event->type() == QEvent::Enter || event->type() == QEvent::Leave) &&
        object->objectName() == "parentPrompt") {
        if (auto text = object->findChild<QLabel *>("parentPromptText"))
            text->setText(event->type() == QEvent::Enter
                              ? tr("这个框是指大爆炸移动后的内容吗？")
                              : tr("归属待确认"));
    }
    if (object == canvas_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
        syncVideoGeometry();
    if (object == canvas_ && videoPreview_ && event->type() == QEvent::MouseButtonPress) {
        const auto mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && mouse->buttons() == Qt::LeftButton &&
            !(mouse->modifiers() & Qt::AltModifier) &&
            canvas_->mode() != Canvas::Adjust && !ensureVideoAnnotationFrame()) return true;
    }
    if (object == canvas_ && video_ && (videoPlayback_->playing() || videoPlayback_->positioning()) &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseButtonDblClick) &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
        return true;
    if (video_ && event->type() == QEvent::KeyPress) {
        auto owner = qobject_cast<QWidget *>(object);
        const auto key = static_cast<QKeyEvent *>(event);
        if (owner && owner->window() == this && key->key() == Qt::Key_Space &&
            key->modifiers() == Qt::NoModifier && !qobject_cast<QPlainTextEdit *>(owner) &&
            !qobject_cast<QTextEdit *>(owner) && !qobject_cast<QLineEdit *>(owner)) {
            if (!key->isAutoRepeat()) { finishNoteEdit(); videoPlayback_->toggle(); }
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        auto card = qobject_cast<QWidget *>(object);
        while (card && card->objectName() != "noteCard") {
            // Let buttons and text editors receive their own mouse events.
            // The card shortcut must not consume a delete press or text selection.
            if (qobject_cast<QAbstractButton *>(card) || qobject_cast<QPlainTextEdit *>(card)) {
                card = nullptr;
                break;
            }
            card = card->parentWidget();
        }
        if (card && card->property("movementSource").isValid()) {
            const auto source = card->property("movementSource").toRectF();
            const auto destination = card->property("movementDestination").toRectF();
            // Creating a text note rebuilds the orphan card; finish this mouse event first.
            QTimer::singleShot(0, this, [this, source, destination] { editMovement(source, destination); });
            return true;
        }
    }
    if (object == imageScroll_->viewport()) {
        // The viewport settles at its real size after the window does, so the layers
        // pinned to it are synced here rather than only in resizeEvent, where the
        // viewport rect can still be the old one — the empty-window hint once kept
        // its construction-time size and its button showed as a sliver in the corner.
        if (event->type() == QEvent::Resize) {
            const QSize area = static_cast<QResizeEvent *>(event)->size();
            if (wave_)
                wave_->resize(area);
            if (emptyWell_)
                emptyWell_->resize(area);
        }
        if (event->type() == QEvent::MouseButtonPress) {
            const auto mouse = static_cast<QMouseEvent *>(event);
            if (!canvas_->imageSize().isEmpty() && mouse->button() == Qt::MiddleButton &&
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
    // The empty-window hint covers the whole viewport, so a picture aimed at the
    // middle of the window lands on it instead of on the window. It does not know how
    // to open a picture, so the drop is handed straight back to the window.
    if (object == emptyWell_) {
        if (event->type() == QEvent::DragEnter) {
            dragEnterEvent(static_cast<QDragEnterEvent *>(event));
            return true;
        }
        if (event->type() == QEvent::DragMove) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent *>(event));
            return true;
        }
    }
    if (object==imageScroll_->viewport() && event->type()==QEvent::Wheel && !canvas_->imageSize().isEmpty()) {
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
QPushButton *Editor::makeCopyJsonTextOptions(QWidget *parent, const QString &name) {
    auto button = textButton({}, false, parent);
    button->setIcon(QIcon(glyph("more", QColor("#80848e")).pixmap(36, 36)
                              .transformed(QTransform().rotate(90))));
    button->setIconSize(QSize(18, 18));
    button->setObjectName(name);
    button->setFixedSize(24, 34);
    button->setStyleSheet("padding:0; min-width:0; min-height:0;");
    button->setToolTip(tr("复制 JSON 内容选项"));
    button->setAccessibleName(tr("复制 JSON 内容选项"));
    auto menu = new QMenu(button);
    menu->setObjectName(name + "Menu");
    auto group = new QActionGroup(menu);
    auto withoutImage = menu->addAction(tr("不携带原图信息"));
    auto withImage = menu->addAction(tr("携带原图信息（嵌入 JSON）"));
    withoutImage->setObjectName("copyJsonTextWithoutImage");
    withImage->setObjectName("copyJsonTextWithImage");
    for (auto action : {withoutImage, withImage}) {
        action->setCheckable(true);
        group->addAction(action);
    }
    auto update = [this, withoutImage, withImage] {
        withoutImage->setText(tr("不携带原图信息"));
        withImage->setText(tr("携带原图信息（嵌入 JSON）"));
        withoutImage->setChecked(!preferences_.copyJsonTextWithImage);
        withImage->setChecked(preferences_.copyJsonTextWithImage);
    };
    update();
    connect(menu, &QMenu::aboutToShow, menu, update);
    connect(withoutImage, &QAction::triggered, this, [this] { setCopyJsonTextWithImage(false); });
    connect(withImage, &QAction::triggered, this, [this] { setCopyJsonTextWithImage(true); });
    connect(button, &QPushButton::clicked, menu, [button, menu] {
        menu->popup(button->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
    });
    return button;
}
void Editor::setCopyJsonTextWithImage(bool enabled) {
    if (preferences_.copyJsonTextWithImage == enabled) return;
    preferences_.copyJsonTextWithImage = enabled;
    emit exportPreferencesChanged(preferences_.imageExportMode, enabled);
    updateToolbar();
}
void Editor::copyJsonText() {
    finishNoteEdit();
    if (!hasDocument() && !video_) return;
    try {
        FeedbackExportOptions options;
        options.mode = preferences_.copyJsonTextWithImage ? ImageExportMode::Embedded : ImageExportMode::None;
        options.preview = false; options.compress = true;
        const auto bundle = agentFeedbackBundle(options);
        QApplication::clipboard()->setText(QString::fromUtf8(bundle.json));
        toast(preferences_.copyJsonTextWithImage ? tr("JSON 文本已复制（包含原图编码）") : tr("JSON 文本已复制（不含原图编码）"));
    } catch (const std::exception &error) { showError(QString::fromUtf8(error.what())); }
}
void Editor::showJsonHelp() {
    QMessageBox help(this);
    help.setObjectName("jsonHelpDialog");
    help.setWindowTitle(tr("无法使用 JSON？"));
    help.setTextFormat(Qt::RichText);
    help.setText(tr(
        "<h3>1. 发送 JSON 文本被截断，又无法发送 JSON 文件？</h3>"
        "<p>在“复制 JSON 内容”旁的选项中选择“不携带原图信息”，再复制 JSON 内容，并向 Agent 一起上传原图。</p>"
        "<h3>2. 都不支持上传，只支持对话怎么办？</h3>"
        "<p>可以尝试发送不包含原图信息的 JSON 文本。但由于没有附带原图，暂时无法确认效果。</p>"
        "<h3>3. AI 没认出来原图信息怎么办？</h3>"
        "<p>可以在提示词中加一句：“参考 JSON 文件中的图片信息……”；如果仍失败，建议使用第一种方式。</p>"));
    help.setStandardButtons(QMessageBox::Ok);
    help.exec();
}
void Editor::copyJson() {
    finishNoteEdit();
    if (!hasDocument() && !video_) return;
    try {
        FeedbackExportOptions options;
        options.mode = preferences_.imageExportMode == ImageExportMode::Embedded ? ImageExportMode::Embedded : ImageExportMode::File;
        options.preview = false; options.compress = true;
        const auto bundle = agentFeedbackBundle(options);
        const QString path = writeFeedbackTempBundle(bundle, preferences_.feedbackDir);
        cleanupOldFeedbackTempFiles(path, preferences_.feedbackDir);
        auto mime = new QMimeData;
        mime->setUrls(feedbackBundleUrls(path, bundle));
        QApplication::clipboard()->setMimeData(mime);
        toast(tr("JSON 与配套图片文件已复制"));
    } catch(const std::exception &error) { showError(QString::fromUtf8(error.what())); }
}
void Editor::changed(bool contentChanged) {
    normalizeNoteParents(doc_);
    if (contentChanged)
        doc_.dirty = true;
    if (video_) { if (contentChanged) video_->dirty = true; commitVideoFrame(); }
    canvas_->refresh();
    if (layoutCanvas_)
        layoutCanvas_->setAnnotations(doc_.notes);
    renderNotes();
    updateControls();
    scheduleRegionRecognition();
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
        hint_->setText(doc_.layout ? tr("在调整后的画面批注 · 滚轮切换范围 · 单击或拖动框选")
                                   : tr("滚轮切换范围 · 单击批注 · 拖动框选"));
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
        hint_->setText(tr("悬停滚轮选范围 · 拖边改宽高 · 拖角等比 · 点标注或框选可添加意见"));
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
    if (enabled && !ensureVideoAnnotationFrame()) return;
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
                auto orderedNotes = notes;
                auto markers = movementMarkers(layoutCanvas_->state(), orderedNotes);
                // A movement without text is the movement itself, so it follows the
                // block: putting the block back leaves nothing to annotate.
                QVector<QRectF> movedSources;
                for (const auto &marker : markers)
                    movedSources.append(marker.source);
                orderedNotes.removeIf([&](const Note &note) {
                    return note.movementSource && note.comment.trimmed().isEmpty() &&
                           !movedSources.contains(*note.movementSource);
                });
                // A movement is an annotation as soon as it is made. Keep its place
                // among later text notes even if no comment is ever entered.
                markers = movementMarkers(layoutCanvas_->state(), orderedNotes);
                for (const auto &marker : markers) {
                    if (marker.noteIndex >= 0 || orderedNotes.size() >= MaxNotes)
                        continue;
                    Note note;
                    note.isPoint = false;
                    note.rect = marker.destination.toAlignedRect();
                    note.movementSource = marker.source;
                    orderedNotes.append(note);
                }
                // Dragging a component home removes the movement it belonged to.
                const bool notesChanged = doc_.notes != orderedNotes;
                doc_.notes = std::move(orderedNotes);
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
    if (!ensureVideoAnnotationFrame())
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
    deleteNote(id);
}
void Editor::deleteNote(const QString &id, std::optional<QRectF> movementSource) {
    finishNoteEdit();
    remember();
    if (movementSource) {
        Note note; note.id = id; note.isPoint = false; note.movementSource = movementSource;
        note.rect = movementSource->toAlignedRect(); doc_.notes.append(note);
    }
    removeNote(doc_, id);
    if (layoutCanvas_ && doc_.layout) layoutCanvas_->setState(*doc_.layout);
    canvas_->select({});
    changed();
    updateLayoutControls();
}
void Editor::copyImage() {
    finishNoteEdit();
    if (videoPreview_ && !ensureVideoAnnotationFrame()) return;
    if (hasDocument()) {
        QApplication::clipboard()->setImage(previewImage(doc_));
        toast(tr("带批注图片已复制"));
    }
}
void Editor::showError(const QString &text) {
    diagnostics::write(diagnostics::Level::Error, "editor.operation", text,
        {{"media", video_ ? "video" : "image"}, {"width", doc_.image.width()}, {"height", doc_.image.height()}});
    QMessageBox::warning(this, "EditHere", text);
}
bool Editor::saveProject() {
    finishNoteEdit();
    if (!hasDocument() && !video_)
        return true;
    QString path = QFileDialog::getSaveFileName(this, tr("保存 EditHere 项目"),
                                                projectPath_.isEmpty() ? tr("设计反馈.edithere") : projectPath_,
                                                tr("EditHere 项目 (*.edithere)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".edithere", Qt::CaseInsensitive))
        path += ".edithere";
    try {
        const auto bytes = projectBytes();
        saveBytes(path, bytes);
        projectPath_ = path;
        doc_.dirty = false;
        if (video_) { video_->dirty = false; for (auto &frame : video_->frames) frame.document.dirty = false; }
        QString associationError;
        if (!QStandardPaths::isTestModeEnabled())
            registerProjectFileAssociation(&associationError);
        if (!associationError.isEmpty())
            diagnostics::write(diagnostics::Level::Warning, "project.file_association", associationError);
        diagnostics::write(diagnostics::Level::Info, "project.save", "Project saved",
                           {{"media", video_ ? "video" : "image"}, {"bytes", double(bytes.size())}});
        toast(associationError.isEmpty() ? tr("项目已保存，可双击继续编辑") : tr("项目已保存，可从 EditHere 导入继续编辑"));
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
    return doc_.dirty || (video_ && video_->dirty) || (hasDocument() && projectPath_.isEmpty() &&
                          (doc_.source == "screen" || doc_.source == "clipboard"));
}
QByteArray Editor::agentFeedback(bool embed) {
    finishNoteEdit();
    return feedbackBytes(embed);
}
FeedbackBundle Editor::agentFeedbackBundle(FeedbackExportOptions options) {
    finishNoteEdit(); commitVideoFrame();
    // The bundle contains every annotated frame. Reveal the unresolved frame
    // before reporting the same validation error used by all export boundaries.
    auto revealUnresolved = [this](const Document &document, qint64 frameUs) {
        auto normalized = document;
        normalizeNoteParents(normalized);
        for (const auto &note : normalized.notes) {
            if (!note.parent || note.parent->state != ParentState::Unknown) continue;
            if (video_ && frameUs != videoFrameUs_) reviewVideoFrame(frameUs);
            focusNote(note.id);
            requireConfirmedNoteParents(normalized);
        }
    };
    if (video_) {
        // Copy IDs/documents: revealing a frame commits the old one and can
        // invalidate references into the project's frame vector.
        const auto frames = video_->frames;
        for (const auto &frame : frames) revealUnresolved(frame.document, frame.timestampUs);
    } else revealUnresolved(doc_, -1);
    options.includeRecognizedText = preferences_.includeRecognizedText;
    options.includeWindowTitle = preferences_.includeWindowTitle;
    return video_ ? buildVideoFeedbackBundle(*video_, options) : buildFeedbackBundle(doc_, options);
}

bool Editor::allowReplace() {
    if (agentSession_) {
        toast(tr("请先完成或取消当前 AI 批注任务"));
        return false;
    }
    finishNoteEdit();
    if (!doc_.dirty && (!video_ || !video_->dirty))
        return true;
    auto answer = QMessageBox::question(
        this, tr("保留当前修改？"), tr("当前批注或布局修改尚未保存。是否先保存项目？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveProject();
}
void Editor::openEmpty() {
    if (agentSession_) {
        toast(tr("请先完成或取消当前 AI 批注任务"));
        return;
    }
    if (!allowReplace())
        return;
    ++generation_;
    cancelRegionRecognition();
    clearVideo();
    stopViewportPan();
    resetLayoutTools();
    doc_ = {};
    undoHistory_.clear();
    redoHistory_.clear();
    projectPath_.clear();
    canvas_->setDocument(nullptr);
    meta_->setText({});
    renderNotes();
    updateControls();
    updateEmptyState();
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    configureNativeWindow(this, false);
    raise();
    activateWindow();
}
void Editor::updateEmptyState() {
    // An empty window that says nothing looks broken, and one that says what it is
    // waiting for is the whole feature: the picture comes from somewhere else.
    if (emptyWell_ == nullptr)
        return;
    // Synced here as well, so a window shown at the size it was created with still
    // covers the whole viewport on its first appearance.
    emptyWell_->setGeometry(imageScroll_->viewport()->rect());
    const bool empty = !hasDocument() && !video_ && !guideActive();
    emptyWell_->setVisible(empty);
    emptyWell_->raise();
    // The viewport can still be settling: on some machines the first layout pass lands
    // after this call, so the size is taken once more once everything has been shown.
    if (empty)
        QTimer::singleShot(0, this, [this] {
            if (emptyWell_ && emptyWell_->isVisible())
                emptyWell_->setGeometry(imageScroll_->viewport()->rect());
        });
    // Nothing has been opened yet, so there is no picture-shaped panel either: the
    // canvas keeps its background for a picture and loses it while waiting for one.
    // The flag goes through a property and a re-polish rather than an inline
    // stylesheet, which would cascade down onto the hint and its button.
    if (imageScroll_->property("empty").toBool() != empty) {
        imageScroll_->setProperty("empty", empty ? QVariant(true) : QVariant(false));
        if (auto *style = imageScroll_->style()) {
            style->unpolish(imageScroll_);
            style->polish(imageScroll_);
        }
    }
}
bool Editor::confirmDiscardOnClose() {
    finishNoteEdit();
    if (!doc_.dirty && (!video_ || !video_->dirty))
        return true;
    // What the box said the last time it was shown: from then on closing throws the
    // changes away. Saving is still one button away while the window is open, so
    // nothing is lost that the user could have wanted to keep.
    if (!preferences_.confirmBeforeDiscard)
        return true;
    QMessageBox box(this);
    box.setWindowTitle(tr("保留当前修改？"));
    box.setText(tr("当前批注或布局修改尚未保存。是否先保存项目？"));
    box.setIcon(QMessageBox::Question);
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    auto *never = new QCheckBox(tr("不再提醒，可在设置中修改"), &box);
    box.setCheckBox(never);
    const auto answer = box.exec();
    if (never->isChecked()) {
        preferences_.confirmBeforeDiscard = false;
        emit preferencesChanged();
    }
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveProject();
}
void Editor::openFile(const QString &provided) {
    QString path = provided;
    if (path.isEmpty())
        path = QFileDialog::getOpenFileName(this, tr("打开图片、视频或项目"), {},
            tr("图片、视频或项目 (*.png *.jpg *.jpeg *.webp *.bmp *.mp4 *.mov *.mkv *.webm *.avi *.m4v *.json *.edithere);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    try {
        if (!allowReplace())
            return;
        loadMedia(path);
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::pasteImage() {
    QImage image = QApplication::clipboard()->image();
    if (image.isNull())
        return;
    try {
        auto doc = fromImage(image, "clipboard", tr("剪贴板图片"));
        if (allowReplace())
            setDocument(std::move(doc));
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::saveImage(bool annotated) {
    finishNoteEdit();
    if (videoPreview_ && !ensureVideoAnnotationFrame()) return;
    if (!hasDocument())
        return;
    const bool layoutPreview = !annotated && doc_.layout.has_value();
    QString path = QFileDialog::getSaveFileName(this,
                                                annotated       ? tr("保存带批注图片")
                                                : layoutPreview ? tr("保存调整效果")
                                                                : tr("保存原图"),
                                                annotated       ? "preview.png"
                                                : layoutPreview ? "layout.png"
                                                                : doc_.imageFile,
                                                tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    try {
        saveBytes(path, annotated       ? encodePng(previewImage(doc_))
                        : layoutPreview ? encodePng(renderLayout(doc_.image, *doc_.layout))
                                        : doc_.png);
        hint_->setText(tr("图片已保存"));
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
    }
}
void Editor::exportJson() {
    finishNoteEdit();
    if (!hasDocument() && !video_) return;
    QDialog dialog(this);
    dialog.setObjectName("feedbackDialog"); dialog.setWindowTitle(tr("查看 JSON")); dialog.resize(730, 640);
    auto layout = new QVBoxLayout(&dialog); layout->setContentsMargins(24, 22, 24, 22);
    auto title = new QLabel(tr("查看 JSON  ·  批注 %1 条").arg(totalAnnotationCount()), &dialog);
    QFont font = title->font(); font.setPointSize(15); font.setBold(true); title->setFont(font); layout->addWidget(title);
    auto embedded = new QCheckBox(tr("原图嵌入 JSON"), &dialog);
    embedded->setObjectName("feedbackEmbedImage");
    embedded->setChecked(preferences_.imageExportMode == ImageExportMode::Embedded);
    embedded->setToolTip(tr("未勾选时将 JSON 与原图作为独立文件复制。"));
    layout->addWidget(embedded);
    auto compress = new QCheckBox(tr("压缩内嵌原图（保持尺寸，可能轻微损失细节）"), &dialog);
    compress->setObjectName("compressFeedbackImage"); compress->setChecked(true); layout->addWidget(compress);
    auto json = new JsonPreview(&dialog); json->setAccessibleName(tr("标准化 JSON")); json->setReadOnly(true);
    json->setLineWrapMode(QPlainTextEdit::WidgetWidth); json->setFont(QFont(h2d::monoFontFamily(), 10)); layout->addWidget(json, 1);
    auto status = mutedLabel({}, &dialog); status->setObjectName("exportStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto ai = textButton(tr("生成修改意见…"), false, &dialog);
    ai->setObjectName("generateInstructions");
    layout->addWidget(ai);
    connect(ai, &QPushButton::clicked, &dialog, [&] {
        dialog.accept(); QTimer::singleShot(0, this, &Editor::generateInstructions);
    });
    auto row = new QHBoxLayout;
    auto close = textButton(tr("关闭"), false, &dialog),
         copy = textButton(tr("复制 JSON 与图片"), true, &dialog), copyText = textButton(tr("复制 JSON 内容"), false, &dialog);
    copy->setObjectName("copyJsonFile"); copyText->setObjectName("copyJsonText");
    close->setProperty("glyphName", "close"); close->setIcon(glyph("close"));
    copy->setProperty("glyphName", "json-copy-file"); copy->setIcon(glyph("json-copy-file", Qt::white));
    copyText->setProperty("glyphName", "json-copy-text"); copyText->setIcon(glyph("json-copy-text"));
    auto textOptions = makeCopyJsonTextOptions(&dialog, "copyJsonTextOptions");
    row->addStretch(); row->addWidget(close); row->addWidget(copyText); row->addWidget(textOptions); row->addWidget(copy); layout->addLayout(row);
    FeedbackBundle bundle;
    auto refresh = [&] {
        try {
            FeedbackExportOptions options;
            options.mode = embedded->isChecked() ? ImageExportMode::Embedded : ImageExportMode::File;
            options.preview = false; options.compress = compress->isChecked();
            compress->setEnabled(embedded->isChecked());
            bundle = agentFeedbackBundle(options);
            auto readable = QJsonDocument::fromJson(bundle.json).object();
            auto fold = [&](QJsonObject feedback) {
                auto image = feedback["image"].toObject();
                if (image.contains("dataUrl")) image["dataUrl"] = image["dataUrl"].toString().section(',', 0, 0) + tr(",[图片编码已折叠]");
                feedback["image"] = image; return feedback;
            };
            if (video_) {
                auto frames = readable["frames"].toArray();
                for (qsizetype index = 0; index < frames.size(); ++index) { auto frame = frames[index].toObject(); frame["feedback"] = fold(frame["feedback"].toObject()); frames[index] = frame; }
                readable["frames"] = frames;
            } else readable = fold(readable);
            json->setPlainText(QString::fromUtf8(formatFeedbackJson(readable, true)));
            copy->setEnabled(true); copyText->setEnabled(true); textOptions->setEnabled(true);
            status->setText(options.mode == ImageExportMode::File ? tr("原图作为配套文件保存；请将 JSON 与图片一起交给 Agent。")
                : tr("图片编码仅在预览中折叠，复制文件包含完整原图。"));
        } catch (const std::exception &error) {
            bundle = {}; json->clear(); copy->setEnabled(false); copyText->setEnabled(false); textOptions->setEnabled(false);
            status->setText(QString::fromUtf8(error.what()));
        }
        copyText->setToolTip(preferences_.copyJsonTextWithImage
            ? tr("复制 JSON 文本（携带原图信息），适用于无法发送文件的 Agent。")
            : tr("复制 JSON 文本（默认不携带原图信息），适用于无法发送文件的 Agent。"));
    };
    auto copyTextContent = [&] {
        if (bundle.json.isEmpty()) return;
        try {
            FeedbackExportOptions options;
            options.mode = preferences_.copyJsonTextWithImage ? ImageExportMode::Embedded : ImageExportMode::None;
            options.preview = false; options.compress = compress->isChecked();
            const auto textBundle = agentFeedbackBundle(options);
            QApplication::clipboard()->setText(QString::fromUtf8(textBundle.json));
            status->setText(preferences_.copyJsonTextWithImage ? tr("JSON 文本已复制（包含原图编码）") : tr("JSON 文本已复制（不含原图编码）"));
        } catch (const std::exception &error) { status->setText(QString::fromUtf8(error.what())); }
    };
    json->copyJson = copyTextContent;
    connect(embedded, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        preferences_.imageExportMode = enabled ? ImageExportMode::Embedded : ImageExportMode::File;
        emit exportPreferencesChanged(preferences_.imageExportMode, preferences_.copyJsonTextWithImage);
    });
    connect(compress, &QCheckBox::toggled, &dialog, refresh);
    connect(this, &Editor::exportPreferencesChanged, &dialog, refresh);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(copyText, &QPushButton::clicked, &dialog, copyTextContent);
    connect(copy, &QPushButton::clicked, &dialog, [&] {
        if (bundle.json.isEmpty()) return;
        try {
            const QString path = writeFeedbackTempBundle(bundle, preferences_.feedbackDir); cleanupOldFeedbackTempFiles(path, preferences_.feedbackDir);
            auto mime = new QMimeData; mime->setUrls(feedbackBundleUrls(path, bundle)); QApplication::clipboard()->setMimeData(mime);
            status->setText(tr("JSON 与配套图片文件已复制"));
        } catch (const std::exception &error) { status->setText(QString::fromUtf8(error.what())); }
    });
    refresh(); dialog.exec();
}
void Editor::generateInstructions() {
    if (!hasDocument() && !video_) return;
    finishNoteEdit(); commitVideoFrame();
    AiFeedbackInput input;
    try {
        FeedbackExportOptions options;
        options.mode = ImageExportMode::None;
        // Use the normal export boundary to reveal any unresolved child parent.
        agentFeedbackBundle(options);
        options.includeRecognizedText = preferences_.includeRecognizedText;
        options.includeWindowTitle = preferences_.includeWindowTitle;
        input = video_ ? aiFeedbackSnapshot(*video_, options) : aiFeedbackSnapshot(doc_, options);
    } catch (const std::exception &error) {
        showError(QString::fromUtf8(error.what())); return;
    }
    for (;;) {
        int result;
        {
            AiInstructionsDialog dialog(preferences_, input, this);
            result = dialog.exec();
        }
        if (result != AiInstructionsDialog::Configure) break;
        emit aiSettingsRequested();
    }
}
void Editor::showContext(QPoint p) {
    finishNoteEdit();
    QMenu menu(this);
    auto settings = menu.addAction(glyph("settings"), tr("设置…"), this, &Editor::settingsRequested);
    settings->setObjectName("editorSettings");
    menu.addAction(glyph("layout-settings"),tr("自定义工具栏…"),this,&Editor::toolbarSettingsRequested);
    auto ai = menu.addAction(glyph("notes"), tr("生成修改意见…"), this, &Editor::generateInstructions);
    ai->setObjectName("generateInstructions");
    ai->setEnabled(hasDocument() || video_.has_value());
    bool hiddenActions=false;
    for (const auto &definition : toolbarActionDefinitions()) {
        auto button=outputButtons_.value(definition.id);
        if (!button || !button->isHidden()) continue;
        if (!hiddenActions) { menu.addSeparator(); hiddenActions=true; }
        auto action=menu.addAction(glyph(toolbarGlyph(definition.id)),definition.label,button,&QPushButton::click);
        action->setObjectName("more_"+definition.id);
        action->setEnabled(button->isEnabled());
        if (definition.id == "copyJsonText") {
            auto options = menu.addMenu(tr("复制 JSON 内容选项"));
            options->setObjectName("moreCopyJsonTextOptions");
            options->setEnabled(button->isEnabled());
            auto group = new QActionGroup(options);
            auto withoutImage = options->addAction(tr("不携带原图信息"));
            auto withImage = options->addAction(tr("携带原图信息（嵌入 JSON）"));
            withoutImage->setObjectName("moreCopyJsonTextWithoutImage");
            withImage->setObjectName("moreCopyJsonTextWithImage");
            for (auto choice : {withoutImage, withImage}) { choice->setCheckable(true); group->addAction(choice); }
            withoutImage->setChecked(!preferences_.copyJsonTextWithImage);
            withImage->setChecked(preferences_.copyJsonTextWithImage);
            connect(withoutImage, &QAction::triggered, this, [this] { setCopyJsonTextWithImage(false); });
            connect(withImage, &QAction::triggered, this, [this] { setCopyJsonTextWithImage(true); });
        }
    }
    menu.addSeparator();
    menu.addAction(glyph("open"),tr("导入图片、视频或项目"),this,[this] { openFile(); });
    menu.addAction(glyph("close"),tr("关闭当前截图"),this,&QWidget::close);
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
    if (!confirmDiscardOnClose())
        return;
    ++generation_;
    cancelRegionRecognition();
    clearVideo();
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
    // A file being dragged in is the ordinary case; an image coming straight from
    // another program is the other one, and it is worth taking because that is what
    // "drag this picture over to annotate it" means when the picture is not a file.
    if (e->mimeData()->hasUrls() || e->mimeData()->hasImage())
        e->acceptProposedAction();
}
void Editor::dropEvent(QDropEvent *e) {
    if (!e->mimeData()->hasUrls() && e->mimeData()->hasImage()) {
        if (agentSession_) {
            toast(tr("请先完成或取消当前 AI 批注任务"));
            return;
        }
        if (!allowReplace())
            return;
        try {
            setDocument(fromImage(qvariant_cast<QImage>(e->mimeData()->imageData()), "drop",
                                  tr("拖入的图片")));
            e->acceptProposedAction();
        } catch (const std::exception &error) {
            e->ignore();
            showError(QString::fromUtf8(error.what()));
        }
        return;
    }
    const auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        openFile(urls.first().toLocalFile());
        e->acceptProposedAction();
    }
}
} // namespace h2d
