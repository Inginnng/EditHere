#pragma once
#include "aifeedback.h"
#include "settings.h"
#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
namespace h2d {
class AiCredentials;
// Owns an immutable annotation snapshot. Only the generated text is copied or
// saved; the screenshot stays outside the destination Agent's conversation.
class AiInstructionsDialog final : public QDialog {
    Q_OBJECT
  public:
    enum { Configure = 2 };
    AiInstructionsDialog(const AppSettings &, AiFeedbackInput, QWidget *parent = nullptr,
                         AiCredentials *credentials = nullptr);
    ~AiInstructionsDialog() override;
    void done(int result) override;
  private:
    void generate();
    void setBusy(bool busy);
    void updateOutputActions();
    AiConnectionSettings connection_;
    AiFeedbackInput input_;
    QString key_;
    bool credentialsLoaded_ = false;
    bool busy_ = false;
    AiCredentials *credentials_;
    AiFeedbackClient *client_;
    QLabel *status_, *usage_;
    QPlainTextEdit *output_;
    QProgressBar *progress_;
    QPushButton *generate_, *cancel_, *copy_, *save_, *configure_;
};
} // namespace h2d
