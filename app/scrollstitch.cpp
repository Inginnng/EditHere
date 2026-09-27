#include "scrollstitch.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace h2d {
namespace {
// A long capture is compared sparsely. Rows and columns are both sampled because a
// page of text differs along both axes, and neither direction needs every pixel to
// be recognised.
constexpr int kColumnSamples = 128;
constexpr int kRowSamples = 64;
} // namespace

namespace {
// How different two rows may be and still count as "the same row". It is the same
// unit as OverlapMatch::score: an average difference per channel.
double rowDifference(const QImage &left, int leftRow, const QImage &right, int rightRow) {
    const auto *a = reinterpret_cast<const QRgb *>(left.constScanLine(leftRow));
    const auto *b = reinterpret_cast<const QRgb *>(right.constScanLine(rightRow));
    const int step = std::max(1, left.width() / kColumnSamples);
    double total = 0;
    int samples = 0;
    for (int column = 0; column < left.width(); column += step) {
        total += std::abs(qRed(a[column]) - qRed(b[column]));
        total += std::abs(qGreen(a[column]) - qGreen(b[column]));
        total += std::abs(qBlue(a[column]) - qBlue(b[column]));
        samples += 3;
    }
    return samples == 0 ? 0.0 : total / samples;
}
} // namespace

double frameDifference(const QImage &left, const QImage &right) {
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return 1.0e9;
    const QImage a = left.convertToFormat(QImage::Format_ARGB32);
    const QImage b = right.convertToFormat(QImage::Format_ARGB32);
    const int rowStep = std::max(1, a.height() / kRowSamples);
    double total = 0;
    int rows = 0;
    for (int row = 0; row < a.height(); row += rowStep) {
        total += rowDifference(a, row, b, row);
        ++rows;
    }
    return rows == 0 ? 1.0e9 : total / rows;
}

ScrollBands fixedBands(const QImage &previous, const QImage &next, double tolerance) {
    ScrollBands bands;
    if (previous.isNull() || next.isNull() || previous.size() != next.size())
        return bands;
    const QImage left = previous.convertToFormat(QImage::Format_ARGB32);
    const QImage right = next.convertToFormat(QImage::Format_ARGB32);
    // Nothing can be trusted past half the region: a band that wide means the page
    // did not move at all rather than that it has a very tall header.
    const int limit = left.height() / 2;
    for (int row = 0; row < limit; ++row) {
        if (rowDifference(left, row, right, row) > tolerance)
            break;
        bands.top = row + 1;
    }
    for (int row = 0; row < limit && bands.top + row + 1 <= left.height(); ++row) {
        if (rowDifference(left, left.height() - 1 - row, right, right.height() - 1 - row) > tolerance)
            break;
        bands.bottom = row + 1;
    }
    return bands;
}

OverlapMatch matchVerticalOverlap(const QImage &current, const QImage &next, int minOverlap,
                                  int maxOverlap, double tolerance) {
    OverlapMatch best;
    if (current.isNull() || next.isNull() || current.width() != next.width())
        return best;
    const int limit = std::min({maxOverlap, current.height(), next.height()});
    if (limit <= 0)
        return best;
    const int floor = std::clamp(minOverlap, 1, limit);
    const QImage top = current.convertToFormat(QImage::Format_ARGB32);
    const QImage bottom = next.convertToFormat(QImage::Format_ARGB32);
    const int columnStep = std::max(1, top.width() / kColumnSamples);
    const int rowStep = std::max(1, limit / kRowSamples);
    for (int overlap = limit; overlap >= floor; --overlap) {
        double total = 0;
        int samples = 0;
        for (int row = 0; row < overlap; row += rowStep) {
            const auto *above = reinterpret_cast<const QRgb *>(top.constScanLine(top.height() - overlap + row));
            const auto *below = reinterpret_cast<const QRgb *>(bottom.constScanLine(row));
            for (int column = 0; column < top.width(); column += columnStep) {
                total += std::abs(qRed(above[column]) - qRed(below[column]));
                total += std::abs(qGreen(above[column]) - qGreen(below[column]));
                total += std::abs(qBlue(above[column]) - qBlue(below[column]));
                samples += 3;
            }
        }
        if (samples == 0)
            continue;
        const double score = total / samples;
        if (!best.found || score < best.score) {
            best.found = true;
            best.overlap = overlap;
            best.score = score;
        }
    }
    if (!best.found || best.score > tolerance)
        return OverlapMatch{};
    return best;
}

