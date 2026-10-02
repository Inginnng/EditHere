#include "scrollstitch.h"
#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace h2d {
namespace {
constexpr int kColumns = 96;
constexpr int kRows = 64;
constexpr double kInvalid = 1.0e9;

int difference(QRgb a, QRgb b) {
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
           std::abs(qBlue(a) - qBlue(b));
}
const QRgb *row(const QImage &image, int y) {
    return reinterpret_cast<const QRgb *>(image.constScanLine(y));
}
int sideMargin(int width) { return std::min(8, std::max(1, width / 200)); }

// Direct-coordinate differences identify moving columns. This also keeps fixed
// sidebars and the scrollbar out of the displacement estimate. Only matching is
// cropped: output slices always retain the entire selected width.
std::pair<int, int> movingColumns(const QImage &a, const QImage &b) {
    const int margin = sideMargin(a.width());
    const int right = a.width() - margin;
    int first = right, last = margin;
    for (int x = margin; x < right; x += std::max(1, a.width() / 384)) {
        int changed = 0;
        for (int y = 0; y < a.height(); y += std::max(1, a.height() / 192)) {
            if (difference(row(a, y)[x], row(b, y)[x]) > 36)
                ++changed;
        }
        if (changed >= 3) {
            first = std::min(first, x);
            last = x;
        }
    }
    if (last - first < 4)
        return {margin, right};
    return {std::max(margin, first - 2), std::min(right, last + 3)};
}

struct Samples {
    int height = 0;
    int columns = 0;
    std::vector<QRgb> colors;
    std::vector<unsigned char> textured;
    std::vector<int> rowEvidence;
    Samples(const QImage &image, int left, int right) : height(image.height()) {
        columns = std::min(kColumns, right - left);
        colors.resize(size_t(height) * columns);
        textured.resize(colors.size());
        rowEvidence.resize(height);
        for (int y = 0; y < height; ++y) {
            const QRgb *scan = row(image, y);
            const QRgb *nearby = row(image, std::min(y + 1, height - 1));
            const QRgb *above = row(image, std::max(0, y - 1));
            for (int column = 0; column < columns; ++column) {
                const int x = left + (column * (right - left - 1)) / std::max(1, columns - 1);
                const QRgb color = scan[x];
                const int contrast = std::max({difference(color, scan[std::min(x + 2, image.width() - 1)]),
                                               difference(color, scan[std::max(x - 2, 0)]),
                                               difference(color, nearby[x]), difference(color, above[x])});
                const size_t index = size_t(y) * columns + column;
                colors[index] = color;
                textured[index] = contrast > 36;
                rowEvidence[y] += textured[index];
            }
        }
    }
};

struct Evaluation {
    double score = kInvalid;
    int evidence = 0;
};

// Empty white space contributes no evidence. Error is measured separately in a
// 4 x 4 grid; a small number of outlier tiles may contain a caret or animation.
// Sample informative rows rather than a regular grid: a page may contain one
// paragraph surrounded by white space. White pixels alone never prove a match.
Evaluation evaluate(const Samples &a, const Samples &b, int overlap) {
    std::array<double, 16> sums{};
    std::array<int, 16> counts{};
    const int start = a.height - overlap;
    std::vector<int> informative;
    informative.reserve(overlap);
    for (int y = 0; y < overlap; ++y) {
        if (a.rowEvidence[start + y] || b.rowEvidence[y])
            informative.push_back(y);
    }
    const int countRows = std::min(kRows, int(informative.size()));
    if (countRows < 3)
        return {};
    for (int sample = 0; sample < countRows; ++sample) {
        const int y = informative[sample * (informative.size() - 1) / std::max(1, countRows - 1)];
        const size_t offsetA = size_t(start + y) * a.columns;
        const size_t offsetB = size_t(y) * b.columns;
        const int vertical = y * 4 / overlap;
        for (int x = 0; x < a.columns; ++x) {
            if (!a.textured[offsetA + x] && !b.textured[offsetB + x])
                continue;
            const int tile = vertical * 4 + x * 4 / a.columns;
            sums[tile] += difference(a.colors[offsetA + x], b.colors[offsetB + x]) / 3.0;
            ++counts[tile];
        }
    }
    std::vector<int> active;
    for (int tile = 0; tile < 16; ++tile) {
        if (counts[tile] >= 4)
            active.push_back(tile);
    }
    if (active.empty())
        return {};
    std::sort(active.begin(), active.end(), [&](int lhs, int rhs) {
        return sums[lhs] / counts[lhs] < sums[rhs] / counts[rhs];
    });
    const int discard = active.size() >= 12 ? 2 : (active.size() >= 5 ? 1 : 0);
    double total = 0.0;
    int evidence = 0;
    for (size_t index = 0; index < active.size() - discard; ++index) {
        const int tile = active[index];
        total += sums[tile];
        evidence += counts[tile];
    }
    if (evidence < 32)
        return {};
    return {total / evidence, evidence};
}

bool exactVerticalOverlap(const QImage &a, const QImage &b, int overlap, int left, int right) {
    const int start = a.height() - overlap;
    for (int y = 0; y < overlap; ++y) {
        if (!std::equal(row(a, start + y) + left, row(a, start + y) + right, row(b, y) + left))
            return false;
    }
    return true;
}

bool rowMoved(const QImage &a, const QImage &b, int y, double tolerance) {
    const int margin = sideMargin(a.width());
    int changed = 0, samples = 0;
    double total = 0.0;
    for (int x = margin; x < a.width() - margin; x += std::max(1, a.width() / 128)) {
        const double diff = difference(row(a, y)[x], row(b, y)[x]) / 3.0;
        total += diff;
        changed += diff > std::max(8.0, tolerance * 2.0);
        ++samples;
    }
    return changed >= 2 || (samples && total / samples > tolerance);
}

// A blank margin is not a fixed toolbar. Require texture or a background colour
// different from the middle of the viewport before removing a static edge band.
bool meaningfulBand(const QImage &image, int begin, int end) {
    if (end <= begin)
        return false;
    const int margin = sideMargin(image.width());
    const QRgb center = row(image, image.height() / 2)[margin];
    int textureRows = 0;
    int differentBackgroundRows = 0;
    for (int y = begin; y < end; ++y) {
        const QRgb *scan = row(image, y);
        bool texture = false;
        int otherBackground = 0;
        int samples = 0;
        for (int x = margin; x < image.width() - margin; x += std::max(1, image.width() / 96)) {
            texture |= difference(scan[x], scan[std::min(x + 2, image.width() - 1)]) > 36;
            otherBackground += difference(scan[x], center) > 45;
            ++samples;
        }
        textureRows += texture;
        differentBackgroundRows += samples && otherBackground > samples / 2;
    }
    return textureRows >= 3 || differentBackgroundRows >= std::min(4, end - begin);
}

bool blankBodyRow(const QImage &image, int y) {
    const int margin = sideMargin(image.width());
    const QRgb background = row(image, image.height() / 2)[margin];
    for (int x = margin; x < image.width() - margin; x += std::max(1, image.width() / 128)) {
        if (difference(row(image, y)[x], background) > 18)
            return false;
    }
    return true;
}

bool stableBand(const QImage &a, const QImage &b, int begin, int end) {
    if (end <= begin)
        return true;
    int changedRows = 0;
    for (int y = begin; y < end; ++y)
        changedRows += rowMoved(a, b, y, 6.0);
    return changedRows <= std::max(2, (end - begin) / 15);
}
} // namespace

