#pragma once
#include <QWidget>
#include <QRect>
namespace h2d {
// The dimmed film a long capture lays over everything outside the region. It is a
// window of its own so that it can be transparent to input while it stays above the
// desktop: the page under the region has to keep taking the wheel and the drags by
// hand, and a film that swallowed them would be the old timer-driven capture back.
// The region itself is a hole in the film — through it the live desktop shows, which
// is what the page is read through while it scrolls.
class ScrollShade final : public QWidget {
  public:
    // `imageSize` is the pixel size of the grab the region is measured in, and
    // `geometry` is the on-screen rectangle this film covers: the hole handed over
    // later is in the grab's pixels and has to be mapped into the film's own units.
    ScrollShade(QSize imageSize, const QRect &geometry);
    // The hole, in the pixels of the grab. An empty rectangle dims the whole screen.
    void setHole(QRect pixels);

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;

  private:
    friend class Overlay;
    QSize imageSize_;
    QRect pixels_; // The hole as handed over, kept so a resize can map it again.
};
} // namespace h2d
