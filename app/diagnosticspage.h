#pragma once
#include <QWidget>
class QLabel;
class QPushButton;

namespace h2d {
// The settings entry point depends on the logger; the logger has no UI dependency.
class DiagnosticsPage final : public QWidget {
    Q_OBJECT
  public:
    explicit DiagnosticsPage(QWidget *parent = nullptr);
  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
  private:
    void retranslate();
    void refresh();
    void exportLogs();
    QLabel *title_, *description_, *path_, *status_;
    QPushButton *open_, *export_;
};
} // namespace h2d