double frameDifference(const QImage &left, const QImage &right) {
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return kInvalid;
    const QImage a = left.convertToFormat(QImage::Format_ARGB32);
    const QImage b = right.convertToFormat(QImage::Format_ARGB32);
    double total = 0.0;
    int samples = 0;
    for (int y = 0; y < a.height(); y += std::max(1, a.height() / 96)) {
        for (int x = 0; x < a.width(); x += std::max(1, a.width() / 128)) {
            total += difference(row(a, y)[x], row(b, y)[x]) / 3.0;
            ++samples;
        }
    }
    return samples ? total / samples : kInvalid;
}

bool equivalentScrollFrames(const QImage &left, const QImage &right) {
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return false;
    const QImage a = left.convertToFormat(QImage::Format_ARGB32);
    const QImage b = right.convertToFormat(QImage::Format_ARGB32);
    std::array<int, 64> changedTiles{};
    int changed = 0, samples = 0;
    int leftChanged = a.width(), rightChanged = -1;
    int topChanged = a.height(), bottomChanged = -1;
    for (int y = 0; y < a.height(); y += std::max(1, a.height() / 96)) {
        for (int x = 0; x < a.width(); x += std::max(1, a.width() / 128)) {
            if (difference(row(a, y)[x], row(b, y)[x]) > 30) {
                ++changed;
                ++changedTiles[(y * 8 / a.height()) * 8 + x * 8 / a.width()];
                leftChanged = std::min(leftChanged, x);
                rightChanged = std::max(rightChanged, x);
                topChanged = std::min(topChanged, y);
                bottomChanged = std::max(bottomChanged, y);
            }
            ++samples;
        }
    }
    if (!changed)
        return true;
    int active = 0;
    for (int count : changedTiles)
        active += count >= 2;
    if (changed > samples / 40 || active > 6 ||
        rightChanged - leftChanged > std::max(24, a.width() / 4) ||
        bottomChanged - topChanged > std::max(16, a.height() / 8))
        return false;
    // A short line on an otherwise empty page may move without changing even
    // 1% of the viewport. A local animation requires stationary texture outside
    // its changed rectangle; white background alone cannot prove no movement.
    int stationary = 0;
    for (int y = 0; y < a.height(); y += std::max(1, a.height() / 96)) {
        for (int x = 0; x < a.width(); x += std::max(1, a.width() / 128)) {
            if (x >= leftChanged && x <= rightChanged && y >= topChanged && y <= bottomChanged)
                continue;
            const QRgb color = row(a, y)[x];
            const int contrast = std::max(
                difference(color, row(a, y)[std::min(x + 2, a.width() - 1)]),
                difference(color, row(a, std::min(y + 1, a.height() - 1))[x]));
            if (contrast > 36 && difference(color, row(b, y)[x]) <= 30)
                ++stationary;
        }
    }
    return stationary >= 32;
}

