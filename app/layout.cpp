#include "layout.h"
#include "model.h"
#include <QHash>
#include <QJsonArray>
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace h2d {
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
constexpr double epsilon = 1e-7;
bool positive(QRectF r) {
    return r.width() > epsilon && r.height() > epsilon;
}
bool contains(QRectF r, QPointF p) {
    return p.x() >= r.left() && p.x() < r.right() && p.y() >= r.top() && p.y() < r.bottom();
}
QRectF sourcePart(const LayoutPiece &piece, QRectF destination) {
    const auto s = piece.source, d = piece.destination;
    return {s.x() + (destination.x() - d.x()) * s.width() / d.width(),
            s.y() + (destination.y() - d.y()) * s.height() / d.height(),
            destination.width() * s.width() / d.width(), destination.height() * s.height() / d.height()};
}
// Split a visible rectangle once. Groups share references, never duplicate pixels.
QStringList cut(LayoutState &state, QRectF region) {
    QVector<LayoutPiece> next;
    QHash<QString, QStringList> replacements;
    QStringList selected;
    for (const auto &piece : state.pieces) {
        const QRectF d = piece.destination, intersection = d.intersected(region);
        if (!positive(intersection)) {
            next.append(piece);
            continue;
        }
        if (intersection == d) {
            next.append(piece);
            selected.append(piece.id);
            continue;
        }
        QVector<QRectF> parts{
            {d.left(), d.top(), d.width(), intersection.top() - d.top()},
            {d.left(), intersection.bottom(), d.width(), d.bottom() - intersection.bottom()},
            {d.left(), intersection.top(), intersection.left() - d.left(), intersection.height()},
            {intersection.right(), intersection.top(), d.right() - intersection.right(),
             intersection.height()},
            intersection};
        QStringList ids;
        for (const auto &part : parts) {
            if (!positive(part))
                continue;
            QString id = uniqueId();
            next.append({id, sourcePart(piece, part), part});
            ids.append(id);
            if (part == intersection)
                selected.append(id);
        }
        replacements.insert(piece.id, ids);
        require(next.size() <= MaxLayoutPieces, "区域过于复杂，请减少手动切分");
    }
    require(next.size() <= MaxLayoutPieces, "区域过于复杂，请减少手动切分");
    for (auto &group : state.groups) {
        QStringList ids;
        for (const auto &id : group.pieces) {
            if (replacements.contains(id))
                ids += replacements.value(id);
            else
                ids.append(id);
        }
        group.pieces = std::move(ids);
    }
    state.pieces = std::move(next);
    return selected;
}
bool close(double a, double b) {
    return std::abs(a - b) <= epsilon;
}
bool sameRectangle(QRectF a, QRectF b) {
    return close(a.left(), b.left()) && close(a.top(), b.top()) && close(a.right(), b.right()) &&
           close(a.bottom(), b.bottom());
}
struct ChangeRegion {
    QRectF from, to;
    int layer = 0;
    bool active = true;
};
bool sameTransform(const ChangeRegion &a, const ChangeRegion &b) {
    const double sx = a.to.width() / a.from.width(), sy = a.to.height() / a.from.height();
    QRectF mapped(a.to.x() + (b.from.x() - a.from.x()) * sx, a.to.y() + (b.from.y() - a.from.y()) * sy,
                  b.from.width() * sx, b.from.height() * sy);
    return sameRectangle(mapped, b.to);
}
bool adjacent(QRectF a, QRectF b, bool horizontal) {
    return horizontal
               ? close(a.top(), b.top()) && close(a.height(), b.height()) && close(a.right(), b.left())
               : close(a.left(), b.left()) && close(a.width(), b.width()) && close(a.bottom(), b.top());
}
bool mergeKeepsDrawingOrder(const QVector<ChangeRegion> &regions, int a, int b) {
    const auto &earlier = regions[regions[a].layer < regions[b].layer ? a : b];
    const auto &later = regions[regions[a].layer < regions[b].layer ? b : a];
    for (int i = 0; i < regions.size(); ++i) {
        const auto &other = regions[i];
        if (i != a && i != b && other.active && other.layer > earlier.layer && other.layer < later.layer &&
            positive(earlier.to.intersected(other.to)))
            return false;
    }
    return true;
}
QVector<ChangeRegion> combinedChanges(const LayoutState &state) {
    QVector<ChangeRegion> regions;
    for (int i = 0; i < state.pieces.size(); ++i) {
        const auto &piece = state.pieces[i];
        if (!sameRectangle(piece.source, piece.destination))
            regions.append({piece.source, piece.destination, i});
    }
    // Full shared edges and an identical affine transform allow a rectangular
    // component to absorb detector-only subdivisions. Never merge across a hole
    // or move an earlier layer over a different, intersecting change.
    bool merged;
    do {
        merged = false;
        for (bool horizontal : {true, false}) {
            QVector<int> order;
            for (int i = 0; i < regions.size(); ++i)
                if (regions[i].active)
                    order.append(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                auto ra = regions[a].from, rb = regions[b].from;
                if (horizontal)
                    return std::make_tuple(ra.y(), ra.height(), ra.x()) <
                           std::make_tuple(rb.y(), rb.height(), rb.x());
                return std::make_tuple(ra.x(), ra.width(), ra.y()) <
                       std::make_tuple(rb.x(), rb.width(), rb.y());
            });
            int previous = -1;
            for (int current : order) {
                if (previous >= 0 && adjacent(regions[previous].from, regions[current].from, horizontal) &&
                    sameTransform(regions[previous], regions[current]) &&
                    mergeKeepsDrawingOrder(regions, previous, current)) {
                    auto &a = regions[previous];
                    auto &b = regions[current];
                    a.from = a.from.united(b.from);
                    a.to = a.to.united(b.to);
                    a.layer = std::max(a.layer, b.layer);
                    b.active = false;
                    merged = true;
                } else {
                    previous = current;
                }
            }
        }
    } while (merged);
    regions.erase(
        std::remove_if(regions.begin(), regions.end(), [](const auto &region) { return !region.active; }),
        regions.end());
    std::sort(regions.begin(), regions.end(), [](const auto &a, const auto &b) { return a.layer < b.layer; });
    return regions;
}
QJsonObject geometry(QRectF r) {
    return {{"x1", r.left()}, {"y1", r.top()}, {"x2", r.right()}, {"y2", r.bottom()}};
}
void keys(const QJsonObject &object, const QStringList &expected) {
    require(object.size() == expected.size(), "布局字段缺失或包含未知字段");
    for (const auto &key : expected)
        require(object.contains(key), "布局字段不完整");
}
QRectF rectangle(const QJsonValue &value) {
    require(value.isObject(), "布局坐标格式不正确");
    auto o = value.toObject();
    keys(o, {"x1", "y1", "x2", "y2"});
    for (const auto &v : o)
        require(v.isDouble() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) <= 1000000,
                "布局坐标必须是有限数值");
    QRectF r(QPointF(o["x1"].toDouble(), o["y1"].toDouble()),
             QPointF(o["x2"].toDouble(), o["y2"].toDouble()));
    require(positive(r), "布局区域尺寸必须为正");
    return r;
}
bool fits(QRectF r, QSize size) {
    return positive(r) && std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
           std::isfinite(r.height()) && r.left() >= -epsilon && r.top() >= -epsilon &&
           r.right() <= size.width() + epsilon && r.bottom() <= size.height() + epsilon;
}
} // namespace
LayoutState createLayout(QSize size, const QVector<Candidate> &input) {
    require(size.width() > 0 && size.height() > 0 && qint64(size.width()) * size.height() <= MaxPixels,
            "图片尺寸不正确");
    LayoutState state;
    state.canvas = size;
    QRectF full(QPointF(0, 0), size);
    state.pieces.append({uniqueId(), full, full});
    auto candidates = input;
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        const bool tableA = a.target["method"].toString().startsWith("table-");
        const bool tableB = b.target["method"].toString().startsWith("table-");
        if (tableA != tableB)
            return tableA;
        return qint64(a.bounds.width()) * a.bounds.height() < qint64(b.bounds.width()) * b.bounds.height();
    });
    QVector<QRectF> seen;
    for (const auto &candidate : candidates) {
        QRectF bounds = QRectF(candidate.bounds).intersected(full);
        if (!positive(bounds) || bounds == full || seen.contains(bounds) ||
            state.groups.size() >= MaxLayoutGroups - 1)
            continue;
        LayoutState next = state;
        try {
            auto ids = cut(next, bounds);
            if (ids.isEmpty())
                continue;
            next.groups.append(
                {uniqueId(), candidate.target["label"].toString().left(1000), "detected", bounds, ids});
            state = std::move(next);
            seen.append(bounds);
        } catch (
            const std::runtime_error &) { /* Keep existing regions when the partition limit is reached. */
        }
    }
    QStringList all;
    for (const auto &piece : state.pieces)
        all.append(piece.id);
    state.groups.append({uniqueId(), "整个图片", "canvas", full, all});
    return state;
}
QString addLayoutRegion(LayoutState &state, QRectF bounds, const QString &label) {
    require(state.groups.size() < MaxLayoutGroups, "最多支持 512 个可选区域");
    bounds = bounds.intersected(QRectF(QPointF(0, 0), state.canvas));
    if (!positive(bounds))
        return {};
    LayoutState next = state;
    auto members = cut(next, bounds);
    if (members.isEmpty())
        return {};
    QString id = uniqueId();
    next.groups.append({id, label.left(1000), "manual", {}, members});
    state = std::move(next);
    return id;
}
QRectF layoutBounds(const LayoutState &state, const QString &groupId) {
    QStringList ids;
    for (const auto &group : state.groups)
        if (group.id == groupId) {
            ids = group.pieces;
            break;
        }
    QSet<QString> selected(ids.begin(), ids.end());
    QRectF result;
    for (const auto &piece : state.pieces)
        if (selected.contains(piece.id))
            result = result.isEmpty() ? piece.destination : result.united(piece.destination);
    return result;
}
QVector<LayoutChoice> layoutChoices(const LayoutState &state, QPointF point) {
    QHash<QString, QRectF> pieces;
    for (const auto &piece : state.pieces)
        pieces.insert(piece.id, piece.destination);
    QVector<LayoutChoice> result;
    for (const auto &group : state.groups) {
        QRectF bounds;
        for (const auto &id : group.pieces) {
            auto r = pieces.value(id);
            bounds = bounds.isEmpty() ? r : bounds.united(r);
        }
        if (contains(bounds, point))
            result.append({group.id, group.label, bounds});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.bounds.width() * a.bounds.height() < b.bounds.width() * b.bounds.height();
    });
    // Keep groups with different membership even if their current bounding boxes coincide.
    return result;
}
QRectF constrainLayoutRect(QRectF r, QSize size) {
    // A parent transform can legitimately make a child smaller than one pixel.
    // Constraining its position must not enlarge it or change its aspect ratio.
    const double minimum = std::nextafter(epsilon, 1.0);
    const double w = std::clamp(r.width(), minimum, double(size.width()));
    const double h = std::clamp(r.height(), minimum, double(size.height()));
    return {std::clamp(r.x(), 0.0, size.width() - w), std::clamp(r.y(), 0.0, size.height() - h), w, h};
}
QRectF resizeLayoutRect(QRectF r, QPointF delta, int handle, QSize canvas) {
    require(handle >= 0 && handle < 8, "未知缩放手柄");
    r = constrainLayoutRect(r, canvas);
    // Corners preserve aspect ratio around the opposite corner; edges affect one dimension.
    if (handle % 2 == 0) {
        bool left = handle == 0 || handle == 6, top = handle == 0 || handle == 2;
        QPointF fixed(left ? r.right() : r.left(), top ? r.bottom() : r.top());
        const double proposedW = r.width() + (left ? -delta.x() : delta.x());
        const double proposedH = r.height() + (top ? -delta.y() : delta.y());
        double factor = (proposedW * r.width() + proposedH * r.height()) /
                        (r.width() * r.width() + r.height() * r.height());
        const double limit = std::min((left ? fixed.x() : canvas.width() - fixed.x()) / r.width(),
                                      (top ? fixed.y() : canvas.height() - fixed.y()) / r.height());
        factor = std::clamp(
            factor, std::min(limit, std::min(1.0, std::max(1.0 / r.width(), 1.0 / r.height()))), limit);
        const QSizeF size(r.width() * factor, r.height() * factor);
        return {
            QPointF(left ? fixed.x() - size.width() : fixed.x(), top ? fixed.y() - size.height() : fixed.y()),
            size};
    }
    double x = r.left(), y = r.top(), right = r.right(), bottom = r.bottom();
    // Keep existing subpixel extents as the lower bound. A fixed 1px minimum
    // would jump on mouse-down and invert clamp bounds next to the canvas edge.
    const double minWidth = std::min(1.0, r.width()), minHeight = std::min(1.0, r.height());
    if (handle == 1)
        y = std::clamp(y + delta.y(), 0.0, bottom - minHeight);
    if (handle == 3)
        right = std::clamp(right + delta.x(), x + minWidth, double(canvas.width()));
    if (handle == 5)
        bottom = std::clamp(bottom + delta.y(), y + minHeight, double(canvas.height()));
    if (handle == 7)
        x = std::clamp(x + delta.x(), 0.0, right - minWidth);
    return {QPointF(x, y), QPointF(right, bottom)};
}
QRectF scaleLayoutRect(QRectF r, double factor, QSize canvas) {
    r = constrainLayoutRect(r, canvas);
    const double limit = std::min(canvas.width() / r.width(), canvas.height() / r.height());
    factor = std::clamp(factor, std::min(limit, std::min(1.0, std::max(1.0 / r.width(), 1.0 / r.height()))),
                        limit);
    auto size = QSizeF(r.width() * factor, r.height() * factor);
    return constrainLayoutRect(QRectF(r.center() - QPointF(size.width() / 2, size.height() / 2), size),
                               canvas);
}
void transformLayoutGroup(LayoutState &state, const QString &id, QRectF destination) {
    auto old = layoutBounds(state, id);
    if (old.isEmpty())
        return;
    destination = constrainLayoutRect(destination, state.canvas);
    if (destination == old)
        return;
    QStringList ids;
    for (const auto &group : state.groups)
        if (group.id == id) {
            ids = group.pieces;
            break;
        }
    QSet<QString> selected(ids.begin(), ids.end());
    QVector<LayoutPiece> background, foreground;
    for (auto piece : state.pieces) {
        if (!selected.contains(piece.id)) {
            background.append(piece);
            continue;
        }
        auto d = piece.destination;
        piece.destination = {destination.x() + (d.x() - old.x()) * destination.width() / old.width(),
                             destination.y() + (d.y() - old.y()) * destination.height() / old.height(),
                             d.width() * destination.width() / old.width(),
                             d.height() * destination.height() / old.height()};
        foreground.append(piece);
    }
    background += foreground;
    state.pieces = std::move(background);
}
void paintLayout(QPainter &painter, const QImage &original, const LayoutState &state) {
    painter.save();
    painter.setClipRect(QRectF(QPointF(0, 0), state.canvas));
    // Unchanged components are the original background. A component returned
    // to its source has no change record, so its draw order must match reload.
    for (bool changed : {false, true})
        for (const auto &piece : state.pieces)
            if ((!sameRectangle(piece.source, piece.destination)) == changed)
                painter.drawImage(piece.destination, original, piece.source);
    painter.restore();
}
QImage renderLayout(const QImage &original, const LayoutState &state) {
    QImage image(state.canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paintLayout(painter, original, state);
    return image;
}
void validateLayout(const LayoutState &state, QSize original) {
    require(state.canvas == original && !state.pieces.isEmpty() && state.pieces.size() <= MaxLayoutPieces &&
                !state.groups.isEmpty() && state.groups.size() <= MaxLayoutGroups,
            "布局大小或数量不正确");
    QSet<QString> ids;
    double total = 0;
    QVector<QRectF> sources;
    for (const auto &piece : state.pieces) {
        require(!piece.id.isEmpty() && !ids.contains(piece.id) && fits(piece.source, original) &&
                    fits(piece.destination, state.canvas),
                "布局切片坐标或编号不正确");
        ids.insert(piece.id);
        total += piece.source.width() * piece.source.height();
        sources.append(piece.source);
    }
    std::sort(sources.begin(), sources.end(), [](auto a, auto b) { return a.left() < b.left(); });
    for (int i = 0; i < sources.size(); ++i)
        for (int j = i + 1; j < sources.size() && sources[j].left() < sources[i].right() - epsilon; ++j) {
            auto overlap = sources[i].intersected(sources[j]);
            require(overlap.width() <= epsilon || overlap.height() <= epsilon, "布局切片重复使用原图像素");
        }
    require(std::abs(total - double(original.width()) * original.height()) < std::max(0.001, total * 1e-8),
            "布局切片未完整覆盖原图");
    QSet<QString> groupIds;
    for (const auto &group : state.groups) {
        require(!group.id.isEmpty() && !groupIds.contains(group.id) && group.label.size() <= 1000 &&
                    QStringList{"detected", "manual", "canvas"}.contains(group.origin) &&
                    !group.pieces.isEmpty() && group.pieces.size() <= MaxLayoutPieces,
                "布局区域格式不正确");
        groupIds.insert(group.id);
        require(group.origin == "manual" ? group.originalBounds.isNull()
                                         : fits(group.originalBounds, original),
                "布局原始区域不正确");
        QSet<QString> members;
        for (const auto &id : group.pieces) {
            require(ids.contains(id) && !members.contains(id), "布局区域引用无效");
            members.insert(id);
        }
    }
}
QJsonArray exportLayoutChanges(const LayoutState &state) {
    validateLayout(state, state.canvas);
    QJsonArray changes;
    for (const auto &region : combinedChanges(state))
        changes.append(QJsonObject{{"from", geometry(region.from)}, {"to", geometry(region.to)}});
    return changes;
}
LayoutState importLayoutChanges(const QJsonArray &changes, QSize original) {
    require(changes.size() <= MaxLayoutPieces, "变化数量超出限制");
    QVector<ChangeRegion> regions;
    for (const auto &value : changes) {
        require(value.isObject(), "变化格式不正确");
        const auto object = value.toObject();
        keys(object, {"from", "to"});
        const auto from = rectangle(object["from"]), to = rectangle(object["to"]);
        require(fits(from, original) && fits(to, original), "变化坐标超出原图");
        require(!sameRectangle(from, to), "变化列表不能包含未变化的区域");
        regions.append({from, to, int(regions.size())});
    }
    QVector<QRectF> sources;
    for (const auto &region : regions)
        sources.append(region.from);
    std::sort(sources.begin(), sources.end(), [](auto a, auto b) { return a.left() < b.left(); });
    for (int i = 0; i < sources.size(); ++i)
        for (int j = i + 1; j < sources.size() && sources[j].left() < sources[i].right() - epsilon; ++j)
            require(!positive(sources[i].intersected(sources[j])), "变化重复引用原图区域");

    auto state = createLayout(original, {});
    QStringList moved;
    // Cut every region while destination coordinates are still the original
    // coordinates. Only then apply moves, so swaps and overlapping destinations
    // never accidentally cut an already moved component.
    for (const auto &region : regions) {
        const auto selected = cut(state, region.from);
        const QSet<QString> selectedSet(selected.begin(), selected.end());
        const QString id = uniqueId();
        state.pieces.erase(std::remove_if(state.pieces.begin(), state.pieces.end(),
                                          [&](const auto &piece) { return selectedSet.contains(piece.id); }),
                           state.pieces.end());
        state.pieces.append({id, region.from, region.from});
        for (auto &group : state.groups) {
            group.pieces.erase(std::remove_if(group.pieces.begin(), group.pieces.end(),
                                              [&](const auto &piece) { return selectedSet.contains(piece); }),
                               group.pieces.end());
            if (group.origin == "canvas")
                group.pieces.append(id);
        }
        if (state.groups.size() < MaxLayoutGroups)
            state.groups.append(
                {uniqueId(), QString("调整 %1").arg(moved.size() + 1), "detected", region.from, {id}});
        moved.append(id);
    }
    QSet<QString> movedSet(moved.begin(), moved.end());
    QHash<QString, LayoutPiece> movedPieces;
    QVector<LayoutPiece> background;
    for (const auto &piece : state.pieces)
        if (movedSet.contains(piece.id))
            movedPieces.insert(piece.id, piece);
        else
            background.append(piece);
    for (int i = 0; i < regions.size(); ++i) {
        auto piece = movedPieces.value(moved[i]);
        piece.destination = regions[i].to;
        background.append(piece);
    }
    state.pieces = std::move(background);
    validateLayout(state, original);
    return state;
}
QJsonObject exportLayout(const LayoutState &state) {
    validateLayout(state, state.canvas);
    QJsonArray pieces, groups;
    for (const auto &piece : state.pieces)
        pieces.append(QJsonObject{{"id", piece.id},
                                  {"source", geometry(piece.source)},
                                  {"destination", geometry(piece.destination)}});
    for (const auto &group : state.groups)
        groups.append(QJsonObject{{"id", group.id},
                                  {"label", group.label},
                                  {"origin", group.origin},
                                  {"originalRectangle", group.originalBounds.isNull()
                                                            ? QJsonValue(QJsonValue::Null)
                                                            : QJsonValue(geometry(group.originalBounds))},
                                  {"pieceIds", QJsonArray::fromStringList(group.pieces)}});
    return {{"coordinateSpace", "image-pixels"},
            {"background", "transparent"},
            {"width", state.canvas.width()},
            {"height", state.canvas.height()},
            {"pieces", pieces},
            {"groups", groups}};
}
LayoutState importLayout(const QJsonObject &json, QSize original) {
    keys(json, {"coordinateSpace", "background", "width", "height", "pieces", "groups"});
    require(json["coordinateSpace"] == "image-pixels" && json["background"] == "transparent" &&
                json["width"].isDouble() && json["width"].toDouble() == original.width() &&
                json["height"].isDouble() && json["height"].toDouble() == original.height() &&
                json["pieces"].isArray() && json["groups"].isArray(),
            "布局坐标约定不正确");
    require(json["pieces"].toArray().size() <= MaxLayoutPieces &&
                json["groups"].toArray().size() <= MaxLayoutGroups,
            "布局数量超出限制");
    LayoutState state;
    state.canvas = original;
    for (const auto &value : json["pieces"].toArray()) {
        auto o = value.toObject();
        keys(o, {"id", "source", "destination"});
        require(o["id"].isString(), "布局编号不正确");
        state.pieces.append({o["id"].toString(), rectangle(o["source"]), rectangle(o["destination"])});
    }
    for (const auto &value : json["groups"].toArray()) {
        auto o = value.toObject();
        keys(o, {"id", "label", "origin", "originalRectangle", "pieceIds"});
        require(o["id"].isString() && o["label"].isString() && o["origin"].isString() &&
                    o["pieceIds"].isArray() && o["pieceIds"].toArray().size() <= MaxLayoutPieces,
                "布局区域字段不正确");
        LayoutGroup group{o["id"].toString(), o["label"].toString(), o["origin"].toString(), {}, {}};
        if (!o["originalRectangle"].isNull())
            group.originalBounds = rectangle(o["originalRectangle"]);
        for (const auto &id : o["pieceIds"].toArray()) {
            require(id.isString(), "布局引用格式不正确");
            group.pieces.append(id.toString());
        }
        state.groups.append(group);
    }
    validateLayout(state, original);
    return state;
}
} // namespace h2d
