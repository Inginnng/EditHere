#pragma once
#include "aifeedback.h"
#include "settings.h"
#include <QDialog>
#include <optional>
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
namespace h2d {
class AiCredentials;
struct AiInstructionsDraft {
    QString text;
    std::optional<QString> extraInstructions;
};
class AiInstructionsDialog final : public QDialog {
    Q_OBJECT
  public:
    enum { Configure = 2 };
    AiInstructionsDialog(const AppSettings &, AiFeedbackInput, QWidget *parent = nullptr,
                         AiCredentials *credentials = nullptr, AiInstructionsDraft draft = {},
                         AiCredentials *providerProbe = nullptr);
    ~AiInstructionsDialog() override;
    AiInstructionsDraft draft() const;
    void applyTo(AppSettings &settings) const;
    void done(int result) override;
  protected:
    void changeEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
  private:
    void generate();
    void probeConfiguredProviders();
    void updateProviderButtonIfReady();
    void updateProviderButton();
    void switchConnection(const QString &providerId);
    QString providerDisplayName(const QString &providerId) const;
    void setBusy(bool busy);
    void setStatus(const QString &, bool error = false);
    void updateOutputActions();
    void refreshStyle();
    void updateSourceWidth();
    AppSettings settings_;
    AiConnectionSettings connection_;
    AiFeedbackInput input_;
    QString systemPrompt_;
    QStringList configuredProviders_;
    int pendingProviderProbes_ = 0;
    bool providerProbeDone_ = false;
    bool activeProbeDone_ = false;
    AiCredentials *providerProbe_ = nullptr;
    QString key_;
    bool credentialsLoaded_ = false, busy_ = false, hasOutput_ = false, refreshingStyle_ = false;
    AiCredentials *credentials_;
    AiFeedbackClient *client_;
    QLabel *status_, *usage_, *count_, *emptyTitle_, *emptyHint_, *modelLabel_ = nullptr;
    QLabel *emptyIcon_ = nullptr;
    QLineEdit *extra_;
    QPlainTextEdit *output_;
    QStackedWidget *content_;
    QSplitter *splitter_ = nullptr;
    QWidget *sourcePanel_ = nullptr;
    QProgressBar *progress_;
    QPushButton *generate_, *cancel_, *copy_, *save_, *configure_, *providerSwitch_, *prompt_;
    QMenu *providerMenu_ = nullptr;
};
} // namespace h2d
