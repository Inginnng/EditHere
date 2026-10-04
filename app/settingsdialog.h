#pragma once
#include "settings.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QCheckBox;
class QTabWidget;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
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
    void setUpdatePreparationHandler(std::function<bool()> handler);
    // Cancelling also rolls the live language preview back. QDialog::reject() is a
    // public slot, so the override keeps the same access.
    void reject() override;

  signals:
    void guideRequested();
    // Emitted whenever a language is installed, so chrome outside this dialog
    // (the tray menu and tooltip) can rebuild its own labels.
    void languageApplied();
    void updateInstallStarted();

  protected:
    void changeEvent(QEvent *) override;

  private:
    void setDraft(const AppSettings &settings);
    // Rebuilds every label of the dialog; called from the constructor and again
    // whenever the interface language changes while the dialog is open.
    void retranslate();
    void applyLanguage(LanguageMode mode);
    void save();
    QComboBox *theme_, *defaultTool_, *language_, *ocrLanguage_, *scrollAxis_;
    // Language the dialog opened with, restored when the user cancels.
    LanguageMode languageOnEntry_ = LanguageMode::System;
    // Language currently installed by this dialog, so repeated selections are no-ops.
    LanguageMode appliedLanguage_ = LanguageMode::System;
    QCheckBox *captureOnStartup_, *launchAtLogin_, *fitImageOnOpen_, *embedOriginal_,
        *confirmBeforeDiscard_, *checkUpdatesOnStartup_, *scrollAutoCrop_, *scrollUltraLong_;
    QLineEdit *feedbackDir_;
    QTabWidget *tabs_;
    class UpdateChecker *updater_;
    QLabel *error_, *launchAtLoginNotice_;
    // Labels styled through the "settingsSection" object name cannot carry a unique
    // object name of their own, so they are kept as members to stay retranslatable.
    QLabel *shortcutGlobalLabel_ = nullptr, *shortcutLocalLabel_ = nullptr;
    QLabel *appearanceTitle_ = nullptr, *languageTitle_ = nullptr, *toolbarTitle_ = nullptr;
    QLabel *ocrTitle_ = nullptr;
    QLabel *versionLabel_ = nullptr;
    QVector<QLabel *> shortcutLabels_;
    QMap<QString, QKeySequenceEdit *> keys_;
    QMap<QString, QCheckBox *> toolbarActions_;
    std::function<QString(const AppSettings &)> apply_;
    CaptureStyle captureStyle_;
};
} // namespace h2d