ScrollBands fixedBands(const QImage &previous, const QImage &next, double tolerance) {
    ScrollBands bands;
    if (previous.isNull() || next.isNull() || previous.size() != next.size())
        return bands;
    const QImage a = previous.convertToFormat(QImage::Format_ARGB32);
    const QImage b = next.convertToFormat(QImage::Format_ARGB32);
    const int limit = a.height() / 3;
    while (bands.top < limit && !rowMoved(a, b, bands.top, tolerance))
        ++bands.top;
    while (bands.bottom < limit && !rowMoved(a, b, a.height() - 1 - bands.bottom, tolerance))
        ++bands.bottom;
    if (bands.top == limit || !meaningfulBand(a, 0, bands.top))
        bands.top = 0;
    if (bands.bottom == limit || !meaningfulBand(a, a.height() - bands.bottom, a.height()))
        bands.bottom = 0;
    // White body rows adjacent to a coloured toolbar can happen to repeat. They
    // are not part of the toolbar, and retaining them would remove page content.
    while (bands.top && blankBodyRow(a, bands.top - 1))
        --bands.top;
    while (bands.bottom && blankBodyRow(a, a.height() - bands.bottom))
        --bands.bottom;
    return bands;
}

OverlapMatch matchVerticalOverlap(const QImage &current, const QImage &next, int minOverlap,
                                  int maxOverlap, double tolerance) {
    OverlapMatch result;
    if (current.isNull() || next.isNull() || current.width() != next.width() || current.width() < 8)
        return result;
    const int limit = std::min({maxOverlap, current.height(), next.height()});
    const int minimum = std::max(8, minOverlap);
    if (limit < minimum)
        return result;
    const int retained = std::min(current.height(), next.height());
    const QImage a = current.copy(0, current.height() - retained, current.width(), retained)
                         .convertToFormat(QImage::Format_ARGB32);
    const QImage b = next.convertToFormat(QImage::Format_ARGB32);
    const auto columns = a.height() == b.height() ? movingColumns(a, b) :
        std::pair<int, int>{sideMargin(a.width()), a.width() - sideMargin(a.width())};
    const Samples sa(a, columns.first, columns.second), sb(b, columns.first, columns.second);
    std::vector<std::pair<double, int>> candidates;
    candidates.reserve(limit - minimum + 1);
    for (int overlap = minimum; overlap <= limit; ++overlap) {
        const Evaluation candidate = evaluate(sa, sb, overlap);
        if (candidate.score < kInvalid)
            candidates.emplace_back(candidate.score, overlap);
    }
    if (candidates.empty())
        return result;
    std::sort(candidates.begin(), candidates.end());
    result.score = candidates.front().first;
    result.overlap = candidates.front().second;
    if (result.score > tolerance)
        return result;
    double runnerUp = kInvalid;
    bool nearTie = false;
    for (size_t index = 1; index < candidates.size(); ++index) {
        const auto [score, overlap] = candidates[index];
        if (std::abs(overlap - result.overlap) >= 3)
            runnerUp = std::min(runnerUp, score);
        nearTie |= score <= result.score + 0.15;
    }
    if (nearTie || runnerUp <= result.score + std::max(1.8, result.score * 0.6)) {
        // Lists may repeat almost every pixel while a small icon or row number
        // identifies the displacement. When their average error is too similar,
        // require a unique exact match across every pixel in the moving region.
        // Repeated patterns still have several exact matches and remain rejected.
        int exactCount = 0, exactOverlap = 0;
        if (result.score == 0.0) {
            for (const auto &[score, overlap] : candidates) {
                if (score > 0.15)
                    break;
                if (exactVerticalOverlap(a, b, overlap, columns.first, columns.second)) {
                    ++exactCount;
                    exactOverlap = overlap;
                }
            }
        }
        if (exactCount == 1) {
            result.found = true;
            result.overlap = exactOverlap;
            result.score = 0.0;
            result.confidence = 1.0;
            return result;
        }
        result.ambiguous = true;
        return result;
    }
    result.found = true;
    result.confidence = runnerUp == kInvalid ? 1.0 :
        std::clamp((runnerUp - result.score) / std::max(1.0, runnerUp), 0.0, 1.0);
    return result;
}

