#pragma once
#include "capturesession.h"
#include "settings.h"
#include <QSize>
#include <QWidget>
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
namespace h2d {
// The one popup both the corner tool and the shadow tool open. Two tabs rather than a
// list of steps, and a slider rather than a set of numbers, because both effects are
// the kind of thing that has to be judged by eye on the picture itself.
class StylePanel final : public QWidget {
    Q_OBJECT
  public:
    enum class Mode { Corners, Shadow };
    explicit StylePanel(Mode mode, QWidget *parent = nullptr);
    void setStyle(const CaptureStyle &style);
    const CaptureStyle &style() const {
        return style_;
    }
    // Opens it beside a button, kept inside the screen it belongs to.
    void popupBeside(const QWidget *anchor, const QRect &bounds);
    void retranslate();
    // Re-applies the panel's own look; called once at construction and again when the
    // theme changes, which is what keeps it readable in both.
    void refresh();

  signals:
    // Every change is announced as it happens, so the region behind the panel shows
    // what the numbers mean before anything is committed.
    void styleChanged(const h2d::CaptureStyle &style);
    // The user asked for this to be what every later capture starts from.
    void rememberRequested(const h2d::CaptureStyle &style);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    void syncFromStyle();
    void emitStyle();
    void setTab(int tab);
    Mode mode_;
    int tab_ = 0; // 0 = 阴影, 1 = 边框
    CaptureStyle style_;
    QPushButton *shadowTab_ = nullptr;
    QPushButton *borderTab_ = nullptr;
    QSlider *strength_ = nullptr;
    QLabel *strengthValue_ = nullptr;
    QCheckBox *remember_ = nullptr;
    QLabel *caption_ = nullptr;
    bool syncing_ = false;
};

// The bar that appears above a finished selection. It is a child of the capture window
// rather than a window of its own, so the keyboard keeps reaching the selection while
// the bar is up and the bar can never end up behind it.
class CaptureToolbar final : public QWidget {
    Q_OBJECT
  public:
    explicit CaptureToolbar(QWidget *parent = nullptr);
    // Places the bar above the selection, which is where a capture tool puts it: the
    // space under a selection is usually the part the user just decided to keep.
    void placeNear(const QRect &selection, const QRect &bounds);
    void setSelectionSize(QSize size);
    // The corner of the screen the selection sits in, as "1234, 567".
    void setSelectionOrigin(QPoint origin);
    // Ratio and size live here rather than in the window, because the bar is where
    // they are shown and where their effect has to be readable.
    CaptureRatio ratio() const {
        return ratioChoice_;
    }
    int customWidth() const {
        return customWidth_;
    }
    int customHeight() const {
        return customHeight_;
    }
    OcrLanguageMode ocrLanguage() const {
        return ocrLanguage_;
    }
    void setOcrLanguage(OcrLanguageMode language);
    // What earlier captures and selections exist, as the caller knows them. The bar
    // only lists them: it never holds a picture of its own, so nothing is copied for
    // the sake of a menu.
    void setHistory(const QStringList &labels);
    void setSelections(const QVector<QRect> &selections);
    // Which entry of the history the window is showing, or -1 for none, so the menu
    // can say where "previous" and "next" would go.
    void setHistoryIndex(int index);
    // Opens the same menu the button shows, so the keyboard can reach it too.
    void showMenu();
    // --- 长截图 ---
    // What a long capture is doing, so the button that starts it can also stop it and
    // the bar can say how far the run has got. Starting and stopping are the same
    // button, which is what makes a run that can be left going and interrupted rather
    // than one that has to be waited out.
    void setScrollState(bool active, bool running, Qt::Orientation axis);
    bool scrollActive() const {
        return scrollActive_;
    }
    bool scrollRunning() const {
        return scrollRunning_;
    }
    Qt::Orientation scrollAxis() const {
        return scrollAxis_;
    }
    // What the button says the run is doing, which the window sets while frames are
    // being placed.
    void setScrollStatus(const QString &status);
    // Disables the whole bar and says what is going on, which is what a long capture
    // or a recognition needs while it runs.
    void setBusy(bool busy, const QString &message = {});
    // Re-applies every visible string; called once at construction and again on a
    // language change.
    void retranslate();
    // The same actions the buttons trigger, so the keyboard can reach them while the
    // pointer is somewhere else.
    void copy();
    void pin();
    void save();
    void recognize();
    void scroll();
    void annotate();
    void dismiss();

