#pragma once
#include "settings.h"
#include <QSet>
#include <QWidget>
#include <functional>

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
namespace h2d {
class AiCredentials;
class AiFeedbackClient;
class AiSettingsPage final : public QWidget {
    Q_OBJECT
  public:
    explicit AiSettingsPage(QWidget *parent = nullptr, AiCredentials *credentials = nullptr);
    void setSettings(const AppSettings &settings);
    void applyTo(AppSettings &settings) const;
    void retranslate();
    // Secrets stay as drafts until the settings dialog's single Save. The callback
    // runs exactly once; failures retain all not-yet-saved drafts for retry.
    void saveCredentials(std::function<void(bool)> finished);
    bool hasPendingCredentials() const;
  protected:
    void showEvent(QShowEvent *) override;
    void changeEvent(QEvent *) override;
  private:
    AiConnectionSettings currentConnection() const;
    void rememberConnection();
    void switchProvider();
    void loadKey();
    void markKeyEdited();
    void updateNote();
    void setStatus(const QString &, bool error = false);
    void credentialBusy(bool busy);
    void syncProviderChoices();
    void updateAppearance();
    void saveNextCredential();
    void finishCredentialSave(bool success);
    QMap<QString, AiConnectionSettings> connections_;
    QMap<QString, QString> keysByProvider_;
    QSet<QString> loadedKeys_, dirtyKeys_;
    QString currentProvider_, loadingProvider_, savingProvider_;
    QStringList saveQueue_;
    std::function<void(bool)> saveFinished_;
    AiCredentials *credentials_;
    AiFeedbackClient *client_;
    QComboBox *provider_, *family_, *protocol_, *region_;
    QLineEdit *apiKey_, *baseUrl_, *model_;
    QSpinBox *timeout_;
    QPlainTextEdit *extra_;
    QWidget *advanced_;
    QMap<QString, QString> lastFamilyProvider_;
    QLabel *title_, *description_, *providerLabel_, *keyLabel_, *keyHint_, *note_, *status_, *baseLabel_,
        *modelLabel_, *protocolLabel_, *timeoutLabel_, *extraLabel_;
    QPushButton *test_, *advancedButton_, *reset_, *prompt_, *docs_, *keyMore_;
    QAction *showKey_, *clearKey_;
    bool credentialsBusy_ = false;
    bool savingCredentials_ = false;
    bool updatingAppearance_ = false;
};
} // namespace h2d
