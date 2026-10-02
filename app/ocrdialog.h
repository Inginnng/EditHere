#pragma once
#include "ocr.h"
#include "settings.h"
#include <QDialog>
#include <QImage>
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;
namespace h2d {
// Shows what was read from a picture. The lines are listed rather than poured into
// one block of text, because picking a line is what highlights where it came from,
// and that is what makes a wrong reading easy to find.
class OcrDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit OcrDialog(QImage image, OcrLanguageMode language, QWidget *parent = nullptr);
    // Fills the window in. A failure keeps the picture and replaces the text with the
    // reason, so the same window is used for both outcomes.
    void setResult(const OcrResult &result);
    void setImage(const QImage &image, OcrLanguageMode language);
    void setBusy(bool busy);
    QString text() const;

  signals:
    // The window owns the language choice, so the caller is told which one to use.
    void retryRequested(OcrLanguageMode language);

  private:
    void retranslate();
    void refreshPreview();
    QImage image_;
    QImage preview_;
    QLabel *picture_ = nullptr;
    QWidget *previewWidget_ = nullptr;
    QLabel *status_ = nullptr;
    QListWidget *lines_ = nullptr;
    QComboBox *language_ = nullptr;
    QPushButton *copyAll_ = nullptr;
    QPushButton *copyLine_ = nullptr;
    QPushButton *retry_ = nullptr;
    QPushButton *close_ = nullptr;
    QVector<OcrLine> result_;
    QVector<QRectF> boxes_;
    bool busy_ = false;
};
} // namespace h2d
