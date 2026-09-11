#pragma once
#include "settings.h"
#include <QColor>
#include <QIcon>
#include <QWidget>
class QPushButton;
class QLabel;
namespace h2d {
QColor accent();
void applyTheme(ThemeMode mode = ThemeMode::Light);
bool isDarkTheme();
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
