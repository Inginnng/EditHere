#pragma once

#include <QPlainTextEdit>
#include <functional>

namespace h2d {

class NoteFadeEffect;

// A note owns its full height; the surrounding notes panel owns scrolling.
class InlineNoteEdit final : public QPlainTextEdit {
  public:
    explicit InlineNoteEdit(QWidget *parent = nullptr);
    explicit InlineNoteEdit(const QString &text, QWidget *parent = nullptr);

    std::function<void()> started, finished, cancelled;
    std::function<void()> presentationChanged;

    void fitContent();
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }
    bool needsCollapse() const { return needsCollapse_; }

  protected:
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    NoteFadeEffect *fade_ = nullptr;
    bool expanded_ = false;
    bool needsCollapse_ = false;
    bool fitting_ = false;
    bool suppressFocusFinish_ = false;
};

} // namespace h2d
