#pragma once
#include "settings.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QKeySequenceEdit;
class QLabel;
namespace h2d {
class SettingsDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
    AppSettings settings() const;
    void setApplyHandler(std::function<QString(const AppSettings &)> handler);

  private:
    void setDraft(const AppSettings &settings);
    void save();
    QComboBox *theme_;
    QLabel *error_;
    QMap<QString, QKeySequenceEdit *> keys_;
    std::function<QString(const AppSettings &)> apply_;
};
} // namespace h2d