  signals:
    void copyRequested();
    void pinRequested();
    void saveRequested();
    void ocrRequested();
    void scrollRequested();
    // The long-capture button was pressed: true starts a run, false stops the one that
    // is going. One button, because that is how a capture the user drives is left and
    // interrupted.
    void scrollRunRequested(bool running);
    // The user asked to leave long-capture mode altogether and get the ordinary tools
    // back for this region.
    void scrollStopRequested();
    // The direction the next run takes was switched.
    void scrollAxisChanged(Qt::Orientation axis);
    void scrollTrimRequested(bool fromStart);
    void annotateRequested();
    void dismissed();
    // The user picked a size or a ratio, which the window applies to the selection
    // before the bar is told what came of it.
    void sizeRequested(QSize size);
    void ratioChanged();
    // Index 0 is the newest capture, matching the history the caller filled in.
    void historyRequested(int index);
    // The user stepped one capture back (-1) or forward (+1) through the history.
    void historyStepRequested(int delta);
    void selectionRequested(QRect selection);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    // Qt 6 dropped QInputDialog::getSize, so the two numbers are asked for by hand.
    static QSize askForSize(QWidget *parent, QSize current, bool *accepted);
    void chooseSize();
    void buildMenu();
    void setRatio(CaptureRatio ratio, int customWidth = 16, int customHeight = 10);
    QPushButton *addButton(const QString &glyph, const QString &label);
    QPushButton *size_ = nullptr;
    QPushButton *ratio_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *more_ = nullptr;
    QPushButton *scrollAxisButton_ = nullptr;
    QPushButton *scrollTrimStart_ = nullptr;
    QPushButton *scrollTrimEnd_ = nullptr;
    QVector<QPushButton *> buttons_;
    CaptureRatio ratioChoice_ = CaptureRatio::Free;
    OcrLanguageMode ocrLanguage_ = OcrLanguageMode::System;
    int customWidth_ = 16, customHeight_ = 10;
    QStringList historyLabels_;
    QVector<QRect> selections_;
    int historyIndex_ = -1;
    bool busy_ = false;
    QString message_;
    bool scrollActive_ = false;
    bool scrollRunning_ = false;
    Qt::Orientation scrollAxis_ = Qt::Vertical;
    QString scrollStatus_;
};

// The column of style tools that stands beside a selection, the way a capture tool
// keeps its decoration switches out of the row of verbs. It owns the style, because
// this is where the style is decided and where its effect has to be readable.
class CaptureSidebar final : public QWidget {
    Q_OBJECT
  public:
    explicit CaptureSidebar(QWidget *parent = nullptr);
    // Stands to the right of the selection, or to the left when the screen ends first.
    void placeBeside(const QRect &selection, const QRect &bounds);
    void setStyle(const CaptureStyle &style);
    const CaptureStyle &style() const {
        return style_;
    }
    void retranslate();
    void setBusy(bool busy);

  signals:
    void resetRequested();
    void styleChanged(const h2d::CaptureStyle &style);
    void rememberRequested(const h2d::CaptureStyle &style);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    void openPanel(StylePanel::Mode mode, QPushButton *anchor);
    void refreshButtons();
    QPushButton *corner_ = nullptr;
    QPushButton *shadow_ = nullptr;
    QPushButton *reset_ = nullptr;
    CaptureStyle style_;
    StylePanel *panel_ = nullptr;
    bool busy_ = false;
};
} // namespace h2d
