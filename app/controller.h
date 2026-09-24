#pragma once
#include "editor.h"
#include "overlay.h"
#include "settings.h"
#include <QSystemTrayIcon>
namespace h2d {
class Controller final : public QObject {
    Q_OBJECT
  public:
    explicit Controller(QObject *parent = nullptr, const AppSettings &settings = loadSettings(),
                        const QString &settingsFile = {});
    void start(bool demo, const QString &path = {}, bool background = false, bool firstUse = false);
    void showGuide();
    void capture();
    void activate();
    void quit();
    void openSettings(bool updates = false, bool toolbar = false);
    QJsonObject handleAgentRequest(const QJsonObject &request);
    void cancelAgentSession(const QString &id, const QString &code, const QString &message);
  signals:
    void agentSessionFinished(const QString &id, const QJsonObject &result);

  private:
    void finishAgentSession();
    void clearOverlays();
    void cancelCapture();
    void raiseEditor();
    void completeCapture(Overlay *source, QRect pixels, QVector<Candidate> candidates);
    void updateTrayShortcut();
    AppSettings settings_;
    QString settingsFile_;
    bool guidePending_ = false;
    QAction *captureAction_ = nullptr;
    Editor editor_;
    QSystemTrayIcon tray_;
    GlobalShortcut shortcut_;
    QVector<Overlay *> overlays_;
    QString agentSessionId_, agentOutput_;
    bool agentEmbed_ = true;
    bool startupUpdateChecked_ = false;
    bool capturing_ = false, wasVisible_ = false;
};
} // namespace h2d
