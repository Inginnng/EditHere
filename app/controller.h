#pragma once
#include "editor.h"
#include "overlay.h"
#include <QSystemTrayIcon>
namespace h2d {
class Controller final : public QObject {
    Q_OBJECT
  public:
    explicit Controller(QObject *parent = nullptr);
    void start(bool demo, const QString &path = {});
    void capture();
    void activate();
    void quit();

  private:
    void clearOverlays();
    void cancelCapture();
    void completeCapture(Overlay *source, QRect pixels, QVector<Candidate> candidates);
    Editor editor_;
    QSystemTrayIcon tray_;
    GlobalShortcut shortcut_;
    QVector<Overlay *> overlays_;
    bool capturing_ = false, wasVisible_ = false;
};
} // namespace h2d