int appendScrolledFrame(QImage *picture, const QImage &frame, int minOverlap, double tolerance) {
    if (!picture || picture->isNull() || frame.isNull())
        return -1;
    const auto match = matchVerticalOverlap(*picture, frame, minOverlap, frame.height(), tolerance);
    if (!match.found)
        return -1;
    const int added = frame.height() - match.overlap;
    if (added <= 0)
        return 0;
    const qint64 height = qint64(picture->height()) + added;
    if (height > ScrollStitcher::maxHeight() || height * frame.width() > ScrollStitcher::maxPixels())
        return -1;
    QImage grown(frame.width(), int(height), QImage::Format_ARGB32);
    if (grown.isNull())
        return -1;
    QPainter painter(&grown);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, *picture);
    painter.drawImage(QRect(0, picture->height(), frame.width(), added), frame,
                      QRect(0, match.overlap, frame.width(), added));
    painter.end();
    *picture = grown;
    return added;
}

void ScrollStitcher::reset(const QImage &first) {
    previous_ = {};
    slices_.clear();
    bottom_ = {};
    cached_ = {};
    bands_ = {};
    height_ = heightLimit_ = 0;
    bandsKnown_ = partial_ = false;
    status_ = Status::Empty;
    if (first.isNull() || first.width() < 8 || first.height() < 32 || first.width() > maxHeight() ||
        first.height() > maxHeight() || qint64(first.width()) * first.height() > maxPixels())
        return;
    previous_ = first.convertToFormat(QImage::Format_ARGB32);
    previous_.setDevicePixelRatio(1.0);
    slices_.append(previous_);
    height_ = previous_.height();
    heightLimit_ = std::min(maxHeight(), int(maxPixels() / previous_.width()));
}

