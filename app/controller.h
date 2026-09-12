#pragma once
#include "editor.h"
#include "overlay.h"
#include "settings.h"
#include <QSystemTrayIcon>
namespace h2d {
class Controller final : public QObject {
    Q_OBJECT
  public:
    explicit Controller(QObject *parent = nullptr, const AppSettings &settings = loadSettings());
    void start(bool demo, const QString &path = {});
    void capture();
    void activate();
    void quit();
    void openSettings(bool updates = false, bool toolbar = false);

  private:
    void clearOverlays();
    void cancelCapture();
    void completeCapture(Overlay *source, QRect pixels, QVector<Candidate> candidates);
    void updateTrayShortcut();
    AppSettings settings_;
    QAction *captureAction_ = nullptr;
    Editor editor_;
    QSystemTrayIcon tray_;
    GlobalShortcut shortcut_;
    QVector<Overlay *> overlays_;
    bool startupUpdateChecked_ = false;
    bool capturing_ = false, wasVisible_ = false;
};
} // namespace h2d
