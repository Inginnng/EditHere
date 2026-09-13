#pragma once
#include "settings.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QCheckBox;
class QTabWidget;
class QKeySequenceEdit;
class QLabel;
namespace h2d {
class SettingsDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
    AppSettings settings() const;
    void showUpdates(bool checkNow = false);
    void showToolbar();
    void setLaunchAtLoginNotice(const QString &notice);
    void setApplyHandler(std::function<QString(const AppSettings &)> handler);

  signals:
    void guideRequested();

  private:
    void setDraft(const AppSettings &settings);
    void save();
    QComboBox *theme_, *defaultTool_;
    QCheckBox *captureOnStartup_, *launchAtLogin_, *fitImageOnOpen_, *embedOriginal_, *checkUpdatesOnStartup_;
    QTabWidget *tabs_;
    class UpdateChecker *updater_;
    QLabel *error_, *launchAtLoginNotice_;
    QMap<QString, QKeySequenceEdit *> keys_;
    QMap<QString, QCheckBox *> toolbarActions_;
    std::function<QString(const AppSettings &)> apply_;
};
} // namespace h2d
