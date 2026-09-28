#pragma once
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
namespace h2d {
// The aspect ratio a selection is held to while it is dragged.
enum class CaptureRatio { Free, Square, FourThree, ThreeTwo, SixteenNine, NineSixteen, Custom };

// What a locked ratio stands for. A custom entry carries its own numbers.
QSize captureRatioSize(CaptureRatio ratio, int customWidth = 16, int customHeight = 10);

// How a ratio reads in a menu, e.g. "3:2" or "16:10".
QString captureRatioLabel(CaptureRatio ratio, int customWidth = 16, int customHeight = 10);

// The two directions a long capture can run in, as they read in a menu. The names are
// about the page rather than about the wheel, because the user is looking at the
// content: a vertical capture is a page that scrolls up and down.
QString scrollAxisLabel(Qt::Orientation axis);

// The rectangle a drag ends up with: the free rectangle between the two corners,
// reshaped so that its width and height keep the locked ratio. The corner the drag
// started from never moves, so the selection grows away from the pointer.
QRect captureRectFromDrag(QPoint anchor, QPoint moving, CaptureRatio ratio, int customWidth = 16,
                          int customHeight = 10);

// Reshapes an existing rectangle to a ratio, keeping its top left corner and its
// width. The result is at least one pixel tall, so the selection never disappears.
QRect captureRectWithRatio(QRect current, CaptureRatio ratio, int customWidth = 16,
                           int customHeight = 10);

// Reshapes a rectangle to a requested size, again keeping its top left corner.
QRect captureRectWithSize(QRect current, QSize requested, CaptureRatio ratio, int customWidth = 16,
                          int customHeight = 10);

// How the region the user picked is turned into a finished picture.
struct CaptureStyle {
    int cornerRadius = 0; // Rounded corners; the pixels outside them come out clear.
    bool border = false;  // Drawn just inside the edges of the picture.
    int borderWidth = 2;
    QColor borderColor = QColor(0, 0, 0, 60);
    // On at a middle strength, because a shadow the user has to go looking for is a
    // shadow nobody ever sees. The panel's slider starts at this number and turning it
    // down to zero is what turns the shadow off.
    bool shadow = true;
    // The one number the shadow panel exposes, 0..100, in the spirit of the single
    // strength slider a screenshot tool shows. The blur and the darkness are derived
    // from it so the panel never has to offer two knobs that fight each other.
    int shadowStrength = 42;
    // What colour the halo is cast in. Left invalid, which means the accent the
    // interface is drawn in — the same blue the selection is outlined in — so the
    // shadow reads as this program's rather than as a grey smudge.
    QColor shadowColor;
    // Derived from the strength; zero when the shadow is off, which is what makes a
    // style that changes nothing cost nothing.
    int shadowRadius() const;
    QColor shadowTint() const;
    bool operator==(const CaptureStyle &) const = default;
};

// The four ways a picked colour can be written down. A picker shows all four and
// copies whichever one is current, which is the arrangement every colour picker
// worth using has settled on.
enum class ColourFormat { Rgb, Hex, Hsv, Hsl };

// The colour in one of those four forms, e.g. "#4f8ab6" or "RGB 79, 138, 182".
QString captureColourText(const QColor &colour, ColourFormat format);
// The short name of a form, e.g. "HEX", as it is written beside the value.
QString captureColourFormatLabel(ColourFormat format);
// The form that follows this one, wrapping round. What the Shift key does.
ColourFormat nextColourFormat(ColourFormat format);

// How far a shadow of this strength reaches outside the picture, in pixels.
int captureShadowRadius(bool shadow, int strength);
// The colour a shadow of this strength is painted in, transparency included.
QColor captureShadowTint(const QColor &colour, bool shadow, int strength);

// Applies the style to the pixels of a region. A style that changes nothing hands
// the pixels straight back, which keeps the common case free of a copy. A shadow
// grows the picture by twice its radius so the halo has somewhere to go.
QImage composeCapture(const QImage &region, const CaptureStyle &style);

// The halo a shadow of this strength casts, on its own and with the middle cut out, so
// the capture window can paint it around the region and the shadow can be judged before
// anything is copied. Null when the shadow is off.
QImage composeShadowPreview(const QSize &body, const CaptureStyle &style);

// Recent captures and selections outlive the session: the pictures are written to
// a directory and only a small index of rectangles is kept beside them.
class CaptureHistory {
  public:
    // An empty directory means the default one under the cache location.
    explicit CaptureHistory(const QString &directory = {});
    // Writes the picture and drops the oldest one beyond the limit. A null picture
    // is refused rather than stored as a broken entry.
    bool add(const QImage &image);
    int count() const;
    // Index 0 is the newest. Null when the index is out of range or the file has
    // gone missing.
    QImage at(int index) const;
    QString directory() const {
        return directory_;
    }
    void clear();
    // The most recent selections, newest first.
    QVector<QRect> selections() const;
    void rememberSelection(const QRect &selection);
    // How many captures and how many selections are kept.
    static int imageLimit();
    static int selectionLimit();

  private:
    QString nextCaptureName() const;
    QString indexPath() const;
    QString directory_;
};
} // namespace h2d
