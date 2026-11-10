#pragma once
#include "aifeedback.h"
#include "settings.h"
#include <QDialog>
#include <optional>
class QLabel;
class QLineEdit;
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
                         AiCredentials *credentials = nullptr, AiInstructionsDraft draft = {});
    ~AiInstructionsDialog() override;
    AiInstructionsDraft draft() const;
    void done(int result) override;
  protected:
    void changeEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
  private:
    void generate();
    void setBusy(bool busy);
    void setStatus(const QString &, bool error = false);
    void updateOutputActions();
    void refreshStyle();
    void updateSourceWidth();
    AiConnectionSettings connection_;
    AiFeedbackInput input_;
    QString systemPrompt_;
    QString key_;
    bool credentialsLoaded_ = false, busy_ = false, hasOutput_ = false, refreshingStyle_ = false;
    AiCredentials *credentials_;
    AiFeedbackClient *client_;
    QLabel *status_, *usage_, *count_, *emptyTitle_, *emptyHint_;
    QLabel *emptyIcon_ = nullptr;
    QLineEdit *extra_;
    QPlainTextEdit *output_;
    QStackedWidget *content_;
    QSplitter *splitter_ = nullptr;
    QWidget *sourcePanel_ = nullptr;
    QProgressBar *progress_;
    QPushButton *generate_, *cancel_, *copy_, *save_, *configure_;
};
} // namespace h2d
