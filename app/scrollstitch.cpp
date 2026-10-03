#include "scrollstitch.h"
#include <QPainter>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <cstring>
#include <map>
#include <unordered_map>

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

// The quick grid can miss a small row number between its sample columns. Use
// native pixels to resolve its close candidates. A moving caret or animation
// may be ignored only when all substantial changes form one small local patch;
// differences spread through repeated list rows remain evidence of a bad match.
Evaluation verifyNativeOverlap(const QImage &a, int firstA, const QImage &b, int firstB,
                               int height, int left, int right) {
    int changed = 0, x1 = right, x2 = left, y1 = height, y2 = -1;
    for (int y = 0; y < height; ++y) {
        const QRgb *scanA = row(a, firstA + y), *scanB = row(b, firstB + y);
        for (int x = left; x < right; ++x) {
            if (difference(scanA[x], scanB[x]) > 45) {
                ++changed;
                x1 = std::min(x1, x); x2 = std::max(x2, x);
                y1 = std::min(y1, y); y2 = std::max(y2, y);
            }
        }
    }
    const bool local = changed && changed <= qint64(right - left) * height / 40 &&
        x2 - x1 + 1 <= std::max(24, (right - left) / 4) &&
        y2 - y1 + 1 <= std::max(16, height / 8);
    double total = 0.0;
    std::array<std::array<int, 511>, 3> channelDeltas{};
    int evidence = 0, informativeRows = 0;
    for (int y = 0; y < height; ++y) {
        const int ya = firstA + y, yb = firstB + y;
        const QRgb *scanA = row(a, ya), *scanB = row(b, yb);
        const QRgb *nearA = row(a, std::min(ya + 1, a.height() - 1));
        const QRgb *nearB = row(b, std::min(yb + 1, b.height() - 1));
        int rowEvidence = 0;
        for (int x = left; x < right; ++x) {
            if (local && x >= x1 - 1 && x <= x2 + 1 && y >= y1 - 1 && y <= y2 + 1)
                continue;
            const int nextX = std::min(x + 2, right - 1);
            const int contrast = std::max({difference(scanA[x], scanA[nextX]),
                difference(scanB[x], scanB[nextX]), difference(scanA[x], nearA[x]),
                difference(scanB[x], nearB[x])});
            if (contrast <= 36) continue;
            total += difference(scanA[x], scanB[x]) / 3.0;
            ++channelDeltas[0][qRed(scanB[x]) - qRed(scanA[x]) + 255];
            ++channelDeltas[1][qGreen(scanB[x]) - qGreen(scanA[x]) + 255];
            ++channelDeltas[2][qBlue(scanB[x]) - qBlue(scanA[x]) + 255];
            ++rowEvidence;
        }
        evidence += rowEvidence;
        informativeRows += rowEvidence > 0;
    }
    if (evidence < 32 || informativeRows < 3) return {};
    std::array<int, 3> bias{};
    for (int channel = 0; channel < 3; ++channel) {
        int accumulated = 0;
        for (int value = 0; value < 511; ++value) {
            accumulated += channelDeltas[channel][value];
            if (accumulated >= (evidence + 1) / 2) {
                if (std::abs(value - 255) <= 3) bias[channel] = value - 255;
                break;
            }
        }
    }
    // Small uniform raster colour changes must not bury the distinctive digits
    // in a list's error baseline. Larger colour changes are never normalized.
    if (bias[0] || bias[1] || bias[2]) {
        total = 0.0;
        for (int y = 0; y < height; ++y) {
            const int ya = firstA + y, yb = firstB + y;
            const QRgb *scanA = row(a, ya), *scanB = row(b, yb);
            const QRgb *nearA = row(a, std::min(ya + 1, a.height() - 1));
            const QRgb *nearB = row(b, std::min(yb + 1, b.height() - 1));
            for (int x = left; x < right; ++x) {
                if (local && x >= x1 - 1 && x <= x2 + 1 && y >= y1 - 1 && y <= y2 + 1) continue;
                const int nextX = std::min(x + 2, right - 1);
                const int contrast = std::max({difference(scanA[x], scanA[nextX]),
                    difference(scanB[x], scanB[nextX]), difference(scanA[x], nearA[x]),
                    difference(scanB[x], nearB[x])});
                if (contrast <= 36) continue;
                total += (std::abs(qRed(scanB[x]) - std::clamp(qRed(scanA[x]) + bias[0], 0, 255)) +
                          std::abs(qGreen(scanB[x]) - std::clamp(qGreen(scanA[x]) + bias[1], 0, 255)) +
                          std::abs(qBlue(scanB[x]) - std::clamp(qBlue(scanA[x]) + bias[2], 0, 255))) / 3.0;
            }
        }
    }
    return {total / evidence, evidence};
}

