#pragma once
#include "settings.h"
#include <QColor>
#include <QIcon>
#include <QRectF>
#include <QString>
#include <QWidget>
class QPushButton;
class QLabel;
class QPainter;
namespace h2d {
// The magnifier panel. It is laid out from these rather than from a layout class,
// because it is painted rather than assembled and the two have to agree exactly, and
// they live here because the tests locate the panel by the same numbers.
constexpr double magnifierPanelWidth = 162;
constexpr double magnifierPanelHeight = 236;
constexpr double magnifierPanelPadding = 8;
constexpr double magnifierZoomHeight = 94;
// One pixel of the screen is this many pixels wide inside the enlargement.
constexpr double magnifierZoomCell = 15;
// Where the magnifier panel goes for a pointer at `at` in a window of `window`: beside
// the pointer, flipped to the other side of it when it would fall off an edge, and
// then stepped clear of `tools`, the rectangles the window's own tool widgets occupy.
// Those widgets are children, so they are painted over the panel, and a readout that
// has disappeared behind the toolbar is worse than one a little further from the
// pointer. The window and the tests ask for the same rectangle from here, so the two
// cannot drift apart.
QRectF magnifierPlacement(QPointF at, QSizeF window, const QVector<QRectF> &tools = {});
// The panel a long capture grows in, beside the region. It is a fixed strip: the picture
// in it is scaled to fit rather than the panel changing size, because a panel that grew
// with the picture would end up taller than the screen it is on.
constexpr double scrollPreviewWidth = 84;
constexpr double scrollPreviewHeight = 210;
constexpr double scrollPreviewPadding = 8;
// Where the panel goes. A picture that grows downwards is shown beside the region and
// one that grows sideways under it, which is the side there is room on in each case. It
// is kept clear of the tools the same way the magnifier is, and shares that search.
QRectF scrollPreviewPlacement(const QRectF &region, QSizeF window, Qt::Orientation axis,
                              const QVector<QRectF> &tools = {});
// "1234 px" or "1.2 万像素" — a long capture can get far past the point where four
// digits are worth reading.
QString scrollLengthText(int pixels);
QColor accent();
void applyTheme(ThemeMode mode = ThemeMode::Light);
// The interface face follows the language: the Chinese face has wider Latin
// metrics, so English uses the system face instead.
void applyInterfaceFont();
bool isDarkTheme();
void paintTransparency(QPainter &painter, const QRect &area);
QIcon glyph(const QString &name, QColor color = QColor());
QPushButton *iconButton(const QString &name, const QString &label, QWidget *parent = nullptr);
QPushButton *textButton(const QString &text, bool primary = false, QWidget *parent = nullptr);
QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr);
class DragBar : public QWidget {
  public:
    explicit DragBar(QWidget *parent = nullptr);

  protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    QPoint offset_;
    bool dragging_ = false;
};
} // namespace h2d
