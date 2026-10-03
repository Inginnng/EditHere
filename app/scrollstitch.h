#pragma once
#include <QImage>
#include <QRect>
#include <QVector>
#include <QSharedPointer>
#include <QTemporaryDir>
#include <memory>

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

// Match the last accepted viewport in either direction, then recover a unique
// position in retained native rows if a scroll skipped that viewport. Keep a
// covered interval so revisiting it never duplicates pixels. Accepted slices
// stay separate until picture() is requested.
class ScrollStitcher final {
  public:
    enum class Status { Empty, Added, Tracked, Repeat, NoMatch, Ambiguous, GeometryChanged, Limit };
    // A matched frame that has not been appended yet. Planning leaves the stitch
    // untouched, so a caller can decide later whether the frame is worth keeping.
    struct Step {
        int added = -1; // >0 new rows, 0 stationary, -1 rejected.
        Status status = Status::Empty;
        QImage frame;
        ScrollBands bands;
        double score = 0.0;
        int displacement = 0; // Signed page movement from the last accepted frame.
        int viewportOffset = 0; // Content coordinate, relative to the first frame.
        int prepend = 0;
        int append = 0;
        quint64 generation = 0;
    };
    void reset(const QImage &first);
    void setUltraLong(bool enabled) { ultraLong_ = enabled; }
    bool ultraLong() const { return ultraLong_; }
    Step plan(const QImage &frame) const;
    int commit(const Step &step); // A step planned before another commit is re-planned.
    int add(const QImage &frame) { return commit(plan(frame)); }
    QImage picture() const;
    // A narrow copy for live display, scaled once per accepted slice.
    QImage preview(int width, int maximumHeight = 0) const;
    QImage previewRegion(const QRect &region, int width) const;
    // Small strips allow PNG export without allocating a full-size bitmap.
    QImage readRows(int first, int count, bool transpose = false) const;
    bool savePng(const QString &path, bool transpose = false, QString *error = nullptr) const;
    bool trimBeforeViewport();
    bool trimAfterViewport();
    bool partial() const { return partial_; }
    ScrollBands bands() const { return bands_; }
    const QImage &lastFrame() const { return previous_; }
    int height() const { return height_; }
    // Native coordinate of the result's first row relative to the initial frame.
    int originOffset() const { return firstOffset_ + (bandsKnown_ ? bands_.top - top_.height() : 0); }
    QRect viewportRect() const;
    bool matched() const { return status_ == Status::Empty ? !previous_.isNull() :
        status_ == Status::Added || status_ == Status::Tracked || status_ == Status::Repeat; }
    bool atLimit() const { return status_ == Status::Limit || height_ >= heightLimit_; }
    Status status() const { return status_; }
    static int maxHeight() { return 32767; }
    static qint64 maxPixels() { return 32000000; }
    static int ultraMaxHeight() { return 2000000; }

  private:
    QImage previous_;
    struct Slice {
        QImage image, thumbnail;
        QVector<quint64> rowKeys;
        QVector<quint64> rowSignatures;
        QString path;
        int height = 0;
        ~Slice();
        QImage load() const;
    };
    QSharedPointer<Slice> storeSlice(const QImage &image);
    QVector<QSharedPointer<Slice>> slices_;
    std::unique_ptr<QTemporaryDir> spool_;
    quint64 spoolSequence_ = 0;
    QImage top_;
    QImage bottom_;
    mutable QImage cached_;
    mutable QVector<QImage> thumbnails_;
    mutable QImage regionPreview_;
    mutable QRect regionPreviewRect_;
    mutable int regionPreviewWidth_ = 0;
    mutable quint64 regionPreviewGeneration_ = 0;
    mutable int thumbnailWidth_ = 0;
    mutable double thumbnailScale_ = 0.0;
    ScrollBands bands_;
    quint64 generation_ = 0;
    int height_ = 0;
    int heightLimit_ = 0;
    int firstOffset_ = 0;
    int endOffset_ = 0;
    int viewportOffset_ = 0;
    bool bandsKnown_ = false;
    bool partial_ = false;
    bool ultraLong_ = false;
    Status status_ = Status::Empty;
};
} // namespace h2d
