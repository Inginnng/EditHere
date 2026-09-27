#pragma once
#include <QImage>
namespace h2d {
// Rows at the top and the bottom of a scrolling region that do not move: a site
// header and a player bar. They have to be kept out of the stitching, because a
// frame that starts with a fixed header never lines up with the rows the previous
// frame ended on.
struct ScrollBands {
    int top = 0;    // Rows at the top that repeat the previous frame.
    int bottom = 0; // Rows at the bottom that repeat the previous frame.
    bool operator==(const ScrollBands &other) const {
        return top == other.top && bottom == other.bottom;
    }
};

// Reads how many rows at each edge of `next` repeat `previous`. Both pictures must
// have the same size. A band stops at the first row that moved, so a page where
// everything moved reports nothing and a page that did not move at all reports the
// whole height.
ScrollBands fixedBands(const QImage &previous, const QImage &next, double tolerance = 6.0);

// How different two whole frames are, in the same unit as OverlapMatch::score: an
// average difference per channel over the sampled pixels. Pictures that do not have
// the same size, or either of them being empty, count as completely different.
double frameDifference(const QImage &left, const QImage &right);

// What two consecutive frames of a scrolling picture have in common.
struct OverlapMatch {
    bool found = false;
    int overlap = 0;    // Rows the second picture repeats from the bottom of the first.
    double score = 0.0; // Average difference per channel over those rows; 0 is identical.
};

// Looks for the number of rows the top of `next` repeats the bottom of `current`.
// Both pictures must have the same width. `tolerance` is the largest average
// difference per channel that still counts as a match; anything above it means the
// two frames do not overlap, which is what happens on a page that scrolled too far
// between two grabs. The comparison samples the pictures sparsely, so a very tall
// capture costs no more than a short one.
OverlapMatch matchVerticalOverlap(const QImage &current, const QImage &next, int minOverlap,
                                  int maxOverlap, double tolerance = 6.0);

// Adds a scrolled frame under a running long picture and returns the rows that were
// added. Returns -1 when there was nothing to add: the frame repeats what is already
// there, which is how the end of a page is recognised.
int appendScrolledFrame(QImage *picture, const QImage &frame, int minOverlap = 8,
                        double tolerance = 6.0);

// Grows a long picture one frame at a time. Everything here is arithmetic on
// QImages, so a test can drive a whole run without a screen, a timer, or a wheel.
class ScrollStitcher final {
  public:
    // The first frame is the region as it was selected.
    void reset(const QImage &first);
    // Places one more frame and returns the rows it added; 0 or less means the page
    // had nothing new. A frame whose pixels are only approximately the same as the
    // previous ones still gets placed, and `partial()` says so afterwards.
    int add(const QImage &frame);
    // The finished picture: the scrolling content with the fixed bottom band put
    // back under it once.
    QImage picture() const;
    // True when at least one frame had to be placed on a looser match than an exact
    // one, which is what a page with something moving in it looks like.
    bool partial() const {
        return partial_;
    }
    // The rows at the edges that were last seen not moving.
    ScrollBands bands() const {
        return bands_;
    }
    int height() const {
        return body_.height() + bottom_.height();
    }
    // The tallest picture a run will build; past this a page is assumed to be one
    // that never stops producing rows.
    static int maxHeight();

  private:
    QImage first_;
    QImage previous_;
    QImage body_;   // The scrolling content, without the fixed bottom band.
    QImage bottom_; // The fixed bottom band, kept from the newest frame.
    ScrollBands bands_;
    bool stripped_ = false;
    bool partial_ = false;
};
} // namespace h2d
