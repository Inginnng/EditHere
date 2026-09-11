#pragma once
#include "canvas.h"
#include <QPointer>
#include <QWidget>
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QPushButton;
namespace h2d {
class Editor final : public QWidget {
    Q_OBJECT
  public:
    explicit Editor(QWidget *parent = nullptr);
    void setDocument(Document document);
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
  signals:
    void captureRequested();
    void hiddenToTray();

  protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    void editNote(Note note, bool fresh, QPoint global);
    void changed();
    void renderNotes();
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
    History history_;
    Canvas *canvas_;
    QScrollArea *imageScroll_, *notesScroll_;
    QWidget *notesPanel_, *noteContainer_;
    QVBoxLayout *noteLayout_;
    QLabel *meta_, *hint_, *noteCount_;
    QPushButton *undo_, *redo_, *zoom_, *notesToggle_;
    QVector<QPushButton *> modes_;
    bool fitted_ = true;
    int generation_ = 0;
    QString projectPath_;
};
} // namespace h2d
