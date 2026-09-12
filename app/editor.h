#pragma once
#include "canvas.h"
#include "settings.h"
#include <QKeySequence>
#include <QMap>
#include <QPointer>
#include <QWidget>
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QStackedWidget;
class QShortcut;
class QPlainTextEdit;
namespace h2d {
class LayoutCanvas;
class LayoutInspector;
class ExplosionWave;
class Editor final : public QWidget {
    Q_OBJECT
  public:
    explicit Editor(QWidget *parent = nullptr);
    void setDocument(Document document);
    void setPreferences(const AppSettings &settings);
    void setShortcuts(const QMap<QString, QKeySequence> &bindings);
    const Document &document() const {
        return doc_;
    }
    Canvas *canvas() const {
        return canvas_;
    }
    bool hasDocument() const {
        return !doc_.image.isNull();
    }
    bool allowReplace();
    bool saveProject();
    void openFile(const QString &path = {});
    void pasteImage();
    void exportJson();
    void fit();
    void explode();
    bool explosionActive() const {
        return explosionActive_;
    }
    LayoutCanvas *layoutCanvas() const {
        return layoutCanvas_;
    }
  signals:
    void captureRequested();
    void hiddenToTray();
    void toolbarSettingsRequested();

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    void editNote(Note note, bool fresh, QPoint global);
    void changed(bool contentChanged = true);
    void renderNotes();
    void beginNoteEdit(const QString &id);
    void finishNoteEdit();
    void cancelNoteEdit();
    void focusNote(const QString &id);
    void revealNote(const QString &id);
    void collapseOtherNotes(const QString &id);
    void addGlobalNote();
    void editMovement(QRectF source, QRectF destination);
    void addManualRegion(QRect area);
    void toggleAnnotations();
    void copyJson();
    void updateToolbar();
    void toast(const QString &message);
    void showContext(QPoint global);
    void copyImage();
    void saveImage(bool annotated = false);
    void removeSelected();
    void setMode(Canvas::Mode mode);
    void updateControls();
    void undo();
    void redo();
    void zoom(double value);
    void showError(const QString &text);
    QString coords(const Note &note) const;
    Document doc_;
    AppSettings preferences_;
    struct Snapshot {
        QVector<Note> notes;
        std::optional<LayoutState> layout;
    };
    QVector<Snapshot> undoHistory_, redoHistory_;
    void remember();
    void restore(Snapshot snapshot);
    void updateLayoutControls();
    void setExplosionActive(bool enabled);
    void setComponentEditing(bool enabled);
    void resetLayoutTools();
    void switchCanvas(QWidget *target);
    Canvas *canvas_;
    LayoutCanvas *layoutCanvas_ = nullptr;
    LayoutInspector *layoutInspector_ = nullptr;
    ExplosionWave *wave_ = nullptr;
    QScrollArea *inspectorScroll_ = nullptr;
    QStackedWidget *detailsStack_;
    std::optional<LayoutState> splitBaseline_;
    bool explosionActive_ = false;
    bool componentEditing_ = false;
    QMap<QString, QPointer<QWidget>> noteCards_;
    QMap<QString, QPointer<QPlainTextEdit>> noteEditors_;
    QString editingId_, draftId_;
    Snapshot editingBaseline_;
    bool editingWasDirty_ = false, renderingNotes_ = false, finishingEdit_ = false;
    bool annotationsVisible_ = true;
    QMap<QString, QPushButton *> outputButtons_;
    QScrollArea *imageScroll_, *notesScroll_;
    QWidget *notesPanel_, *noteContainer_;
    QVBoxLayout *noteLayout_;
    QHBoxLayout *dock_;
    QWidget *inspectorSeparator_ = nullptr;
    QLabel *meta_, *hint_, *noteCount_, *emptyNotes_;
    QPushButton *undo_, *redo_, *zoom_, *notesToggle_, *explosion_, *hideAnnotations_;
    QVector<QPushButton *> modes_;
    QMap<QString, QShortcut *> shortcuts_;
    bool fitted_ = true;
    int generation_ = 0;
    QString projectPath_;
};
} // namespace h2d
