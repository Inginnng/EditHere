#pragma once
#include "settings.h"
#include <QColor>
#include <QIcon>
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