// These keys are an index into retained native rows, never a replacement for
// the image. Several textured rows propose a location; complete overlap pixels
// across slice boundaries must then uniquely verify it.
quint64 nativeRowKey(const QImage &image, int y, int *evidence = nullptr) {
    const int left = sideMargin(image.width()), right = image.width() - left;
    const QRgb *scan = row(image, y);
    const QRgb *nearby = row(image, std::min(y + 1, image.height() - 1));
    int texture = 0;
    quint64 key = 1469598103934665603ULL;
    for (int x = left; x < right; ++x) {
        texture += std::max(difference(scan[x], scan[std::min(x + 2, right - 1)]),
                            difference(scan[x], nearby[x])) > 36;
        key = (key ^ quint64(scan[x] & 0x00ffffff)) * 1099511628211ULL;
    }
    if (evidence) *evidence = texture;
    return texture >= 2 ? (key ? key : 1) : 0;
}

quint64 nativeRowSignature(const QImage &image, int y) {
    const int left = sideMargin(image.width()), span = image.width() - left * 2;
    const QRgb *scan = row(image, y);
    const QRgb *nearby = row(image, std::min(y + 1, image.height() - 1));
    quint64 signature = 0;
    for (int group = 0; group < 8; ++group) {
        const int begin = left + group * span / 8, end = left + (group + 1) * span / 8;
        if (end <= begin) continue;
        int luma = 0, edges = 0;
        for (int x = begin; x < end; ++x) {
            luma += (77 * qRed(scan[x]) + 150 * qGreen(scan[x]) + 29 * qBlue(scan[x])) >> 8;
            edges += std::max(difference(scan[x], scan[std::min(x + 2, left + span - 1)]),
                               difference(scan[x], nearby[x])) > 36;
        }
        const int mean = std::min(31, (luma / (end - begin) + 4) / 8);
        const int density = std::min(7, edges * 8 / (end - begin));
        signature |= quint64((mean << 3) | density) << (group * 8);
    }
    return signature;
}

