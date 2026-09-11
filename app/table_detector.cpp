#include "detector.h"
#include <QMap>
#include <QSet>
#include <algorithm>
#include <numeric>
namespace h2d {
namespace {
// Directional contrast + closing short gaps + retaining long runs implements
// line extraction without shrinking away one-pixel rules on high-DPI captures.
struct Rule {
    int position, start, end, last;
};
QVector<Rule> rules(const QImage &gray, bool horizontal) {
    const int across = horizontal ? gray.width() : gray.height();
    const int lanes = horizontal ? gray.height() : gray.width();
    const int minimum = horizontal ? 24 : 14;
    QVector<Rule> found;
    QVector<int> previous;
    auto pixel = [&](int lane, int at) {
        return horizontal ? gray.constScanLine(lane)[at] : gray.constScanLine(at)[lane];
    };
    for (int lane = 0; lane < lanes; ++lane) {
        QVector<int> current;
        int begin = -1, end = -1, count = 0;
        auto finish = [&] {
            if (begin >= 0 && end - begin + 1 >= minimum && count * 10 >= (end - begin + 1) * 8) {
                int match = -1;
                for (int index : previous) {
                    const auto &rule = found[index];
                    if (lane - rule.position <= 6 && std::abs(begin - rule.start) <= 6 &&
                        std::abs(end - rule.end) <= 6) {
                        match = index;
                        break;
                    }
                }
                if (match < 0 && found.size() < 2048) {
                    match = found.size();
                    found.append({lane, begin, end, lane});
                } else if (match >= 0) {
                    found[match].start = std::min(found[match].start, begin);
                    found[match].end = std::max(found[match].end, end);
                    found[match].last = lane;
                }
                if (match >= 0)
                    current.append(match);
            }
            begin = end = -1;
            count = 0;
        };
        for (int at = 0; at < across; ++at) {
            const int center = pixel(lane, at);
            bool ink = false;
            for (int radius : {1, 3, 5}) {
                if (lane < radius || lane + radius >= lanes)
                    continue;
                const int a = pixel(lane - radius, at) - center;
                const int b = pixel(lane + radius, at) - center;
                if ((a >= 4 && b >= 4) || (a <= -4 && b <= -4)) {
                    ink = true;
                    break;
                }
            }
            if (ink) {
                if (begin < 0)
                    begin = at;
                end = at;
                ++count;
            } else if (begin >= 0 && at - end > 3)
                finish();
        }
        finish();
        previous = std::move(current);
    }
    for (auto &rule : found)
        rule.position = (rule.position + rule.last) / 2;
    return found;
}
struct Union {
    QVector<int> parent;
    explicit Union(int count) : parent(count) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int root(int i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }
    void join(int a, int b) {
        parent[root(a)] = root(b);
    }
};
QVector<int> axes(const QVector<Rule> &lines) {
    QVector<int> values;
    for (auto line : lines)
        values.append(line.position);
    std::sort(values.begin(), values.end());
    QVector<int> unique;
    for (int value : values)
        if (unique.isEmpty() || value - unique.last() > 3)
            unique.append(value);
    return unique;
}
bool covered(const QVector<Rule> &lines, int axis, int start, int end) {
    int reach = start;
    // Collinear segments can be separated at intersections or short broken rules.
    QVector<Rule> matches;
    for (auto line : lines)
        if (std::abs(line.position - axis) <= 3)
            matches.append(line);
    std::sort(matches.begin(), matches.end(), [](auto a, auto b) { return a.start < b.start; });
    for (auto line : matches) {
        if (line.start > reach + 4)
            break;
        if (line.end >= reach)
            reach = line.end;
    }
    return reach >= end - 4;
}
} // namespace
QVector<Candidate> detectTableBlocks(const QImage &original) {
    if (original.isNull() || qint64(original.width()) * original.height() > MaxPixels)
        return {};
    const auto gray = original.convertToFormat(QImage::Format_Grayscale8);
    const auto horizontal = rules(gray, true), vertical = rules(gray, false);
    Union lines(horizontal.size() + vertical.size());
    for (int h = 0; h < horizontal.size(); ++h)
        for (int v = 0; v < vertical.size(); ++v) {
            const auto a = horizontal[h], b = vertical[v];
            if (b.position >= a.start - 4 && b.position <= a.end + 4 && a.position >= b.start - 4 &&
                a.position <= b.end + 4)
                lines.join(h, horizontal.size() + v);
        }
    struct Grid {
        QVector<Rule> horizontal, vertical;
    };
    QMap<int, Grid> grids;
    for (int i = 0; i < horizontal.size(); ++i)
        grids[lines.root(i)].horizontal.append(horizontal[i]);
    for (int i = 0; i < vertical.size(); ++i)
        grids[lines.root(horizontal.size() + i)].vertical.append(vertical[i]);
    QVector<Candidate> containers, cells;
    QSet<QString> seen;
    auto add = [&](QRect rect, const QString &kind, const QString &label, bool cell = false) {
        rect = rect.intersected(original.rect());
        if (rect.width() < 12 || rect.height() < 10)
            return;
        const QString key =
            QString("%1,%2,%3,%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        if (seen.contains(key))
            return;
        seen.insert(key);
        auto target = manualTarget();
        target["source"] = "vision";
        target["method"] = "table-" + kind;
        target["label"] = label;
        (cell ? cells : containers).append({rect, target});
    };
    for (const auto &grid : grids) {
        const auto xs = axes(grid.vertical), ys = axes(grid.horizontal);
        const int columns = xs.size() - 1, rows = ys.size() - 1;
        if (columns < 1 || rows < 1 || columns > 128 || rows > 128 || columns * rows > 4096)
            continue;
        QVector<bool> hEdges((rows + 1) * columns), vEdges(rows * (columns + 1));
        for (int y = 0; y <= rows; ++y)
            for (int x = 0; x < columns; ++x)
                hEdges[y * columns + x] = covered(grid.horizontal, ys[y], xs[x], xs[x + 1]);
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x <= columns; ++x)
                vEdges[y * (columns + 1) + x] = covered(grid.vertical, xs[x], ys[y], ys[y + 1]);
        auto closed = [&](int left, int top, int right, int bottom) {
            for (int x = left; x < right; ++x)
                if (!hEdges[top * columns + x] || !hEdges[bottom * columns + x])
                    return false;
            for (int y = top; y < bottom; ++y)
                if (!vEdges[y * (columns + 1) + left] || !vEdges[y * (columns + 1) + right])
                    return false;
            return true;
        };
        auto rect = [&](int l, int t, int r, int b) {
            return QRect(xs[l], ys[t], xs[r] - xs[l], ys[b] - ys[t]);
        };
        Union faces(columns * rows);
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                const int i = y * columns + x;
                if (x + 1 < columns && !vEdges[y * (columns + 1) + x + 1])
                    faces.join(i, i + 1);
                if (y + 1 < rows && !hEdges[(y + 1) * columns + x])
                    faces.join(i, i + columns);
            }
        QMap<int, QRect> bounds;
        QMap<int, int> counts;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                const int id = faces.root(y * columns + x);
                bounds[id] = bounds[id].united(QRect(x, y, 1, 1));
                ++counts[id];
            }
        QVector<QRect> valid;
        for (auto it = bounds.cbegin(); it != bounds.cend(); ++it) {
            const auto b = it.value();
            if (counts[it.key()] == b.width() * b.height() &&
                closed(b.x(), b.y(), b.x() + b.width(), b.y() + b.height()))
                valid.append(rect(b.x(), b.y(), b.x() + b.width(), b.y() + b.height()));
        }
        if (valid.isEmpty())
            continue;
        const bool table = valid.size() > 1;
        if (closed(0, 0, columns, rows))
            add(rect(0, 0, columns, rows), table ? "outer" : "frame", table ? "整个表格" : "边框区域");
        if (table) {
            for (int y = 0; y < rows; ++y)
                if (closed(0, y, columns, y + 1))
                    add(rect(0, y, columns, y + 1), "row", "表格行");
            for (int x = 0; x < columns; ++x)
                if (closed(x, 0, x + 1, rows))
                    add(rect(x, 0, x + 1, rows), "column", "表格列");
            for (auto b : valid)
                add(b, "cell", "单元格", true);
        }
    }
    // Retain complete table/row/column ranges before sampling very dense cell sets.
    constexpr int limit = 384;
    QVector<Candidate> result;
    auto append = [&](const QVector<Candidate> &items) {
        const int count = std::min(int(items.size()), limit - int(result.size()));
        for (int i = 0; i < count; ++i)
            result.append(items[count == 1 ? 0 : qint64(i) * (items.size() - 1) / (count - 1)]);
    };
    append(containers);
    append(cells);
    return result;
}
} // namespace h2d