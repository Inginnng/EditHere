#pragma once
#include <QImage>
#include <QVector>

namespace h2d {
struct ScrollBands {
    int top = 0;
    int bottom = 0;
    bool operator==(const ScrollBands &other) const { return top == other.top && bottom == other.bottom; }
};

ScrollBands fixedBands(const QImage &previous, const QImage &next, double tolerance = 6.0);
double frameDifference(const QImage &left, const QImage &right);
// Small, local animation is allowed; changes spread over the page are movement.
bool equivalentScrollFrames(const QImage &left, const QImage &right);

struct OverlapMatch {
    bool found = false;
    int overlap = 0;
    double score = 0.0; // Mean channel error on informative pixels, in [0, 255].
    double confidence = 0.0;
    bool ambiguous = false;
};
OverlapMatch matchVerticalOverlap(const QImage &current, const QImage &next, int minOverlap,
                                  int maxOverlap, double tolerance = 6.0);
int appendScrolledFrame(QImage *picture, const QImage &frame, int minOverlap = 32,
                        double tolerance = 6.0);

// Frames are matched against the last accepted viewport, never the growing image.
// Accepted slices stay separate until picture() is requested, avoiding a full
// allocation and copy on every scroll. Failed additions leave all state intact.
class ScrollStitcher final {
  public:
    enum class Status { Empty, Added, Repeat, NoMatch, Ambiguous, GeometryChanged, Limit };
    void reset(const QImage &first);
    int add(const QImage &frame); // >0 new rows, 0 stationary, -1 rejected.
    QImage picture() const;
    bool partial() const { return partial_; }
    ScrollBands bands() const { return bands_; }
    const QImage &lastFrame() const { return previous_; }
    int height() const { return height_; }
    bool atLimit() const { return status_ == Status::Limit || height_ >= heightLimit_; }
    Status status() const { return status_; }
    static int maxHeight() { return 32767; }
    static qint64 maxPixels() { return 32000000; }

  private:
    QImage previous_;
    QVector<QImage> slices_;
    QImage bottom_;
    mutable QImage cached_;
    ScrollBands bands_;
    int height_ = 0;
    int heightLimit_ = 0;
    bool bandsKnown_ = false;
    bool partial_ = false;
    Status status_ = Status::Empty;
};
} // namespace h2d
