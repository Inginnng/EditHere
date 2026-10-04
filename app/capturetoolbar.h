#pragma once
#include "capturesession.h"
#include "settings.h"
#include <QSize>
#include <QLabel>
#include <QWidget>
#include <optional>
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QScrollArea;
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
    // Disables the whole bar and says what is going on, which is what a recognition
    // needs while it runs.
    void setBusy(bool busy, const QString &message = {});
    void setScrollAvailable(bool available);
    // Re-applies every visible string; called once at construction and again on a
    // language change.
    void retranslate();
    // The same actions the buttons trigger, so the keyboard can reach them while the
    // pointer is somewhere else.
    void copy();
    void pin();
    void save();
    void recognize();
    void annotate();
    void dismiss();
    void scroll();

  signals:
    void copyRequested();
    void pinRequested();
    void saveRequested();
    void ocrRequested();
    void annotateRequested();
    void dismissed();
    void scrollRequested();
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
    QVector<QPushButton *> buttons_;
    CaptureRatio ratioChoice_ = CaptureRatio::Free;
    OcrLanguageMode ocrLanguage_ = OcrLanguageMode::System;
    int customWidth_ = 16, customHeight_ = 10;
    QStringList historyLabels_;
    QVector<QRect> selections_;
    int historyIndex_ = -1;
    bool busy_ = false;
    bool scrollAvailable_ = true;
    QString message_;
};

// A hollow, live frame marks the physical capture region without covering the page.
// Only its outside edges receive input when stopped; its separate move handle can
// move along the capture axis while running, and freely while stopped.
class ScrollCaptureRegion final : public QWidget {
    Q_OBJECT
  public:
    explicit ScrollCaptureRegion(QWidget *parent = nullptr);
    ~ScrollCaptureRegion() override;
    void setSelection(const QRect &selection, const QRect &bounds);
    QRect selection() const { return selection_; }
    void setState(bool running, Qt::Orientation axis = Qt::Vertical);
    QWidget *moveHandle() const { return handle_; }
    void setHandlePosition(QPoint global);
  signals:
    void regionChanged(const QRect &selection);
    void dragStarted();
    void dragFinished();
  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
  private:
    void placeHandle();
    void dragTo(QPoint global);
    QRect selection_, bounds_, dragSelection_;
    QPoint dragOrigin_;
    QWidget *handle_ = nullptr;
    QPoint handleAnchor_;
    bool anchored_ = false;
    Qt::Edges dragEdges_;
    Qt::Orientation axis_ = Qt::Vertical;
    bool running_ = true, dragging_ = false;
};

// Dims the screen outside the live capture region. Native exclusion keeps the
// shade out of sampled pixels while transparent input lets the target scroll.
class ScrollCaptureShade final : public QWidget {
  public:
    explicit ScrollCaptureShade(QWidget *parent = nullptr);
    void setSelection(const QRect &selection, const QRect &screen);
  protected:
    void paintEvent(QPaintEvent *event) override;
  private:
    QRect selection_;
};

class ScrollCapturePreview final : public QLabel {
    Q_OBJECT
  public:
    explicit ScrollCapturePreview(QWidget *parent = nullptr);
    void setCapture(const QImage &image, QSize fullSize, const QRect &viewport, bool matched,
                    Qt::Orientation axis, const QRect &imageSource = {});
    void setVisibleRegion(const QRect &region);
    QRect croppedRect() const;
    QRectF imageRect() const;
    QRectF viewportRect() const;
    void beginCrop(bool end);
    void clearCrop();
    void setAutoCrop(bool enabled);
  signals:
    void cropChanged();
  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
  private:
    void cropAt(QPointF point);
    QSize fullSize_;
    QRect viewport_;
    QRect visibleRegion_, imageSource_;
    Qt::Orientation axis_ = Qt::Vertical;
    int cropBegin_ = 0, cropEnd_ = 0, cropEdge_ = -1;
    bool matched_ = true, cropping_ = false, autoCrop_ = false;
    int growthDirection_ = 0;
    int knownGrowthDirection_ = 0;
};

// A separate window keeps the capture controls live while the frozen screen covers
// are hidden. It sits outside the captured region whenever there is room.
class ScrollCaptureProgress final : public QWidget {
    Q_OBJECT
  public:
    explicit ScrollCaptureProgress(QWidget *parent = nullptr);
    void placeBeside(const QRect &selection, const QRect &screen);
    // The image may already be reduced to previewWidth(); size is the full result.
    void setProgress(const QImage &image, int addedFrames, QSize size = {}, const QRect &viewport = {},
                     bool matched = true, Qt::Orientation axis = Qt::Vertical, const QRect &imageSource = {});
    // Keep the cross-axis scale fixed. Long results use a bounded native-pixel
    // window rather than shrinking the entire thumbnail to the selection height.
    QRect previewSourceRect(QSize size, const QRect &viewport, Qt::Orientation axis,
                            std::optional<int> nativeOrigin = {});
    // Device pixels the live preview is drawn at, so callers can scale only once.
    int previewWidth() const;
    void setAutomatic(bool automatic);
    void setAutomaticSupport(bool available, const QString &reason = {});
    // A recoverable problem: capture continues and the message stays until progress.
    void setNotice(const QString &message);
    void setStopped(const QString &message);
    void setRunning(Qt::Orientation axis);
    void setAxis(Qt::Orientation axis);
    void setSelectionSize(QSize size);
    QPoint moveHandlePosition() const;
    void setAutoCrop(bool enabled);
    bool autoCrop() const;
    void beginCrop(bool end);
    QRect croppedRect() const;
    void clearCrop();
  signals:
    void stopRequested();
    void finishRequested();
    void cancelRequested();
    void automaticChanged(bool automatic);
    void autoCropChanged(bool enabled);
    void resumeRequested();
    void directionRequested();
    void cropRequested(bool end);
    void copyRequested();
    void saveRequested();
    void pinRequested();
    void positionChanged(QPoint handlePosition);
  protected:
    void paintEvent(QPaintEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
  private:
    QScrollArea *previewArea_ = nullptr;
    ScrollCapturePreview *preview_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *size_ = nullptr;
    QWidget *moveSlot_ = nullptr;
    QWidget *cropMenu_ = nullptr;
    QPushButton *cropButton_ = nullptr;
    QCheckBox *autoCrop_ = nullptr;
    QPushButton *stop_ = nullptr;
    QPushButton *finish_ = nullptr;
    QCheckBox *automatic_ = nullptr;
    QPushButton *automaticButton_ = nullptr;
    QPushButton *direction_ = nullptr, *cropBegin_ = nullptr, *cropEnd_ = nullptr;
    QVector<QPushButton *> outputButtons_;
    QRect captureSelection_, captureScreen_;
    int previewMaximumWidth_ = 150;
    QPointF previewAnchor_;
    QSize previewLastSize_;
    QRect previewLastViewport_;
    std::optional<int> previewLastOrigin_;
    Qt::Orientation previewAxis_ = Qt::Vertical;
    bool hasProgress_ = false, stopped_ = false, canEdit_ = true;
    bool automaticSupported_ = true;
    QString automaticUnavailableReason_;
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
