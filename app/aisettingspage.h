#pragma once
#include "settings.h"
#include <QSet>
#include <QWidget>

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
  protected:
    void showEvent(QShowEvent *) override;
    void changeEvent(QEvent *) override;
  private:
    AiConnectionSettings currentConnection() const;
    void rememberConnection();
    void switchProvider();
    void loadKey();
    void updateNote();
    void setStatus(const QString &, bool error = false);
    void credentialBusy(bool busy);
    QMap<QString, AiConnectionSettings> connections_;
    QMap<QString, QString> keysByProvider_;
    QSet<QString> loadedKeys_, dirtyKeys_;
    QString currentProvider_, loadingProvider_;
    AiCredentials *credentials_;
    AiFeedbackClient *client_;
    QComboBox *provider_, *protocol_;
    QLineEdit *apiKey_, *baseUrl_, *model_;
    QSpinBox *timeout_;
    QPlainTextEdit *extra_;
    QWidget *advanced_;
    QLabel *description_, *providerLabel_, *keyLabel_, *keyHint_, *note_, *status_, *baseLabel_,
        *modelLabel_, *protocolLabel_, *timeoutLabel_, *extraLabel_;
    QPushButton *showKey_, *saveKey_, *deleteKey_, *test_, *advancedButton_, *reset_, *prompt_, *docs_;
    bool credentialsBusy_ = false;
};
} // namespace h2d