bool similarRowSignature(quint64 a, quint64 b, const std::vector<int> &groups) {
    for (int group : groups) {
        const int first = int((a >> (group * 8)) & 255), second = int((b >> (group * 8)) & 255);
        if (std::abs((first >> 3) - (second >> 3)) > 1 ||
            std::abs((first & 7) - (second & 7)) > 1) return false;
    }
    return !groups.empty();
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
QRgb bodyBackground(const QImage &image, int left, int right) {
    const QRgb *scan = row(image, image.height() / 2);
    std::unordered_map<QRgb, int> counts;
    QRgb background = scan[left];
    int most = 0;
    for (int x = left; x < right; x += std::max(1, (right - left) / 96)) {
        const int count = ++counts[scan[x]];
        if (count > most) { most = count; background = scan[x]; }
    }
    return background;
}

bool meaningfulBand(const QImage &image, int begin, int end, int left, int right) {
    if (end <= begin)
        return false;
    const QRgb center = bodyBackground(image, left, right);
    int textureRows = 0;
    int differentBackgroundRows = 0;
    for (int y = begin; y < end; ++y) {
        const QRgb *scan = row(image, y);
        bool texture = false;
        int otherBackground = 0;
        int samples = 0;
        for (int x = left; x < right; x += std::max(1, image.width() / 96)) {
            texture |= difference(scan[x], scan[std::min(x + 2, image.width() - 1)]) > 36;
            otherBackground += difference(scan[x], center) > 45;
            ++samples;
        }
        textureRows += texture;
        differentBackgroundRows += samples && otherBackground > samples / 2;
    }
    return textureRows >= 3 || differentBackgroundRows >= std::min(4, end - begin);
}

bool blankBodyRow(const QImage &image, int y, int left, int right) {
    const QRgb background = bodyBackground(image, left, right);
    for (int x = left; x < right; x += std::max(1, image.width() / 128)) {
        if (difference(row(image, y)[x], background) > 18)
            return false;
    }
    return true;
}

// A coincidentally stationary separator above a coloured toolbar is page
// content. Its blank side margins distinguish it from the toolbar background,
// even when the separator itself prevents blankBodyRow() from recognising it.
bool bodyMarginsBesideColouredBar(const QImage &image, int y, int edge, int left, int right) {
    const QRgb background = bodyBackground(image, left, right);
    const int margin = sideMargin(image.width());
    // The moving range can start inside a document separator. Also inspect the
    // original outer margins; one may be hidden by a fixed vertical sidebar,
    // while the other still proves that a stationary separator is body content.
    for (int x : {left, right - 1, margin, image.width() - margin - 1}) {
        if (difference(row(image, edge)[x], background) > 45 &&
            difference(row(image, y)[x], background) <= 18) return true;
    }
    return false;
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
    const auto columns = movingColumns(a, b);
    const int margin = sideMargin(a.width());
    // Matching pads its moving range by a few pixels to retain glyph edges.
    // Classification uses the interior so that padding cannot sample a sidebar.
    const int left = columns.first > margin ? columns.first + 2 : margin;
    const int right = std::max(left + 1, columns.second < a.width() - margin ?
                              columns.second - 3 : a.width() - margin);
    const int limit = a.height() / 3;
    while (bands.top < limit && !rowMoved(a, b, bands.top, tolerance))
        ++bands.top;
    while (bands.bottom < limit && !rowMoved(a, b, a.height() - 1 - bands.bottom, tolerance))
        ++bands.bottom;
    if (bands.top == limit || !meaningfulBand(a, 0, bands.top, left, right))
        bands.top = 0;
    if (bands.bottom == limit || !meaningfulBand(a, a.height() - bands.bottom, a.height(), left, right))
        bands.bottom = 0;
    // White body rows adjacent to a coloured toolbar can happen to repeat. They
    // are not part of the toolbar, and retaining them would remove page content.
    // A stationary vertical sidebar is not evidence of a horizontal header or
    // footer. Inspect only the moving content when classifying these bands.
    while (bands.top && (blankBodyRow(a, bands.top - 1, left, right) ||
                        bodyMarginsBesideColouredBar(a, bands.top - 1, 0, left, right)))
        --bands.top;
    while (bands.bottom && (blankBodyRow(a, a.height() - bands.bottom, left, right) ||
                           bodyMarginsBesideColouredBar(a, a.height() - bands.bottom, a.height() - 1,
                                                     left, right)))
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
        const double plausible = result.score + std::max(1.8, result.score * 0.6);
        std::vector<std::pair<double, int>> verified;
        for (const auto &[score, overlap] : candidates) {
            if (score > plausible) break;
            // A huge family of indistinguishable shifts has no reliable answer.
            if (verified.size() >= 32) { result.ambiguous = true; return result; }
            const auto dense = verifyNativeOverlap(a, a.height() - overlap, b, 0,
                                                   overlap, columns.first, columns.second);
            if (dense.score < kInvalid) verified.emplace_back(dense.score, overlap);
        }
        if (verified.empty()) return result;
        std::sort(verified.begin(), verified.end());
        result.score = verified.front().first;
        result.overlap = verified.front().second;
        if (result.score > tolerance) return result;
        runnerUp = kInvalid;
        for (size_t index = 1; index < verified.size(); ++index) {
            if (std::abs(verified[index].second - result.overlap) >= 3)
                runnerUp = std::min(runnerUp, verified[index].first);
        }
        // The dense score includes the small distinguishing glyphs, so it can
        // resolve much smaller differences than the quick sample grid.
        if (runnerUp <= result.score + std::max(0.02, result.score * 0.35)) {
            result.ambiguous = true;
            return result;
        }
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

ScrollStitcher::Slice::~Slice() {
    if (!path.isEmpty()) QFile::remove(path);
}

QImage ScrollStitcher::Slice::load() const {
    return path.isEmpty() ? image : QImage(path).convertToFormat(QImage::Format_ARGB32);
}

QSharedPointer<ScrollStitcher::Slice> ScrollStitcher::storeSlice(const QImage &image) {
    if (image.isNull()) return {};
    auto slice = QSharedPointer<Slice>::create();
    slice->height = image.height();
    slice->rowKeys.resize(image.height());
    slice->rowSignatures.resize(image.height());
    for (int y = 0; y < image.height(); ++y) {
        slice->rowKeys[y] = nativeRowKey(image, y);
        slice->rowSignatures[y] = nativeRowSignature(image, y);
    }
    if (ultraLong_) {
        if (!spool_ || !spool_->isValid()) return {};
        slice->path = spool_->filePath(QStringLiteral("%1.png").arg(++spoolSequence_));
        if (!image.save(slice->path, "PNG")) return {};
        const double scale = std::min({1.0, 190.0 / image.width(), 720.0 / image.height()});
        slice->thumbnail = image.scaled(std::max(1, qRound(image.width() * scale)),
                                       std::max(1, qRound(image.height() * scale)),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else {
        slice->image = image;
    }
    return slice;
}

void ScrollStitcher::reset(const QImage &first) {
    previous_ = {};
    slices_.clear();
    spool_.reset();
    spoolSequence_ = 0;
    top_ = {};
    bottom_ = {};
    cached_ = {};
    thumbnails_.clear();
    bands_ = {};
    height_ = heightLimit_ = 0;
    firstOffset_ = endOffset_ = viewportOffset_ = 0;
    bandsKnown_ = partial_ = false;
    status_ = Status::Empty;
    ++generation_;
    if (first.isNull() || first.width() < 8 || first.height() < 32 || first.width() > maxHeight() ||
        first.height() > maxHeight() || qint64(first.width()) * first.height() > maxPixels())
        return;
    if (ultraLong_) {
        spool_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/edithere-longcapture-XXXXXX"));
        if (!spool_->isValid()) return;
    }
    previous_ = first.convertToFormat(QImage::Format_ARGB32);
    previous_.setDevicePixelRatio(1.0);
    const auto slice = storeSlice(previous_);
    if (!slice) { previous_ = {}; return; }
    slices_.append(slice);
    height_ = previous_.height();
    endOffset_ = height_;
    heightLimit_ = ultraLong_ ? ultraMaxHeight() : std::min(maxHeight(), int(maxPixels() / previous_.width()));
}

ScrollStitcher::Step ScrollStitcher::plan(const QImage &frame) const {
    Step step;
    step.generation = generation_;
    if (previous_.isNull() || frame.isNull() || frame.size() != previous_.size()) {
        step.status = Status::GeometryChanged;
        return step;
    }
    QImage next = frame.convertToFormat(QImage::Format_ARGB32);
    next.setDevicePixelRatio(1.0);
    step.frame = next;
    step.viewportOffset = viewportOffset_;
    if (previous_ == next) {
        step.status = Status::Repeat;
        step.added = 0;
        return step;
    }
    const ScrollBands bands = bandsKnown_ ? bands_ : fixedBands(previous_, next);
    const int contentHeight = next.height() - bands.top - bands.bottom;
    const QImage current = previous_.copy(0, bands.top, next.width(), contentHeight);
    const QImage incoming = next.copy(0, bands.top, next.width(), contentHeight);
    const int minimum = std::max(32, contentHeight / 5);
    const auto forward = matchVerticalOverlap(current, incoming, minimum, contentHeight, 8.0);
    const auto backward = matchVerticalOverlap(incoming, current, minimum, contentHeight, 8.0);
    if (!forward.found && !backward.found) {
        bool recoveryAmbiguous = false;
        if (bandsKnown_) {
            std::unordered_map<quint64, std::vector<int>> anchors;
            std::unordered_map<quint64, int> frequency;
            std::vector<quint64> queryKeys(contentHeight), querySignatures(contentHeight);
            std::vector<int> queryEvidence(contentHeight), anchorRows;
            for (int y = 0; y < contentHeight; ++y) {
                queryKeys[y] = nativeRowKey(incoming, y, &queryEvidence[y]);
                querySignatures[y] = nativeRowSignature(incoming, y);
                if (queryKeys[y]) ++frequency[queryKeys[y]];
            }
            // Prefer distinctive textured rows over the busiest common letters
            // in a repeated list. Keep anchors distributed through the viewport.
            for (int band = 0; band < 12; ++band) {
                int selected = -1, bestEvidence = 1, fewest = std::numeric_limits<int>::max();
                for (int y = band * contentHeight / 12; y < (band + 1) * contentHeight / 12; ++y) {
                    const quint64 key = queryKeys[y];
                    if (key && y + 7 < contentHeight && (frequency[key] < fewest ||
                        (frequency[key] == fewest && queryEvidence[y] > bestEvidence))) {
                        selected = y; fewest = frequency[key]; bestEvidence = queryEvidence[y];
                    }
                }
                if (selected >= 0) {
                    anchors[queryKeys[selected]].push_back(selected);
                    anchorRows.push_back(selected);
                }
            }
            std::map<int, int> votes;
            int position = firstOffset_;
            for (const auto &slice : slices_) {
                for (int y = 0; y < slice->rowKeys.size(); ++y) {
                    const auto anchor = anchors.find(slice->rowKeys[y]);
                    if (anchor == anchors.end()) continue;
                    for (int queryY : anchor->second) {
                        const int offset = position + y - queryY;
                        if (std::min(endOffset_, offset + contentHeight) -
                            std::max(firstOffset_, offset) >= minimum)
                            ++votes[offset];
                    }
                    if (votes.size() > 8192) break;
                }
                position += slice->height;
                if (votes.size() > 8192) break;
            }
            const auto columns = movingColumns(current, incoming);
            const auto acceptRecovered = [&](const std::map<int, int> &proposals) {
                std::vector<std::pair<double, int>> verified;
                if (proposals.size() > 8192) { recoveryAmbiguous = true; return false; }
                for (const auto &[offset, count] : proposals) {
                    if (count < 2) continue;
                    const int begin = std::max(firstOffset_, offset);
                    const int length = std::min(endOffset_, offset + contentHeight) - begin;
                    const QImage retained = readRows(top_.height() + begin - firstOffset_, length);
                    if (retained.isNull()) continue;
                    const QImage overlap = incoming.copy(0, begin - offset, incoming.width(), length);
                    const Samples saved(retained, columns.first, columns.second),
                                  proposed(overlap, columns.first, columns.second);
                    if (evaluate(saved, proposed, length).score > 8.0) continue;
                    if (verified.size() >= 32) { recoveryAmbiguous = true; return false; }
                    const auto match = verifyNativeOverlap(retained, 0, incoming, begin - offset,
                                                           length, columns.first, columns.second);
                    if (match.score < kInvalid) verified.emplace_back(match.score, offset);
                }
                if (verified.empty()) return false;
                std::sort(verified.begin(), verified.end());
                const auto [score, offset] = verified.front();
                double alternate = kInvalid;
                for (size_t index = 1; index < verified.size(); ++index)
                    if (std::abs(verified[index].second - offset) >= 3)
                        alternate = std::min(alternate, verified[index].first);
                recoveryAmbiguous = alternate <= score + std::max(0.02, score * 0.35);
                if (score <= 8.0 && !recoveryAmbiguous) {
                    step.viewportOffset = offset;
                    step.displacement = offset - viewportOffset_;
                    step.prepend = std::max(0, firstOffset_ - offset);
                    step.append = std::max(0, offset + contentHeight - endOffset_);
                    step.added = step.prepend + step.append;
                    step.status = step.added ? Status::Added : Status::Tracked;
                    step.bands = bands_;
                    step.score = score;
                    if (height_ + qint64(step.added) > heightLimit_) {
                        step.status = Status::Limit;
                        step.added = -1;
                    }
                    return true;
                }
                return false;
            };
            if (acceptRecovered(votes)) return step;
            const bool exactAmbiguous = recoveryAmbiguous;
            recoveryAmbiguous = false;
            // A second index tolerates small colour changes and ignores fixed
            // sidebars. Each byte combines average luma with edge density for
            // one horizontal group. Four nearby rows form an anchor profile;
            // matching one blank/quantized row alone would be too ambiguous.
            std::vector<int> groups;
            const int margin = sideMargin(incoming.width()), span = incoming.width() - margin * 2;
            for (int group = 0; group < 8; ++group) {
                const int begin = margin + group * span / 8, end = margin + (group + 1) * span / 8;
                if (end > begin && begin >= columns.first && end <= columns.second) groups.push_back(group);
            }
            if (groups.empty()) {
                for (int group = 0; group < 8; ++group) {
                    const int begin = margin + group * span / 8, end = margin + (group + 1) * span / 8;
                    if (end > begin && (begin + end) / 2 >= columns.first &&
                        (begin + end) / 2 < columns.second) groups.push_back(group);
                }
            }
            std::vector<int> boundaries;
            position = firstOffset_;
            for (const auto &slice : slices_) {
                boundaries.push_back(position);
                position += slice->height;
            }
            const auto signatureAt = [&](int y) {
                const auto found = std::upper_bound(boundaries.begin(), boundaries.end(), y);
                const int index = int(found - boundaries.begin()) - 1;
                return index >= 0 && y < endOffset_ ?
                    slices_[index]->rowSignatures[y - boundaries[index]] : quint64(0);
            };
            votes.clear();
            position = firstOffset_;
            if (!groups.empty()) {
                for (const auto &slice : slices_) {
                    for (int y = 0; y < slice->height; ++y) {
                        for (int queryY : anchorRows) {
                            const int offset = position + y - queryY;
                            if (position + y + 7 >= endOffset_ ||
                                std::min(endOffset_, offset + contentHeight) -
                                std::max(firstOffset_, offset) < minimum) continue;
                            bool matches = true;
                            for (int delta : {0, 1, 3, 7}) {
                                const quint64 saved = y + delta < slice->height ? slice->rowSignatures[y + delta] :
                                    signatureAt(position + y + delta);
                                if (!similarRowSignature(saved, querySignatures[queryY + delta], groups)) {
                                    matches = false; break;
                                }
                            }
                            if (matches) ++votes[offset];
                        }
                        if (votes.size() > 8192) break;
                    }
                    position += slice->height;
                    if (votes.size() > 8192) break;
                }
            }
            if (acceptRecovered(votes)) return step;
            recoveryAmbiguous |= exactAmbiguous;
        }
        // A reliable nonzero displacement takes precedence over animation
        // tolerance. Sparse moving text can affect far fewer than 2% of pixels.
        if (!forward.ambiguous && !backward.ambiguous && !recoveryAmbiguous &&
            equivalentScrollFrames(previous_, next)) {
            step.status = Status::Repeat;
            step.added = 0;
        } else
            step.status = forward.ambiguous || backward.ambiguous || recoveryAmbiguous ?
                Status::Ambiguous : Status::NoMatch;
        return step;
    }
    const int forwardDistance = contentHeight - forward.overlap;
    const int backwardDistance = contentHeight - backward.overlap;
    bool reversed = !forward.found;
    if (forward.found && backward.found && forwardDistance && backwardDistance) {
        // A repeating pattern can support two opposite displacements. Only use
        // one direction when its evidence is substantially better.
        if (std::abs(forward.score - backward.score) <= 1.8) {
            step.status = Status::Ambiguous;
            return step;
        }
        reversed = backward.score < forward.score;
    }
    const auto &match = reversed ? backward : forward;
    const int displacement = (contentHeight - match.overlap) * (reversed ? -1 : 1);
    if (!displacement) {
        step.status = Status::Repeat;
        step.added = 0;
        return step;
    }
    step.displacement = displacement;
    step.viewportOffset = viewportOffset_ + displacement;
    const int end = bandsKnown_ ? endOffset_ : contentHeight;
    step.prepend = std::max(0, firstOffset_ - step.viewportOffset);
    step.append = std::max(0, step.viewportOffset + contentHeight - end);
    const int added = step.prepend + step.append;
    if (height_ + qint64(added) > heightLimit_) {
        step.status = Status::Limit;
        return step;
    }
    step.status = added ? Status::Added : Status::Tracked;
    step.added = added;
    step.bands = bands;
    step.score = match.score;
    return step;
}

int ScrollStitcher::commit(const Step &step) {
    if (step.generation != generation_)
        return step.frame.isNull() ? (status_ = Status::GeometryChanged, -1) : commit(plan(step.frame));
    status_ = step.status;
    if (step.added < 0 || step.status == Status::Repeat)
        return step.added;
    const QImage &next = step.frame;
    const ScrollBands bands = step.bands;
    const int added = step.added;
    const QImage prefix = step.prepend ? next.copy(0, bands.top, next.width(), step.prepend) : QImage();
    const QImage suffix = step.append ? next.copy(0, next.height() - bands.bottom - step.append,
                                                 next.width(), step.append) : QImage();
    if ((step.prepend && prefix.isNull()) || (step.append && suffix.isNull())) {
        status_ = Status::Limit;
        return -1;
    }
    const auto prefixSlice = step.prepend ? storeSlice(prefix) : QSharedPointer<Slice>();
    const auto suffixSlice = step.append ? storeSlice(suffix) : QSharedPointer<Slice>();
    const auto bodySlice = !bandsKnown_ ? storeSlice(previous_.copy(0, bands.top, next.width(),
        next.height() - bands.top - bands.bottom)) : QSharedPointer<Slice>();
    if ((step.prepend && !prefixSlice) || (step.append && !suffixSlice) || (!bandsKnown_ && !bodySlice)) {
        status_ = Status::Limit;
        return -1;
    }
    if (!bandsKnown_) {
        top_ = bands.top ? previous_.copy(0, 0, next.width(), bands.top) : QImage();
        bottom_ = bands.bottom ? previous_.copy(0, next.height() - bands.bottom, next.width(), bands.bottom) : QImage();
        slices_[0] = bodySlice;
        endOffset_ = slices_[0]->height;
        thumbnails_.clear();
    }
    if (step.prepend) {
        slices_.prepend(prefixSlice);
        firstOffset_ -= step.prepend;
        thumbnails_.clear();
    }
    if (step.append) {
        slices_.append(suffixSlice);
        endOffset_ += step.append;
    }
    height_ += added;
    bands_ = bands;
    bandsKnown_ = true;
    partial_ |= step.score > 1.0;
    previous_ = next;
    viewportOffset_ = step.viewportOffset;
    if (added)
        cached_ = {};
    ++generation_;
    return added;
}

QRect ScrollStitcher::viewportRect() const {
    if (previous_.isNull())
        return {};
    if (!bandsKnown_)
        return previous_.rect();
    // Fixed toolbars live at the output edges and are not part of the moving
    // page. The indicator follows the captured body instead of repeating them.
    return {0, top_.height() + viewportOffset_ - firstOffset_, previous_.width(),
            previous_.height() - bands_.top - bands_.bottom};
}

bool ScrollStitcher::trimBeforeViewport() {
    if (!bandsKnown_ || slices_.isEmpty()) return false;
    int remove = viewportOffset_ - firstOffset_;
    if (remove <= 0 && top_.isNull()) return false;
    remove = std::clamp(remove, 0, endOffset_ - firstOffset_ - 1);
    const int removed = remove;
    auto retained = slices_;
    while (remove > 0 && !retained.isEmpty()) {
        const int take = std::min(remove, retained.front()->height);
        if (take == retained.front()->height) retained.removeFirst();
        else {
            const auto slice = storeSlice(retained.front()->load().copy(0, take, previous_.width(),
                                                                       retained.front()->height - take));
            if (!slice) return false;
            retained.front() = slice;
        }
        remove -= take;
    }
    slices_ = std::move(retained);
    firstOffset_ += removed;
    height_ -= removed;
    height_ -= top_.height();
    top_ = {};
    cached_ = {};
    thumbnails_.clear();
    ++generation_;
    return true;
}

bool ScrollStitcher::trimAfterViewport() {
    if (!bandsKnown_ || slices_.isEmpty()) return false;
    const int contentHeight = previous_.height() - bands_.top - bands_.bottom;
    int remove = endOffset_ - viewportOffset_ - contentHeight;
    if (remove <= 0 && bottom_.isNull()) return false;
    remove = std::clamp(remove, 0, endOffset_ - firstOffset_ - 1);
    const int removed = remove;
    auto retained = slices_;
    while (remove > 0 && !retained.isEmpty()) {
        const int take = std::min(remove, retained.back()->height);
        if (take == retained.back()->height) retained.removeLast();
        else {
            const auto slice = storeSlice(retained.back()->load().copy(0, 0, previous_.width(),
                                                                      retained.back()->height - take));
            if (!slice) return false;
            retained.back() = slice;
        }
        remove -= take;
    }
    slices_ = std::move(retained);
    endOffset_ -= removed;
    height_ -= removed;
    height_ -= bottom_.height();
    bottom_ = {};
    cached_ = {};
    thumbnails_.clear();
    ++generation_;
    return true;
}

QImage ScrollStitcher::preview(int width, int maximumHeight) const {
    if (slices_.isEmpty() || width <= 0)
        return {};
    if (ultraLong_ && maximumHeight <= 0) maximumHeight = 720;
    const double scale = std::min({1.0, double(width) / previous_.width(),
        maximumHeight > 0 ? double(maximumHeight) / height_ : 1.0});
    const int resultWidth = std::max(1, qRound(previous_.width() * scale));
    if (ultraLong_) {
        QImage result(resultWidth, std::max(1, qRound(height_ * scale)), QImage::Format_ARGB32);
        if (result.isNull()) return {};
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        int offset = 0;
        const auto paint = [&](const QImage &thumbnail, int nativeHeight) {
            const int begin = qRound(offset * scale);
            offset += nativeHeight;
            const int end = qRound(offset * scale);
            if (end > begin && !thumbnail.isNull())
                painter.drawImage(QRect(0, begin, resultWidth, end - begin), thumbnail);
        };
        if (!top_.isNull()) paint(top_, top_.height());
        for (const auto &slice : slices_) paint(slice->thumbnail, slice->height);
        if (!bottom_.isNull()) paint(bottom_, bottom_.height());
        return result;
    }
    const auto scaled = [resultWidth](const QImage &image, int height) {
        return image.isNull() || height <= 0 ? QImage() :
            image.scaled(resultWidth, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    if (thumbnailWidth_ != width || thumbnailScale_ != scale) {
        thumbnails_.clear();
        thumbnailWidth_ = width;
        thumbnailScale_ = scale;
    }
    int contentEnd = top_.height();
    for (int index = 0; index < slices_.size(); ++index) {
        const int contentStart = contentEnd;
        contentEnd += slices_[index]->height;
        if (index >= thumbnails_.size())
            thumbnails_.append(scaled(slices_[index]->load(), qRound(contentEnd * scale) - qRound(contentStart * scale)));
    }
    // Round cumulative boundaries, avoiding one extra preview row per small
    // wheel movement and keeping the viewport indicator aligned to the page.
    const QImage header = scaled(top_, qRound(top_.height() * scale));
    const QImage footer = scaled(bottom_, qRound(height_ * scale) - qRound(contentEnd * scale));
    QImage result(resultWidth, std::max(1, qRound(height_ * scale)), QImage::Format_ARGB32);
    if (result.isNull())
        return {};
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int y = header.height();
    if (!header.isNull())
        painter.drawImage(0, 0, header);
    for (const QImage &thumbnail : thumbnails_) {
        painter.drawImage(0, y, thumbnail);
        y += thumbnail.height();
    }
    if (!footer.isNull())
        painter.drawImage(0, y, footer);
    return result;
}

QImage ScrollStitcher::previewRegion(const QRect &requested, int width) const {
    const QRect region = requested.intersected(QRect(0, 0, previous_.width(), height_));
    if (region.isEmpty() || slices_.isEmpty() || width <= 0) return {};
    if (regionPreviewGeneration_ == generation_ && regionPreviewRect_ == region &&
        regionPreviewWidth_ == width && !regionPreview_.isNull()) return regionPreview_;
    const double scale = std::min(1.0, double(width) / region.width());
    const QSize output(std::max(1, qRound(region.width() * scale)),
                       std::max(1, qRound(region.height() * scale)));
    // The caller supplies only a visible screen window. Never allocate a
    // thumbnail whose height grows with the entire two-million-pixel result.
    if (output.width() > 8192 || output.height() > 8192) return {};
    QImage result(output, QImage::Format_ARGB32);
    if (result.isNull()) return {};
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double xScale = double(output.width()) / region.width();
    const double yScale = double(output.height()) / region.height();
    int offset = 0;
    const auto paint = [&](const QImage &image, int nativeHeight) {
        const QRect native(0, offset, previous_.width(), nativeHeight);
        offset += nativeHeight;
        const QRect part = native.intersected(region);
        if (part.isEmpty() || image.isNull()) return;
        const QRectF source(double(part.x()) * image.width() / native.width(),
                            double(part.y() - native.y()) * image.height() / nativeHeight,
                            double(part.width()) * image.width() / native.width(),
                            double(part.height()) * image.height() / nativeHeight);
        // Round cumulative boundaries rather than individual slice heights.
        const int begin = qRound((part.y() - region.y()) * yScale);
        const int end = qRound((part.y() + part.height() - region.y()) * yScale);
        if (end > begin)
            painter.drawImage(QRectF((part.x() - region.x()) * xScale, begin,
                                     part.width() * xScale, end - begin), image, source);
    };
    if (!top_.isNull()) paint(top_, top_.height());
    for (const auto &slice : slices_) {
        if (offset >= region.y() + region.height() || offset + slice->height <= region.y()) {
            offset += slice->height;
            continue;
        }
        // Cached thumbnails cover normal display resolution. Extremely narrow
        // selections may need the original intersecting slice, never the full stitch.
        const bool enoughDetail = !slice->thumbnail.isNull() &&
            double(slice->thumbnail.width()) / previous_.width() >= xScale * 0.95 &&
            double(slice->thumbnail.height()) / slice->height >= yScale * 0.95;
        paint(enoughDetail ? slice->thumbnail : slice->load(), slice->height);
    }
    if (!bottom_.isNull()) paint(bottom_, bottom_.height());
    painter.end();
    regionPreview_ = result;
    regionPreviewRect_ = region;
    regionPreviewWidth_ = width;
    regionPreviewGeneration_ = generation_;
    return result;
}

QImage ScrollStitcher::readRows(int first, int count, bool transpose) const {
    const int length = transpose ? previous_.width() : height_;
    if (first < 0 || first >= length || count <= 0 || slices_.isEmpty()) return {};
    count = std::min(count, length - first);
    QImage result(transpose ? height_ : previous_.width(), count, QImage::Format_ARGB32);
    if (result.isNull()) return {};
    int offset = 0;
    const auto copy = [&](const QImage &image, int rows) {
        if (image.isNull()) return false;
        if (transpose) {
            for (int y = 0; y < count; ++y) {
                auto *destination = reinterpret_cast<QRgb *>(result.scanLine(y)) + offset;
                for (int x = 0; x < rows; ++x)
                    destination[x] = reinterpret_cast<const QRgb *>(image.constScanLine(x))[first + y];
            }
        } else {
            const int begin = std::max(first, offset);
            const int end = std::min(first + count, offset + rows);
            for (int y = begin; y < end; ++y)
                std::memcpy(result.scanLine(y - first), image.constScanLine(y - offset),
                            size_t(previous_.width()) * sizeof(QRgb));
        }
        offset += rows;
        return true;
    };
    const auto needed = [&](int rows) {
        return transpose || (offset < first + count && offset + rows > first);
    };
    if (!top_.isNull() && !copy(top_, top_.height())) return {};
    for (const auto &slice : slices_) {
        if (needed(slice->height)) {
            if (!copy(slice->load(), slice->height)) return {};
        } else offset += slice->height;
    }
    if (!bottom_.isNull() && !copy(bottom_, bottom_.height())) return {};
    return result;
}

QImage ScrollStitcher::picture() const {
    if (slices_.isEmpty() || height_ > maxHeight() || qint64(height_) * previous_.width() > maxPixels())
        return {};
    if (!cached_.isNull())
        return cached_;
    if (slices_.size() == 1 && top_.isNull() && bottom_.isNull())
        return slices_.front()->load();
    QImage result(previous_.width(), height_, QImage::Format_ARGB32);
    if (result.isNull())
        return {};
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    int y = top_.height();
    if (!top_.isNull())
        painter.drawImage(0, 0, top_);
    for (const auto &slice : slices_) {
        const QImage loaded = slice->load();
        if (loaded.isNull()) return {};
        painter.drawImage(0, y, loaded);
        y += slice->height;
    }
    if (!bottom_.isNull())
        painter.drawImage(0, y, bottom_);
    painter.end();
    cached_ = result;
    return cached_;
}
} // namespace h2d
