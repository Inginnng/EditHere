#include "detector.h"
#include <algorithm>
#include <functional>
namespace h2d {
namespace {
qint64 area(QRect r) {
    return qint64(r.width()) * r.height();
}
double overlap(QRect a, QRect b) {
    const auto intersection = area(a.intersected(b));
    return double(intersection) / std::max<qint64>(1, area(a) + area(b) - intersection);
}
void components(const QVector<int> &map, int w, int h, bool ignoreZero,
                const std::function<void(QRect, int)> &found) {
    QVector<quint8> seen(w * h);
    QVector<int> queue(w * h);
    for (int origin = 0; origin < map.size(); ++origin) {
        if (seen[origin] || (ignoreZero && !map[origin]))
            continue;
        int head = 0, tail = 1, left = origin % w, right = left, top = origin / w, bottom = top;
        queue[0] = origin;
        seen[origin] = 1;
        int value = map[origin];
        auto visit = [&](int i) {
            if (!seen[i] && map[i] == value) {
                seen[i] = 1;
                queue[tail++] = i;
            }
        };
        while (head < tail) {
            int i = queue[head++], x = i % w, y = i / w;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            if (x)
                visit(i - 1);
            if (x + 1 < w)
                visit(i + 1);
            if (y)
                visit(i - w);
            if (y + 1 < h)
                visit(i + w);
        }
        found({left, top, right - left + 1, bottom - top + 1}, tail);
    }
}
} // namespace
QVector<Candidate> detectBlocks(const QImage &original) {
    if (original.width() < 16 || original.height() < 16)
        return {};
    QImage image = original.scaled(1100, 1100, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
    if (original.width() <= 1100 && original.height() <= 1100)
        image = original.convertToFormat(QImage::Format_RGB32);
    const int w = image.width(), h = image.height();
    QVector<int> colors(w * h), edges(w * h);
    QVector<Candidate> proposals;
    auto add = [&](QRect r, QString method, QString label) {
        int x = int(std::floor(double(r.x()) * original.width() / w)),
            y = int(std::floor(double(r.y()) * original.height() / h));
        int xx = int(std::ceil(double(r.x() + r.width()) * original.width() / w)),
            yy = int(std::ceil(double(r.y() + r.height()) * original.height() / h));
        QRect b(x, y, xx - x, yy - y);
        if (b.width() < 12 || b.height() < 10 || b == original.rect())
            return;
        auto target = manualTarget();
        target["source"] = "vision";
        target["label"] = label;
        target["method"] = method;
        proposals.append({b, target});
    };
    for (int y = 0; y < h; y++) {
        const auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; x++) {
            QRgb v = row[x];
            colors[y * w + x] = ((qRed(v) >> 4) << 8) | ((qGreen(v) >> 4) << 4) | (qBlue(v) >> 4);
        }
    }
    components(colors, w, h, false, [&](QRect r, int count) {
        if (r.width() < 24 || r.height() < 18 || count < 220)
            return;
        double fill = double(count) / area(r);
        int color = colors[r.y() * w + r.x()], matches = 0, border = 0;
        for (int x = r.x(); x < r.x() + r.width(); x++) {
            matches += colors[r.y() * w + x] == color;
            matches += colors[(r.y() + r.height() - 1) * w + x] == color;
            border += 2;
        }
        for (int y = r.y(); y < r.y() + r.height(); y++) {
            matches += colors[y * w + r.x()] == color;
            matches += colors[y * w + r.x() + r.width() - 1] == color;
            border += 2;
        }
        if (fill > .7 || (fill > .2 && double(matches) / border > .86))
            add(r, "color-region", "色块区域");
    });
    auto difference = [](QRgb a, QRgb b) {
        return std::max(
            {std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
    };
    for (int y = 1; y < h - 1; y++) {
        auto row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        auto below = reinterpret_cast<const QRgb *>(image.constScanLine(y + 1));
        for (int x = 1; x < w - 1; x++)
            if (difference(row[x], row[x + 1]) > 36 || difference(row[x], below[x]) > 36)
                edges[y * w + x] = 1;
    }
    QVector<int> summed((w + 1) * (h + 1)), dilated(w * h);
    for (int y = 0; y < h; y++) {
        int row = 0;
        for (int x = 0; x < w; x++) {
            row += edges[y * w + x];
            summed[(y + 1) * (w + 1) + x + 1] = summed[y * (w + 1) + x + 1] + row;
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int l = std::max(0, x - 3), r = std::min(w, x + 4), t = std::max(0, y - 1),
                b = std::min(h, y + 2);
            dilated[y * w + x] = summed[b * (w + 1) + r] - summed[t * (w + 1) + r] - summed[b * (w + 1) + l] +
                                     summed[t * (w + 1) + l] >
                                 0;
        }
    components(dilated, w, h, true, [&](QRect r, int count) {
        if (r.width() >= 24 && r.height() >= 14 && count >= 80) {
            r.adjust(2, 0, -2, 0);
            add(r, "edge-region", "内容区域");
        }
    });
    std::stable_sort(proposals.begin(), proposals.end(),
                     [](auto &a, auto &b) { return area(a.bounds) < area(b.bounds); });
    QVector<Candidate> output;
    for (const auto &p : proposals) {
        bool duplicate = false;
        for (const auto &c : output)
            if (overlap(p.bounds, c.bounds) > .87) {
                duplicate = true;
                break;
            }
        if (!duplicate)
            output.append(p);
    }
    constexpr int maxCandidates = 220;
    if (output.size() > maxCandidates) {
        QVector<Candidate> sampled;
        sampled.reserve(maxCandidates);
        // Sample the complete size range instead of dropping every large region.
        // Stable area ordering also spreads equal-sized cells across the image.
        for (int i = 0; i < maxCandidates; ++i) {
            const qsizetype index = qint64(i) * (output.size() - 1) / (maxCandidates - 1);
            sampled.append(output[index]);
        }
        output = std::move(sampled);
    }
    auto structured = detectTableBlocks(original);
    if (structured.isEmpty())
        return output;
    QVector<Candidate> other;
    for (const auto &candidate : output) {
        const bool duplicate = std::any_of(structured.cbegin(), structured.cend(), [&](const auto &table) {
            return overlap(candidate.bounds, table.bounds) > .87;
        });
        if (!duplicate)
            other.append(candidate);
    }
    const int count = std::min(int(other.size()), 480 - int(structured.size()));
    for (int i = 0; i < count; ++i)
        structured.append(other[count == 1 ? 0 : qint64(i) * (other.size() - 1) / (count - 1)]);
    return structured;
}
} // namespace h2d
