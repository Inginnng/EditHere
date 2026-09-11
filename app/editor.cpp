#include "editor.h"
#include "detector.h"
#include "explosion.h"
#include "platform.h"
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
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
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
    setWindowTitle("HelpDesign");
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
    auto brand = new QLabel("HelpDesign", bar);
    brand->setObjectName("brand");
    header->addWidget(brand);
    meta_ = mutedLabel({}, bar);
    header->addSpacing(10);
    header->addWidget(meta_);
    header->addStretch();
    explosion_ = textButton("大爆炸", false, bar);
    explosion_->setObjectName("explodeButton");
    explosion_->setEnabled(false);
    explosion_->setCheckable(true);
    header->addWidget(explosion_);
    connect(explosion_, &QPushButton::clicked, this, &Editor::explode);
    auto capture = iconButton("capture", "重新截图", bar);
    auto open = iconButton("save", "打开图片或项目", bar);
    auto close = iconButton("close", "关闭当前截图", bar);
    header->addWidget(open);
    header->addWidget(capture);
    header->addWidget(close);
    connect(capture, &QPushButton::clicked, this, &Editor::captureRequested);
    connect(open, &QPushButton::clicked, this, [this] { openFile(); });
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(bar);
    auto content = new QHBoxLayout;
    content->setSpacing(0);
    content->setContentsMargins(0, 0, 0, 0);
    imageScroll_ = new QScrollArea(shell);
    imageScroll_->setObjectName("imageWell");
    imageScroll_->setAlignment(Qt::AlignCenter);
    imageScroll_->setWidgetResizable(false);
    canvas_ = new Canvas;
    imageScroll_->setWidget(canvas_);
    content->addWidget(imageScroll_, 1);
    notesPanel_ = new QWidget(shell);
    notesPanel_->setObjectName("notesPanel");
    notesPanel_->setFixedWidth(280);
    auto side = new QVBoxLayout(notesPanel_);
    side->setContentsMargins(12, 12, 12, 6);
    noteCount_ = new QLabel("批注", notesPanel_);
    side->addWidget(noteCount_);
    notesScroll_ = new QScrollArea(notesPanel_);
    notesScroll_->setWidgetResizable(true);
    noteContainer_ = new QWidget;
    noteContainer_->setObjectName("noteContainer");
    noteLayout_ = new QVBoxLayout(noteContainer_);
    noteLayout_->setContentsMargins(0, 8, 0, 0);
    noteLayout_->setSpacing(10);
    noteLayout_->setAlignment(Qt::AlignTop);
    notesScroll_->setWidget(noteContainer_);
    side->addWidget(notesScroll_);
    detailsStack_ = new QStackedWidget(shell);
    detailsStack_->setObjectName("detailsStack");
    detailsStack_->setFixedWidth(280);
    detailsStack_->addWidget(notesPanel_);
    content->addWidget(detailsStack_);
    wave_ = new ExplosionWave(imageScroll_->viewport());
    wave_->setGeometry(imageScroll_->viewport()->rect());
    layout->addLayout(content, 1);
    hint_ = mutedLabel("滚轮 ↑ 更大 ↓ 更小 · 单击批注 · 拖动框选", shell);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setFixedHeight(26);
    layout->addWidget(hint_);
    auto dock = new QHBoxLayout;
    dock->setContentsMargins(12, 3, 12, 10);
    dock->setSpacing(4);
    dock->addStretch();
    componentTool_ = textButton("移动组件", false, shell);
    componentTool_->setObjectName("componentTool");
    componentTool_->setCheckable(true);
    componentTool_->hide();
    dock->addWidget(componentTool_);
    connect(componentTool_, &QPushButton::clicked, this, [this] { setComponentEditing(true); });
    QStringList names{"smart", "point", "rect", "select"},
        labels{"智能选块 (B)", "点标注 (P)", "框选 (R)", "调整批注 (V)"};
    for (int i = 0; i < 4; i++) {
        auto button = iconButton(names[i], labels[i], shell);
        button->setCheckable(true);
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
    notesToggle_ = iconButton("notes", "显示或隐藏批注", shell);
    notesToggle_->setCheckable(true);
    dock->addWidget(notesToggle_);
    connect(notesToggle_, &QPushButton::clicked, this, [this] {
        detailsStack_->setVisible(!detailsStack_->isVisible());
        if (fitted_)
            QTimer::singleShot(0, this, &Editor::fit);
        updateControls();
    });
    auto copy = iconButton("copy", "复制图片", shell), more = iconButton("more", "更多操作", shell);
    dock->addWidget(copy);
    dock->addWidget(more);
    connect(copy, &QPushButton::clicked, this, &Editor::copyImage);
    connect(more, &QPushButton::clicked, this,
            [this, more] { showContext(more->mapToGlobal(QPoint(0, 0))); });
    auto exportButton = textButton("导出 JSON", true, shell);
    dock->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, &Editor::exportJson);
    dock->addStretch();
    auto grip = new QSizeGrip(shell);
    dock->addWidget(grip);
    layout->addLayout(dock);
    connect(canvas_, &Canvas::editRequested, this, &Editor::editNote);
    connect(canvas_, &Canvas::hintChanged, hint_, &QLabel::setText);
    connect(canvas_, &Canvas::zoomRequested, this, &Editor::zoom);
    connect(canvas_, &Canvas::contextRequested, this, &Editor::showContext);
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
    shortcut("paste", &Editor::pasteImage);
    shortcut("export", &Editor::exportJson);
    shortcut("fit", &Editor::fit);
    shortcut("delete", &Editor::removeSelected);
    shortcut("close", [this] {
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
    resize(1180, 800);
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
    componentTool_->setToolTip(label("component", "移动组件"));
}
void Editor::setDocument(Document document) {
    resetLayoutTools();
    doc_ = std::move(document);
    projectPath_.clear();
    undoHistory_.clear();
    redoHistory_.clear();
    canvas_->setDocument(&doc_);
    setMode(Canvas::Smart);
    canvas_->setLayoutPreview(doc_.layout.has_value());
    explosion_->setEnabled(doc_.layout.has_value());
    updateLayoutControls();
    detailsStack_->show();
    detailsStack_->setCurrentWidget(notesPanel_);
    meta_->setText(QString("%1 × %2").arg(doc_.image.width()).arg(doc_.image.height()));
    renderNotes();
    show();
    configureNativeWindow(this, false);
    QRect available = QGuiApplication::screenAt(QCursor::pos())
                          ? QGuiApplication::screenAt(QCursor::pos())->availableGeometry()
                          : QGuiApplication::primaryScreen()->availableGeometry();
    resize(std::min(1260, available.width() - 60), std::min(850, available.height() - 80));
    move(available.center() - rect().center());
    QTimer::singleShot(0, this, &Editor::fit);
    const int generation = ++generation_;
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
void Editor::fit() {
    if (!hasDocument())
        return;
    fitted_ = true;
    QSize area = imageScroll_->viewport()->size() - QSize(36, 36);
    canvas_->setZoom(std::min({1.0, double(std::max(80, area.width())) / doc_.image.width(),
                               double(std::max(80, area.height())) / doc_.image.height()}));
    if (layoutCanvas_)
        layoutCanvas_->setZoom(canvas_->zoom());
    zoom_->setText(QString::number(qRound(canvas_->zoom() * 100)) + "%");
}
void Editor::zoom(double value) {
    if (!hasDocument())
        return;
    if (value == 0) {
        fit();
        return;
    }
    fitted_ = false;
    canvas_->setZoom(value);
    if (layoutCanvas_)
        layoutCanvas_->setZoom(canvas_->zoom());
    zoom_->setText(QString::number(qRound(canvas_->zoom() * 100)) + "%");
}
void Editor::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (wave_)
        wave_->setGeometry(imageScroll_->viewport()->rect());
    if (fitted_)
        QTimer::singleShot(0, this, &Editor::fit);
}
void Editor::setMode(Canvas::Mode mode) {
    setComponentEditing(false);
    canvas_->setLayoutPreview(doc_.layout.has_value());
    canvas_->setMode(mode);
    canvas_->setFocus();
    updateLayoutControls();
    updateControls();
}
void Editor::updateControls() {
    for (int i = 0; i < modes_.size(); i++)
        modes_[i]->setChecked(!componentEditing_ && i == canvas_->mode());
    undo_->setEnabled(!undoHistory_.isEmpty());
    redo_->setEnabled(!redoHistory_.isEmpty());
    notesToggle_->setChecked(!detailsStack_->isHidden());
    componentTool_->setVisible(explosionActive_);
    componentTool_->setChecked(componentEditing_);
}
QString Editor::coords(const Note &n) const {
    return n.isPoint ? QString("点 (%1, %2)").arg(n.point.x()).arg(n.point.y())
                     : QString("框 (%1, %2) → (%3, %4)")
                           .arg(n.rect.x())
                           .arg(n.rect.y())
                           .arg(n.rect.x() + n.rect.width())
                           .arg(n.rect.y() + n.rect.height());
}
void Editor::renderNotes() {
    while (auto item = noteLayout_->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    noteCount_->setText(QString("批注  %1").arg(doc_.notes.size()));
    int number = 0;
    for (const auto &n : doc_.notes) {
        auto card = new QFrame(noteContainer_);
        card->setObjectName("noteCard");
        card->setProperty("selected", n.id == canvas_->selected());
        auto l = new QVBoxLayout(card);
        l->setContentsMargins(12, 10, 12, 12);
        auto row = new QHBoxLayout;
        auto badge = new QLabel(QString::number(++number), card);
        badge->setObjectName("badge");
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(24, 24);
        row->addWidget(badge);
        row->addStretch();
        auto edit = iconButton("edit", QString("编辑批注 %1").arg(number), card),
             remove = iconButton("trash", QString("删除批注 %1").arg(number), card);
        edit->setFixedSize(28, 28);
        remove->setFixedSize(28, 28);
        row->addWidget(edit);
        row->addWidget(remove);
        l->addLayout(row);
        auto text = new QLabel(n.comment, card);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->addWidget(text);
        auto position = mutedLabel(coords(n), card);
        position->setWordWrap(true);
        l->addWidget(position);
        noteLayout_->addWidget(card);
        connect(edit, &QPushButton::clicked, this, [this, id = n.id] {
            for (const auto &note : doc_.notes)
                if (note.id == id) {
                    editNote(note, false, QCursor::pos());
                    break;
                }
        });
        connect(remove, &QPushButton::clicked, this, [this, id = n.id] {
            canvas_->select(id);
            removeSelected();
        });
    }
    if (doc_.notes.isEmpty()) {
        auto empty = mutedLabel("点选或画框，留下第一条修改意见。", noteContainer_);
        empty->setWordWrap(true);
        noteLayout_->addWidget(empty);
    }
}
void Editor::editNote(Note note, bool fresh, QPoint global) {
    if (fresh && doc_.notes.size() >= MaxNotes) {
        showError("最多支持 1000 条批注");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(fresh ? "添加批注" : "编辑批注");
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedWidth(380);
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 20, 22, 18);
    auto heading = new QLabel(fresh ? QString("批注 %1").arg(doc_.notes.size() + 1) : "修改批注", &dialog);
    QFont title = heading->font();
    title.setPointSize(title.pointSize() + 3);
    title.setBold(true);
    heading->setFont(title);
    layout->addWidget(heading);
    layout->addWidget(mutedLabel(coords(note), &dialog));
    auto text = new QPlainTextEdit(note.comment, &dialog);
    text->setAccessibleName("修改意见");
    text->setObjectName("commentInput");
    text->setPlaceholderText("希望这里怎么改？");
    text->setFixedHeight(130);
    layout->addWidget(text);
    auto error = mutedLabel({}, &dialog);
    layout->addWidget(error);
    auto actions = new QHBoxLayout;
#ifdef Q_OS_MAC
    actions->addWidget(mutedLabel("⌘ + Enter 保存", &dialog));
#else
    actions->addWidget(mutedLabel("Ctrl + Enter 保存", &dialog));
#endif
    actions->addStretch();
    auto cancel = textButton("取消", false, &dialog), save = textButton("保存", true, &dialog);
    actions->addWidget(cancel);
    actions->addWidget(save);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    auto accept = [&] {
        if (text->toPlainText().trimmed().isEmpty() || text->toPlainText().size() > 10000) {
            error->setText("请输入 1 至 10000 字的修改意见");
            return;
        }
        dialog.accept();
    };
    connect(save, &QPushButton::clicked, &dialog, accept);
    auto key = new QShortcut(QKeySequence("Ctrl+Return"), &dialog);
    connect(key, &QShortcut::activated, &dialog, accept);
    dialog.adjustSize();
    QRect screen = QGuiApplication::screenAt(global) ? QGuiApplication::screenAt(global)->availableGeometry()
                                                     : QGuiApplication::primaryScreen()->availableGeometry();
    dialog.move(std::clamp(global.x() + 18, screen.left() + 12, screen.right() - dialog.width() - 12),
                std::clamp(global.y() + 16, screen.top() + 12, screen.bottom() - dialog.height() - 12));
    text->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;
    note.comment = text->toPlainText().trimmed();
    note.updatedAt = timestamp();
    remember();
    if (fresh)
        doc_.notes.append(note);
    else
        for (auto &n : doc_.notes)
            if (n.id == note.id) {
                n = note;
                break;
            }
    canvas_->select(note.id);
    detailsStack_->show();
    changed();
    if (fitted_)
        QTimer::singleShot(0, this, &Editor::fit);
    (componentEditing_ ? static_cast<QWidget *>(layoutCanvas_) : static_cast<QWidget *>(canvas_))->setFocus();
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
    if (undoHistory_.isEmpty())
        return;
    redoHistory_.append({doc_.notes, doc_.layout});
    restore(undoHistory_.takeLast());
}
void Editor::redo() {
    if (redoHistory_.isEmpty())
        return;
    undoHistory_.append({doc_.notes, doc_.layout});
    restore(redoHistory_.takeLast());
}
void Editor::updateLayoutControls() {
    explosion_->setChecked(explosionActive_);
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
        detailsStack_->setCurrentWidget(inspectorScroll_);
        detailsStack_->show();
        layoutCanvas_->setFocus();
        hint_->setText("悬停滚轮选范围 · 拖边改宽高 · 拖角等比 · 点标注或框选可添加意见");
    } else {
        if (layoutCanvas_)
            layoutCanvas_->cancelInteraction();
        canvas_->setLayoutPreview(doc_.layout.has_value());
        switchCanvas(canvas_);
        detailsStack_->setCurrentWidget(notesPanel_);
        canvas_->setFocus();
    }
    updateLayoutControls();
}
void Editor::switchCanvas(QWidget *target) {
    if (imageScroll_->widget() == target)
        return;
    const int x = imageScroll_->horizontalScrollBar()->value();
    const int y = imageScroll_->verticalScrollBar()->value();
    if (auto previous = imageScroll_->takeWidget()) {
        previous->hide();
        previous->setParent(this);
    }
    imageScroll_->setWidget(target);
    target->show();
    imageScroll_->horizontalScrollBar()->setValue(x);
    imageScroll_->verticalScrollBar()->setValue(y);
}
void Editor::resetLayoutTools() {
    explosionActive_ = false;
    componentEditing_ = false;
    componentTool_->hide();
    splitBaseline_.reset();
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
            inspectorScroll_ = new QScrollArea(detailsStack_);
            inspectorScroll_->setWidgetResizable(true);
            layoutInspector_ = new LayoutInspector(layoutCanvas_);
            inspectorScroll_->setWidget(layoutInspector_);
            detailsStack_->addWidget(inspectorScroll_);
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
            connect(layoutCanvas_, &LayoutCanvas::zoomRequested, this, &Editor::zoom);
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
    if (componentEditing_)
        return;
    QString id = canvas_->selected();
    if (id.isEmpty())
        return;
    remember();
    doc_.notes.removeIf([&](const Note &n) { return n.id == id; });
    canvas_->select({});
    changed();
}
void Editor::copyImage() {
    if (hasDocument()) {
        const bool result = doc_.layout.has_value();
        QApplication::clipboard()->setImage(result ? renderLayout(doc_.image, *doc_.layout) : doc_.image);
        hint_->setText(result ? "调整效果已复制" : "原图已复制");
    }
}
void Editor::showError(const QString &text) {
    QMessageBox::warning(this, "HelpDesign", text);
}
bool Editor::saveProject() {
    if (!hasDocument())
        return true;
    QString path = QFileDialog::getSaveFileName(this, "保存原图、批注与变化",
                                                projectPath_.isEmpty() ? "feedback.json" : projectPath_,
                                                "HelpDesign 反馈 (*.json)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".json", Qt::CaseInsensitive))
        path += ".json";
    try {
        const auto bytes = serializeFeedback(doc_, true);
        saveBytes(path, bytes);
        projectPath_ = path;
        doc_.dirty = false;
        hint_->setText("已保存 JSON，包含完整原图、批注与变化");
        return true;
    } catch (const std::exception &e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}
bool Editor::allowReplace() {
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
                                            "图片或项目 (*.png *.jpg *.jpeg *.webp *.bmp *.json)");
    if (path.isEmpty())
        return;
    try {
        Document next = loadDocument(path);
        if (!allowReplace())
            return;
        setDocument(std::move(next));
        if (path.endsWith(".json", Qt::CaseInsensitive))
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
    if (!hasDocument())
        return;
    const bool layoutPreview = !annotated && doc_.layout.has_value();
    QString path = QFileDialog::getSaveFileName(this,
                                                annotated       ? "保存批注预览"
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
    if (!hasDocument())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("导出批注");
    dialog.resize(730, 640);
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    auto title = new QLabel(QString("导出批注  ·  %1 条").arg(doc_.notes.size()), &dialog);
    QFont font = title->font();
    font.setPointSize(15);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    auto embed = new QCheckBox("包含原图，可独立还原", &dialog);
    embed->setObjectName("embedOriginal");
    embed->setChecked(true);
    layout->addWidget(embed);
    auto json = new JsonPreview(&dialog);
    json->setAccessibleName("标准化 JSON");
    json->setReadOnly(true);
    json->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    json->setFont(QFont("Consolas", 10));
    layout->addWidget(json, 1);
    auto status = mutedLabel({}, &dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto row = new QHBoxLayout;
    auto save = textButton("保存 JSON 与图片", false, &dialog), close = textButton("关闭", false, &dialog),
         copy = textButton("复制 JSON", true, &dialog);
    row->addWidget(save);
    row->addStretch();
    row->addWidget(close);
    row->addWidget(copy);
    layout->addLayout(row);
    QByteArray exportBytes;
    auto refresh = [&] {
        try {
            exportBytes = serializeFeedback(doc_, embed->isChecked());
            const auto feedback = QJsonDocument::fromJson(exportBytes).object();
            QStringList lines{"{"};
            if (embed->isChecked())
                lines.append(
                    "  \"image\": \"data:image/png;base64,[图片编码已折叠，复制或保存包含完整原图]\",");
            lines.append("  \"annotationSpace\": \"result\",");
            for (const auto &name : {QString("annotations"), QString("changes")}) {
                lines.append("  \"" + name + "\": [");
                const auto entries = feedback[name].toArray();
                for (qsizetype i = 0; i < entries.size(); ++i)
                    lines.append("    " +
                                 QString::fromUtf8(
                                     QJsonDocument(entries[i].toObject()).toJson(QJsonDocument::Compact)) +
                                 (i + 1 < entries.size() ? "," : ""));
                lines.append(name == "annotations" ? "  ]," : "  ]");
            }
            lines.append("}");
            json->setPlainText(lines.join('\n'));
            copy->setEnabled(true);
            save->setEnabled(true);
            status->setText(QString("%1 · %2 字符 · 批注坐标对应调整后的画面")
                                .arg(embed->isChecked() ? "图片编码仅在预览中折叠，复制/保存包含完整原图"
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
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto copyJson = [&] {
        if (exportBytes.isEmpty())
            return;
        QApplication::clipboard()->setText(QString::fromUtf8(exportBytes));
        status->setText(embed->isChecked() ? "JSON 已复制，包含完整原图" : "JSON 已复制，未包含原图");
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
                QDir(base).filePath("HelpDesign-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
                                    "-" + uniqueId().left(4));
            if (!QDir().mkpath(folder))
                throw std::runtime_error("无法创建导出目录");
            QDir dir(folder);
            saveBytes(dir.filePath("feedback.png"), doc_.png);
            saveBytes(dir.filePath("annotations.png"), encodePng(previewImage(doc_)));
            if (doc_.layout && !exportFeedback(doc_)["changes"].toArray().isEmpty())
                saveBytes(dir.filePath("result.png"), encodePng(renderLayout(doc_.image, *doc_.layout)));
            saveBytes(dir.filePath("feedback.json"), exportBytes);
            status->setText("已保存到：" + folder);
        } catch (const std::exception &e) {
            status->setText(QString::fromUtf8(e.what()));
        }
    });
    dialog.exec();
}
void Editor::showContext(QPoint p) {
    QMenu menu(this);
    menu.addAction("重新截图", this, &Editor::captureRequested);
    menu.addAction("打开图片或项目", this, [this] { openFile(); });
    menu.addSeparator();
    menu.addAction(canvas_->layoutPreview() ? "复制调整效果" : "复制原图", this, &Editor::copyImage);
    menu.addAction(canvas_->layoutPreview() ? "保存调整效果" : "保存原图", this, [this] { saveImage(); });
    menu.addAction("保存批注预览", this, [this] { saveImage(true); });
    menu.addAction("保存项目", this, [this] { saveProject(); });
    menu.addAction("导出 JSON", this, &Editor::exportJson);
    menu.addSeparator();
    menu.addAction("适应图片", this, &Editor::fit);
    menu.addAction("关闭当前截图", this, &QWidget::close);
    menu.exec(p);
}
void Editor::closeEvent(QCloseEvent *e) {
    e->ignore();
    if (!allowReplace())
        return;
    ++generation_;
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