int appendScrolledFrame(QImage *picture, const QImage &frame, int minOverlap, double tolerance) {
    if (picture == nullptr || picture->isNull() || frame.isNull() || picture->width() != frame.width())
        return -1;
    if (frame.height() < minOverlap)
        return -1;
    const QImage base = picture->convertToFormat(QImage::Format_ARGB32);
    const auto match = matchVerticalOverlap(base, frame, minOverlap, base.height(), tolerance);
    if (!match.found)
        return -1;
    // A frame that repeats the whole picture is what a page that has stopped
    // scrolling looks like, and there is nothing to add.
    if (match.overlap >= base.height())
        return -1;
    const int added = frame.height() - match.overlap;
    if (added <= 0)
        return -1;
    QImage grown(base.width(), base.height() + added, QImage::Format_ARGB32);
    QPainter painter(&grown);
    painter.drawImage(0, 0, base);
    // Only the part below the shared rows is new, so the seam keeps the pixels that
    // were captured first.
    painter.drawImage(QRect(0, base.height(), frame.width(), added), frame,
                      QRect(0, match.overlap, frame.width(), added));
    painter.end();
    *picture = grown;
    return added;
}

namespace {
// The tolerances a frame is tried at, in order. The first one only accepts rows that
// are practically identical; the later ones are for a region that has something
// moving in it, where a rough placement is better than giving up on the whole page.
constexpr double kTolerances[] = {6.0, 18.0, 48.0};
} // namespace

int ScrollStitcher::maxHeight() {
    return 20000;
}

void ScrollStitcher::reset(const QImage &first) {
    first_ = first.convertToFormat(QImage::Format_ARGB32);
    previous_ = first_;
    body_ = first_;
    bottom_ = {};
    bands_ = {};
    stripped_ = false;
    partial_ = false;
}

int ScrollStitcher::add(const QImage &frame) {
    if (body_.isNull() || frame.isNull() || frame.size() != previous_.size())
        return -1;
    if (body_.height() >= maxHeight())
        return -1;
    const QImage next = frame.convertToFormat(QImage::Format_ARGB32);
    const ScrollBands bands = fixedBands(previous_, next);
    // A frame that repeats the previous one edge to edge has not scrolled, and there
    // is nothing to place.
    if (bands.top + bands.bottom >= next.height())
        return -1;
    const int bottom = std::clamp(bands.bottom, 0, next.height() - bands.top - 1);
    body_ = body_.convertToFormat(QImage::Format_ARGB32);
    // The first frame was grabbed before any band was known about, so the footer it
    // carries is taken off the moment the first pair of frames reveals one.
    if (!stripped_) {
        body_ = first_.copy(0, 0, first_.width(), first_.height() - bottom);
        stripped_ = true;
    }
    if (bottom > 0)
        bottom_ = next.copy(0, next.height() - bottom, next.width(), bottom);
    bands_ = bands;
    // The fixed bands are taken off both ends: the header would never line up with
    // the rows the picture already ends on, and the footer is put back once at the
    // end instead of being repeated after every frame.
    const QImage inner = next.copy(0, bands.top, next.width(), next.height() - bands.top - bottom);
    int added = -1;
    for (double tolerance : kTolerances) {
        QImage grown = body_;
        added = appendScrolledFrame(&grown, inner, 8, tolerance);
        if (added > 0) {
            body_ = grown;
            if (tolerance != kTolerances[0])
                partial_ = true;
            break;
        }
    }
    if (added <= 0)
        return -1;
    previous_ = next;
    return added;
}

QImage ScrollStitcher::picture() const {
    if (bottom_.isNull() || bottom_.height() == 0)
        return body_;
    if (body_.isNull())
        return bottom_;
    QImage joined(body_.width(), body_.height() + bottom_.height(), QImage::Format_ARGB32);
    QPainter painter(&joined);
    painter.drawImage(0, 0, body_);
    painter.drawImage(0, body_.height(), bottom_);
    painter.end();
    return joined;
}
} // namespace h2d