int ScrollStitcher::add(const QImage &frame) {
    if (previous_.isNull() || frame.isNull() || frame.size() != previous_.size()) {
        status_ = Status::GeometryChanged;
        return -1;
    }
    if (atLimit()) {
        status_ = Status::Limit;
        return -1;
    }
    QImage next = frame.convertToFormat(QImage::Format_ARGB32);
    next.setDevicePixelRatio(1.0);
    if (previous_ == next) {
        status_ = Status::Repeat;
        return 0;
    }
    const ScrollBands bands = bandsKnown_ ? bands_ : fixedBands(previous_, next);
    const int contentHeight = next.height() - bands.top - bands.bottom;
    const QImage current = previous_.copy(0, bands.top, next.width(), contentHeight);
    const QImage incoming = next.copy(0, bands.top, next.width(), contentHeight);
    const int minimum = std::max(32, contentHeight / 5);
    const auto match = matchVerticalOverlap(current, incoming, minimum, contentHeight, 8.0);
    if (!match.found) {
        // A reliable nonzero displacement takes precedence over animation
        // tolerance. Sparse moving text can affect far fewer than 2% of pixels.
        if (!match.ambiguous && equivalentScrollFrames(previous_, next)) {
            status_ = Status::Repeat;
            return 0;
        }
        status_ = match.ambiguous ? Status::Ambiguous : Status::NoMatch;
        return -1;
    }
    const int added = contentHeight - match.overlap;
    if (!added) {
        status_ = Status::Repeat;
        return 0;
    }
    if (height_ + qint64(added) > heightLimit_) {
        status_ = Status::Limit;
        return -1;
    }
    const QImage slice = next.copy(0, next.height() - bands.bottom - added, next.width(), added);
    const QImage footer = bands.bottom ? next.copy(0, next.height() - bands.bottom, next.width(), bands.bottom) : QImage();
    if (slice.isNull() || (bands.bottom && footer.isNull())) {
        status_ = Status::Limit;
        return -1;
    }
    if (!bandsKnown_ && bands.bottom)
        slices_[0] = previous_.copy(0, 0, next.width(), next.height() - bands.bottom);
    slices_.append(slice);
    bottom_ = footer;
    height_ += added;
    bands_ = bands;
    bandsKnown_ = true;
    partial_ |= match.score > 1.0;
    previous_ = next;
    cached_ = {};
    status_ = Status::Added;
    return added;
}

QImage ScrollStitcher::picture() const {
    if (slices_.isEmpty())
        return {};
    if (!cached_.isNull())
        return cached_;
    if (slices_.size() == 1 && bottom_.isNull())
        return slices_.front();
    QImage result(previous_.width(), height_, QImage::Format_ARGB32);
    if (result.isNull())
        return {};
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int y = 0;
    for (const QImage &slice : slices_) {
        painter.drawImage(0, y, slice);
        y += slice.height();
    }
    if (!bottom_.isNull())
        painter.drawImage(0, y, bottom_);
    painter.end();
    cached_ = result;
    return cached_;
}
} // namespace h2d
