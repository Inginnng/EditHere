#include "model.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace h2d {
static void fail(const QString &text) {
    throw std::runtime_error(text.toStdString());
}
QString uniqueId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}
QString timestamp() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
QJsonObject rectJson(const QRect &r) {
    return {{"x1", r.x()}, {"y1", r.y()}, {"x2", r.x() + r.width()}, {"y2", r.y() + r.height()}};
}
static int integer(const QJsonValue &v) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::floor(v.toDouble()) != v.toDouble() ||
        std::abs(v.toDouble()) > 10000000)
        fail("坐标格式不正确");
    return v.toInt();
}
QRect jsonRect(const QJsonObject &o) {
    if (o.size() != 4 || !o.contains("x1") || !o.contains("y1") || !o.contains("x2") || !o.contains("y2"))
        fail("矩形字段不正确");
    const int x = integer(o["x1"]), y = integer(o["y1"]), r = integer(o["x2"]), b = integer(o["y2"]);
    if (r <= x || b <= y)
        fail("矩形尺寸必须为正数");
    return {x, y, r - x, b - y};
}
bool containsPixel(const QRect &r, QPoint p) {
    return p.x() >= r.x() && p.y() >= r.y() && p.x() < r.x() + r.width() && p.y() < r.y() + r.height();
}
QRect dragRect(QPoint a, QPoint b, QSize s) {
    int x1 = std::clamp(std::min(a.x(), b.x()), 0, s.width()),
        y1 = std::clamp(std::min(a.y(), b.y()), 0, s.height());
    int x2 = std::clamp(std::max(a.x(), b.x()), 0, s.width()),
        y2 = std::clamp(std::max(a.y(), b.y()), 0, s.height());
    return {x1, y1, x2 - x1, y2 - y1};
}
QVector<QPoint> handles(const QRect &r) {
    int x = r.x(), y = r.y(), xx = x + r.width(), yy = y + r.height(), cx = (x + xx) / 2, cy = (y + yy) / 2;
    return {{x, y}, {cx, y}, {xx, y}, {xx, cy}, {xx, yy}, {cx, yy}, {x, yy}, {x, cy}};
}
QRect moveRect(QRect r, QPoint d, QSize s, int h) {
    if (h < 0)
        return r.translated(std::clamp(d.x(), -r.x(), s.width() - r.x() - r.width()),
                            std::clamp(d.y(), -r.y(), s.height() - r.y() - r.height()));
    int x = r.x(), y = r.y(), xx = x + r.width(), yy = y + r.height();
    if (h == 0 || h == 6 || h == 7)
        x = std::clamp(x + d.x(), 0, xx - 1);
    if (h == 0 || h == 1 || h == 2)
        y = std::clamp(y + d.y(), 0, yy - 1);
    if (h == 2 || h == 3 || h == 4)
        xx = std::clamp(xx + d.x(), x + 1, s.width());
    if (h == 4 || h == 5 || h == 6)
        yy = std::clamp(yy + d.y(), y + 1, s.height());
    return {x, y, xx - x, yy - y};
}
QJsonObject manualTarget() {
    return {{"source", "manual"},
            {"label", "手动标注"},
            {"controlType", QJsonValue::Null},
            {"automationId", QJsonValue::Null},
            {"method", "user-selection"},
            {"originalScreenBounds", QJsonValue::Null},
            {"clipped", false}};
}
QByteArray encodePng(const QImage &image) {
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        fail("无法编码图片");
    return png;
}
Document fromImage(const QImage &image, const QString &source, const QString &title) {
    if (image.isNull() || qint64(image.width()) * image.height() > MaxPixels || image.width() > 32767 ||
        image.height() > 32767)
        fail("图片过大或无法读取，最多支持 3200 万像素");
    Document doc;
    doc.image = image.convertToFormat(QImage::Format_ARGB32);
    doc.image.setDevicePixelRatio(1);
    doc.png = encodePng(doc.image);
    doc.source = source;
    doc.title = title;
    doc.imageFile = "capture-" + doc.id.left(8) + ".png";
    return doc;
}
void validateDocument(const Document &d) {
    if (d.image.isNull() || qint64(d.image.width()) * d.image.height() > MaxPixels ||
        d.image.width() > 32767 || d.image.height() > 32767 || d.notes.size() > MaxNotes)
        fail("图片或批注数量超出限制");
    if (d.id.isEmpty() || !QDateTime::fromString(d.createdAt, Qt::ISODateWithMs).isValid())
        fail("项目标识或日期不正确");
    if (!QStringList{"screen", "file", "clipboard", "demo"}.contains(d.source))
        fail("图片来源不正确");
    if (!QRegularExpression("^[^\\\\/:*?\"<>|\\x00-\\x1f]+\\.png$", QRegularExpression::CaseInsensitiveOption)
             .match(d.imageFile)
             .hasMatch())
        fail("原图文件名不正确");
    if (d.screenBounds && d.screenBounds->isEmpty())
        fail("屏幕范围不正确");
    if (d.layout)
        validateLayout(*d.layout, d.image.size());
    QSet<QString> ids;
    for (const auto &n : d.notes) {
        if (n.id.isEmpty() || ids.contains(n.id) || n.comment.trimmed().isEmpty() || n.comment.size() > 10000)
            fail("批注编号或文字不正确");
        ids.insert(n.id);
        if (n.isGlobal && n.movementSource)
            fail("全局批注不能关联移动区域");
        if (n.movementSource) {
            const auto r = *n.movementSource;
            if (n.isPoint || !std::isfinite(r.x()) || !std::isfinite(r.y()) ||
                !std::isfinite(r.width()) || !std::isfinite(r.height()) || r.isEmpty() ||
                r.x() < 0 || r.y() < 0 || r.right() > d.image.width() + 1e-7 ||
                r.bottom() > d.image.height() + 1e-7)
                fail("关联移动区域不正确");
        }
        if (n.isGlobal) {
            // Global design feedback intentionally has no canvas position.
        } else if (n.isPoint) {
            if (!containsPixel(QRect(QPoint(0, 0), d.image.size()), n.point))
                fail("点标注超出画布");
        } else if (n.rect.isEmpty() || n.rect.x() < 0 || n.rect.y() < 0 ||
                   n.rect.x() + n.rect.width() > d.image.width() ||
                   n.rect.y() + n.rect.height() > d.image.height())
            fail("框选超出画布");
        if (!QStringList{"manual", "vision", "uia", "accessibility"}.contains(
                n.target["source"].toString()) ||
            !n.target["label"].isString() || !n.target["method"].isString() || !n.target["clipped"].isBool())
            fail("批注来源格式不正确");
        if (n.target.size() != 7 ||
            (!n.target["controlType"].isNull() && !n.target["controlType"].isString()) ||
            (!n.target["automationId"].isNull() && !n.target["automationId"].isString()))
            fail("批注来源字段不正确");
        if (!n.target["originalScreenBounds"].isNull())
            jsonRect(n.target["originalScreenBounds"].toObject());
        if (!QDateTime::fromString(n.createdAt, Qt::ISODateWithMs).isValid() ||
            !QDateTime::fromString(n.updatedAt, Qt::ISODateWithMs).isValid())
            fail("批注日期不正确");
    }
}
namespace {
QJsonObject floatingRectJson(QRectF r) {
    return {{"x1", r.left()}, {"y1", r.top()}, {"x2", r.right()}, {"y2", r.bottom()}};
}
QRectF floatingJsonRect(const QJsonObject &o) {
    if (o.size() != 4)
        fail("移动区域字段不正确");
    for (const auto &key : {"x1", "y1", "x2", "y2"})
        if (!o[key].isDouble() || !std::isfinite(o[key].toDouble()) ||
            std::abs(o[key].toDouble()) > 10000000)
            fail("移动区域坐标不正确");
    const QRectF r(QPointF(o["x1"].toDouble(), o["y1"].toDouble()),
                   QPointF(o["x2"].toDouble(), o["y2"].toDouble()));
    if (r.isEmpty())
        fail("移动区域尺寸必须为正数");
    return r;
}
bool sameMovementRect(QRectF a, QRectF b) {
    return std::abs(a.left() - b.left()) < 1e-7 && std::abs(a.top() - b.top()) < 1e-7 &&
           std::abs(a.right() - b.right()) < 1e-7 && std::abs(a.bottom() - b.bottom()) < 1e-7;
}
int movementIndex(const Note &note, const QJsonArray &changes) {
    if (!note.movementSource || note.isGlobal)
        return -1;
    for (int i = 0; i < changes.size(); ++i) {
        const auto source = floatingJsonRect(changes[i].toObject()["from"].toObject());
        if (sameMovementRect(source.intersected(*note.movementSource), *note.movementSource))
            return i;
    }
    return -1;
}
// Pixel export may split a moved parent around its edited children. Match visual
// trajectories separately, giving a child's own trajectory priority over its parent.
int movementIndex(const Note &note, const QVector<LayoutMovement> &movements) {
    if (!note.movementSource || note.isGlobal)
        return -1;
    int best = -1;
    double smallestArea = std::numeric_limits<double>::max();
    for (int i = 0; i < movements.size(); ++i) {
        const auto source = movements[i].source;
        if (sameMovementRect(source, *note.movementSource))
            return i;
        const double area = source.width() * source.height();
        if (area < smallestArea &&
            sameMovementRect(source.intersected(*note.movementSource), *note.movementSource)) {
            best = i;
            smallestArea = area;
        }
    }
    return best;
}
struct NoteTransform {
    double sx, sy, tx, ty;
    QPointF map(QPointF point) const {
        return {point.x() * sx + tx, point.y() * sy + ty};
    }
    bool matches(const NoteTransform &other) const {
        return std::abs(sx - other.sx) < 1e-8 && std::abs(sy - other.sy) < 1e-8 &&
               std::abs(tx - other.tx) < 1e-8 && std::abs(ty - other.ty) < 1e-8;
    }
};
bool containsLayoutPoint(QRectF rect, QPointF point) {
    return point.x() >= rect.left() && point.y() >= rect.top() && point.x() < rect.right() &&
           point.y() < rect.bottom();
}
QPointF sourcePoint(const LayoutPiece &piece, QPointF point) {
    return {piece.source.x() +
                (point.x() - piece.destination.x()) * piece.source.width() / piece.destination.width(),
            piece.source.y() +
                (point.y() - piece.destination.y()) * piece.source.height() / piece.destination.height()};
}
NoteTransform noteTransform(const LayoutPiece &before, const LayoutPiece &after) {
    const double sx =
        after.destination.width() / after.source.width() * before.source.width() / before.destination.width();
    const double sy = after.destination.height() / after.source.height() * before.source.height() /
                      before.destination.height();
    return {sx, sy,
            after.destination.x() +
                (before.source.x() - after.source.x()) * after.destination.width() / after.source.width() -
                before.destination.x() * sx,
            after.destination.y() +
                (before.source.y() - after.source.y()) * after.destination.height() / after.source.height() -
                before.destination.y() * sy};
}
QVector<QRectF> uncoveredRectangles(QRectF rect, QRectF covered) {
    QVector<QRectF> result;
    covered = rect.intersected(covered);
    if (covered.isEmpty())
        return {rect};
    // Adjacent transformed cuts can differ by machine precision at a shared
    // edge. Use the layout engine\'s geometric tolerance, not pixel rounding,
    // so these microscopic slivers do not become separate background content.
    if (covered.top() - rect.top() > 1e-7)
        result.append({rect.left(), rect.top(), rect.width(), covered.top() - rect.top()});
    if (rect.bottom() - covered.bottom() > 1e-7)
        result.append({rect.left(), covered.bottom(), rect.width(), rect.bottom() - covered.bottom()});
    if (covered.left() - rect.left() > 1e-7)
        result.append({rect.left(), covered.top(), covered.left() - rect.left(), covered.height()});
    if (rect.right() - covered.right() > 1e-7)
        result.append({covered.right(), covered.top(), rect.right() - covered.right(), covered.height()});
    return result;
}
using NotePieceMap = QHash<QString, QVector<const LayoutPiece *>>;
QVector<const LayoutPiece *> visiblePieceOrder(const LayoutState &state) {
    QVector<const LayoutPiece *> order;
    // Match paintLayout: changed pieces cover the original background, including
    // components returned to their source after they were brought to the front.
    for (bool changed : {true, false})
        for (auto it = state.pieces.crbegin(); it != state.pieces.crend(); ++it) {
            const auto a = it->source, b = it->destination;
            const bool moved = std::abs(a.left() - b.left()) > 1e-7 || std::abs(a.top() - b.top()) > 1e-7 ||
                               std::abs(a.right() - b.right()) > 1e-7 ||
                               std::abs(a.bottom() - b.bottom()) > 1e-7;
            if (moved == changed)
                order.append(&*it);
        }
    return order;
}
NotePieceMap correspondingPieces(const LayoutState &before, const LayoutState &after) {
    QHash<QString, const LayoutPiece *> byId;
    for (const auto &piece : after.pieces)
        byId.insert(piece.id, &piece);
    NotePieceMap result;
    for (const auto &piece : before.pieces) {
        const auto *matching = byId.value(piece.id, nullptr);
        if (matching && matching->source == piece.source) {
            result.insert(piece.id, {matching});
            continue;
        }
        QVector<const LayoutPiece *> parts;
        for (const auto &other : after.pieces)
            if (!piece.source.intersected(other.source).isEmpty())
                parts.append(&other);
        result.insert(piece.id, std::move(parts));
    }
    return result;
}
std::optional<NoteTransform> rectangleNoteTransform(QRectF rectangle,
                                                    const QVector<const LayoutPiece *> &ordered,
                                                    const NotePieceMap &correspondence) {
    QVector<QRectF> remaining{rectangle};
    std::optional<NoteTransform> transform;
    // Visible topmost pixels own the annotation. A rectangle moves only when all
    // of its visible pixels share one transform, including across manual cuts.
    for (const auto *it : ordered) {
        if (remaining.isEmpty())
            break;
        QVector<QRectF> next;
        for (const auto &region : remaining) {
            const QRectF intersection = region.intersected(it->destination);
            if (intersection.width() <= 1e-7 || intersection.height() <= 1e-7) {
                next.append(region);
                continue;
            }
            const QRectF source(sourcePoint(*it, intersection.topLeft()),
                                sourcePoint(*it, intersection.bottomRight()));
            double mappedArea = 0;
            for (const auto *mapped : correspondence.value(it->id)) {
                const auto &piece = *mapped;
                const QRectF part = source.intersected(piece.source);
                if (part.isEmpty())
                    continue;
                const auto current = noteTransform(*it, piece);
                if (transform && !transform->matches(current))
                    return std::nullopt;
                transform = current;
                mappedArea += part.width() * part.height();
            }
            if (std::abs(mappedArea - source.width() * source.height()) >
                std::max(1.0, source.width() * source.height()) * 1e-8)
                return std::nullopt;
            next.append(uncoveredRectangles(region, intersection));
            if (next.size() > MaxLayoutPieces)
                return std::nullopt;
        }
        remaining = std::move(next);
    }
    return remaining.isEmpty() ? transform : std::nullopt;
}
std::optional<QRect> quantizedComponentRectangle(QRect rectangle, const LayoutState &before,
                                                 const QVector<const LayoutPiece *> &ordered,
                                                 const NotePieceMap &correspondence) {
    std::optional<QRect> result;
    std::optional<NoteTransform> shared;
    // Selecting a fractional component creates an integer enclosing rectangle.
    // Only that exact enclosure qualifies; arbitrary cross-component frames keep
    // the strict rule above. Re-enclose true bounds after mapping to avoid drift.
    for (const auto &choice : layoutChoices(before, QRectF(rectangle).center())) {
        if (choice.bounds.toAlignedRect() != rectangle)
            continue;
        const auto transform = rectangleNoteTransform(choice.bounds, ordered, correspondence);
        if (!transform)
            return std::nullopt;
        const QRectF mapped(transform->map(choice.bounds.topLeft()),
                            transform->map(choice.bounds.bottomRight()));
        const auto enclosed = mapped.toAlignedRect().intersected(QRect(QPoint(0, 0), before.canvas));
        if (enclosed.isEmpty() || (result && (*result != enclosed || !shared->matches(*transform))))
            return std::nullopt;
        result = enclosed;
        shared = transform;
    }
    return result;
}
} // namespace
int movementAnnotationIndex(const Note &note, const LayoutState &layout) {
    return movementIndex(note, exportLayoutChanges(layout));
}
QVector<MovementMarker> movementMarkers(const LayoutState &layout, const QVector<Note> &notes) {
    const auto movements = layoutMovements(layout);
    QVector<int> firstNotes(movements.size(), -1);
    for (int i = 0; i < notes.size(); ++i) {
        const int movement = movementIndex(notes[i], movements);
        if (movement >= 0 && firstNotes[movement] < 0)
            firstNotes[movement] = i;
    }
    QVector<MovementMarker> markers;
    for (int i = 0; i < movements.size(); ++i) {
        const auto &movement = movements[i];
        if (QLineF(movement.source.center(), movement.destination.center()).length() <= 0.01)
            continue;
        const int noteIndex = firstNotes[i];
        markers.append({movement.source, movement.destination, noteIndex >= 0 ? noteIndex + 1 : 0, noteIndex});
    }
    return markers;
}
QPointF movementMarkerAnchor(const MovementMarker &marker, double zoom, QSizeF viewport) {
    const QLineF line(marker.source.center() * zoom, marker.destination.center() * zoom);
    const auto clampAnchor = [&](QPointF anchor) {
        anchor.setX(std::clamp(anchor.x(), 14.0, std::max(14.0, viewport.width() - 14.0)));
        anchor.setY(std::clamp(anchor.y(), 14.0, std::max(14.0, viewport.height() - 14.0)));
        return anchor;
    };
    QPointF anchor = line.center();
    if (line.length() > 0 && line.length() < 40) {
        const QPointF normal(-line.dy() / line.length(), line.dx() / line.length());
        const auto positive = anchor + normal * 20;
        const auto negative = anchor - normal * 20;
        // Keep short arrows visible and prefer the side that fits inside the canvas.
        anchor = QLineF(positive, clampAnchor(positive)).length() <=
                         QLineF(negative, clampAnchor(negative)).length()
                     ? positive
                     : negative;
    }
    return clampAnchor(anchor);
}
std::optional<QRectF> movementAnnotationDestination(const Note &note, const LayoutState &layout) {
    if (!note.movementSource || note.isGlobal)
        return std::nullopt;
    // The selected parent retains its own frame even when a child moves outside it.
    for (const auto &movement : layoutMovements(layout))
        if (sameMovementRect(movement.source, *note.movementSource))
            return movement.destination;
    std::optional<NoteTransform> transform;
    double covered = 0;
    for (const auto &piece : layout.pieces) {
        const auto intersection = piece.source.intersected(*note.movementSource);
        if (intersection.width() <= 1e-7 || intersection.height() <= 1e-7)
            continue;
        const double sx = piece.destination.width() / piece.source.width();
        const double sy = piece.destination.height() / piece.source.height();
        const NoteTransform current{sx, sy, piece.destination.x() - piece.source.x() * sx,
                                    piece.destination.y() - piece.source.y() * sy};
        if (transform && !transform->matches(current))
            return std::nullopt;
        transform = current;
        covered += intersection.width() * intersection.height();
    }
    const double area = note.movementSource->width() * note.movementSource->height();
    if (!transform || std::abs(covered - area) > std::max(1.0, area) * 1e-8)
        return std::nullopt;
    return QRectF(transform->map(note.movementSource->topLeft()),
                  transform->map(note.movementSource->bottomRight()));
}
QVector<Note> remapNotes(const QVector<Note> &notes, const LayoutState &before, const LayoutState &after) {
    if (before.canvas != after.canvas || before.canvas.isEmpty())
        return notes;
    auto result = notes;
    const auto ordered = visiblePieceOrder(before);
    const auto correspondence = correspondingPieces(before, after);
    const int width = after.canvas.width(), height = after.canvas.height();
    for (auto &note : result) {
        if (note.isGlobal)
            continue;
        if (note.movementSource) {
            if (const auto destination = movementAnnotationDestination(note, after)) {
                note.rect = destination->toAlignedRect().intersected(QRect(QPoint(0, 0), after.canvas));
                continue;
            }
        }
        if (note.isPoint) {
            // IDs can change during a manual cut; match the original pixels.
            for (const auto *it : ordered) {
                if (!containsLayoutPoint(it->destination, note.point))
                    continue;
                const auto source = sourcePoint(*it, note.point);
                for (const auto *mapped : correspondence.value(it->id)) {
                    const auto &piece = *mapped;
                    if (!containsLayoutPoint(piece.source, source))
                        continue;
                    const auto destination = noteTransform(*it, piece).map(note.point);
                    note.point = {std::clamp(qRound(destination.x()), 0, width - 1),
                                  std::clamp(qRound(destination.y()), 0, height - 1)};
                    break;
                }
                break;
            }
        } else if (auto transform = rectangleNoteTransform(QRectF(note.rect), ordered, correspondence)) {
            const auto topLeft = transform->map(QPointF(note.rect.topLeft()));
            const auto bottomRight = transform->map(
                QPointF(note.rect.x() + note.rect.width(), note.rect.y() + note.rect.height()));
            const int x1 = std::clamp(qRound(topLeft.x()), 0, width - 1);
            const int y1 = std::clamp(qRound(topLeft.y()), 0, height - 1);
            const int x2 = std::clamp(qRound(bottomRight.x()), x1 + 1, width);
            const int y2 = std::clamp(qRound(bottomRight.y()), y1 + 1, height);
            note.rect = {x1, y1, x2 - x1, y2 - y1};
        } else if (auto enclosed = quantizedComponentRectangle(note.rect, before, ordered, correspondence)) {
            note.rect = *enclosed;
        }
    }
    return result;
}
QJsonObject exportFeedback(const Document &doc, bool embed) {
    validateDocument(doc);
    const auto changes = doc.layout ? exportLayoutChanges(*doc.layout) : QJsonArray{};
    QJsonArray annotations;
    for (const auto &note : doc.notes) {
        QJsonObject annotation{{"text", note.comment}};
        const int changeIndex = movementIndex(note, changes);
        if (note.isGlobal) {
            // A text-only annotation applies to the whole design.
        } else if (changeIndex >= 0)
            annotation.insert("change", changeIndex);
        else if (note.isPoint)
            annotation.insert("point", QJsonObject{{"x", note.point.x()}, {"y", note.point.y()}});
        else
            annotation.insert("rectangle", rectJson(note.rect));
        annotations.append(annotation);
    }
    QJsonObject result{{"annotationSpace", "result"},
                       {"annotations", annotations},
                       {"changes", changes}};
    if (embed) {
        validateProjectStorageSize(((qint64(doc.png.size()) + 2) / 3) * 4, doc.png.size());
        result.insert("image", "data:image/png;base64," + QString::fromLatin1(doc.png.toBase64()));
    }
    return result;
}
QByteArray serializeFeedback(const Document &doc, bool embed) {
    auto bytes = QJsonDocument(exportFeedback(doc, embed)).toJson(QJsonDocument::Compact);
    validateProjectStorageSize(bytes.size() + 1);
    return bytes + '\n';
}
static QJsonObject documentObject(const Document &d, bool embed, bool current) {
    validateDocument(d);
    QVector<Note> legacyNotes = d.notes;
    if (!current && std::any_of(d.notes.begin(), d.notes.end(), [](const Note &note) {
            return note.isGlobal || note.movementSource.has_value();
        }))
        fail("这些批注需要当前 HelpDesign 项目格式");
    if (d.layout && !current) {
        const auto original = createLayout(d.image.size(), {});
        legacyNotes = remapNotes(d.notes, *d.layout, original);
        if (remapNotes(legacyNotes, original, *d.layout) != d.notes)
            fail("这些批注无法用旧版原图坐标保存，请使用当前反馈格式");
    }
    QJsonObject capture{
        {"id", d.id},
        {"createdAt", d.createdAt},
        {"source", d.source},
        {"title", d.title},
        {"imageFile", d.imageFile},
        {"width", d.image.width()},
        {"height", d.image.height()},
        {"coordinateSpace", "image-pixels"},
        {"origin", "top-left"},
        {"rectangleConvention", "top-left-inclusive-bottom-right-exclusive"},
        {"screenBounds",
         d.screenBounds ? QJsonValue(rectJson(*d.screenBounds)) : QJsonValue(QJsonValue::Null)},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(d.png, QCryptographicHash::Sha256).toHex())},
        {"pngBase64",
         embed ? QJsonValue(QString::fromLatin1(d.png.toBase64())) : QJsonValue(QJsonValue::Null)}};
    QJsonArray notes;
    int number = 0;
    bool ax = false;
    for (const auto &n : legacyNotes) {
        ax |= n.target["source"] == "accessibility";
        QJsonObject item{
            {"id", n.id},
            {"number", ++number},
            {"kind", n.isGlobal ? "global" : n.isPoint ? "point" : "rectangle"},
            {"point", n.isPoint && !n.isGlobal ? QJsonValue(QJsonObject{{"x", n.point.x()}, {"y", n.point.y()}})
                                : QJsonValue(QJsonValue::Null)},
            {"rectangle", n.isPoint || n.isGlobal ? QJsonValue(QJsonValue::Null) : QJsonValue(rectJson(n.rect))},
            {"comment", n.comment},
            {"target", n.target},
            {"createdAt", n.createdAt},
            {"updatedAt", n.updatedAt}};
        if (n.movementSource)
            item.insert("movementSource", floatingRectJson(*n.movementSource));
        notes.append(item);
    }
    QJsonObject result{{"schemaVersion", current ? "3.0.0" : d.layout ? "2.0.0"
                                         : ax     ? "1.1.0"
                                                  : "1.0.0"},
                       {"tool", current ? "HelpDesign" : "Help2Design Capture"},
                       {"exportedAt", timestamp()},
                       {"capture", capture},
                       {"annotations", notes}};
    if (d.layout) {
        auto layout = exportLayout(*d.layout);
        if (!current)
            layout.remove("movements");
        result.insert("layout", layout);
    }
    if (current) {
        result.insert("annotationSpace", "result");
        if (!d.layout)
            result.insert("layout", QJsonValue::Null);
    }
    return result;
}
QJsonObject exportDocument(const Document &doc, bool embed) {
    return documentObject(doc, embed, false);
}
void validateProjectStorageSize(qint64 jsonBytes, qint64 externalImageBytes) {
    if (jsonBytes < 0 || jsonBytes > MaxProjectFileBytes)
        fail("项目不能超过 96 MiB，未保存当前修改。请缩小图片或减少批注和分块后重试");
    if (externalImageBytes < 0 || externalImageBytes > MaxImageFileBytes)
        fail("配套原图不能超过 48 MiB，请缩小图片后重试");
}
QByteArray serializeDocument(const Document &doc, bool embed) {
    // Reject an oversized embedded image before allocating its Base64 representation.
    if (embed)
        validateProjectStorageSize(((qint64(doc.png.size()) + 2) / 3) * 4);
    else
        validateProjectStorageSize(0, doc.png.size());
    auto bytes = QJsonDocument(documentObject(doc, embed, true)).toJson(QJsonDocument::Indented);
    validateProjectStorageSize(bytes.size());
    return bytes;
}
static void exactKeys(const QJsonObject &o, const QStringList &keys) {
    if (o.size() != keys.size())
        fail("项目字段缺失或包含未知字段");
    for (const auto &k : keys)
        if (!o.contains(k))
            fail("项目缺少字段：" + k);
}
static void feedbackFields(const QJsonObject &feedback) {
    QStringList fields{"annotations", "changes"};
    if (feedback.contains("annotationSpace")) {
        if (feedback["annotationSpace"] != "result")
            fail("批注坐标空间不正确");
        fields.append("annotationSpace");
        if (feedback.contains("image"))
            fields.append("image");
    }
    exactKeys(feedback, fields);
}
static QByteArray feedbackPng(const QJsonValue &value) {
    const QString prefix = "data:image/png;base64,";
    if (!value.isString())
        fail("内嵌原图必须是 PNG data URL");
    const QString data = value.toString();
    if (!data.startsWith(prefix))
        fail("内嵌原图必须是 PNG data URL");
    const qint64 encodedSize = data.size() - prefix.size();
    if (encodedSize <= 0 || encodedSize > ((MaxImageFileBytes + 2) / 3) * 4)
        fail("内嵌原图不能超过 48 MiB");
    const QByteArray encoded = data.mid(prefix.size()).toLatin1();
    auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64() != encoded)
        fail("原图 Base64 不正确");
    validateProjectStorageSize(0, decoded.decoded.size());
    return decoded.decoded;
}
static QImage readFeedbackPng(QByteArray &png) {
    if (png.size() < 33 || !png.startsWith(QByteArray::fromHex("89504e470d0a1a0a0000000d49484452")))
        fail("内嵌原图不是有效的 PNG");
    const auto *header = reinterpret_cast<const uchar *>(png.constData());
    const quint32 width = qFromBigEndian<quint32>(header + 16);
    const quint32 height = qFromBigEndian<quint32>(header + 20);
    if (!width || !height || width > 32767 || height > 32767 || quint64(width) * height > MaxPixels)
        fail("内嵌原图尺寸超出限制");
    QBuffer buffer(&png);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const QSize size{int(width), int(height)};
    if (reader.size() != size)
        fail("内嵌原图尺寸不正确");
    auto image = reader.read();
    if (image.isNull() || image.size() != size)
        fail("内嵌原图无法读取");
    return image;
}
Document loadFeedback(const QJsonObject &feedback, const QImage &original) {
    feedbackFields(feedback);
    if (!feedback["annotations"].isArray() || !feedback["changes"].isArray() ||
        feedback["annotations"].toArray().size() > MaxNotes)
        fail("批注或变化列表格式不正确");
    QImage image = original;
    QByteArray embedded;
    if (feedback.contains("image")) {
        embedded = feedbackPng(feedback["image"]);
        image = readFeedbackPng(embedded);
        if (!original.isNull() &&
            image.convertToFormat(QImage::Format_ARGB32) != original.convertToFormat(QImage::Format_ARGB32))
            fail("内嵌原图与提供的图片不一致");
    }
    auto doc = fromImage(image, "file", "设计反馈");
    if (!embedded.isEmpty())
        doc.png = embedded;
    const auto changes = feedback["changes"].toArray();
    if (!changes.isEmpty())
        doc.layout = importLayoutChanges(changes, image.size());
    for (const auto &value : feedback["annotations"].toArray()) {
        if (!value.isObject())
            fail("批注格式不正确");
        auto annotation = value.toObject();
        const bool point = annotation.contains("point");
        const bool movement = annotation.contains("change");
        const bool global = !point && !movement && !annotation.contains("rectangle");
        exactKeys(annotation, global ? QStringList{"text"} : movement ? QStringList{"change", "text"}
                              : point ? QStringList{"point", "text"} : QStringList{"rectangle", "text"});
        if (!annotation["text"].isString())
            fail("批注文字格式不正确");
        Note note;
        note.isPoint = point;
        note.isGlobal = global;
        note.comment = annotation["text"].toString();
        if (global) {
            note.isPoint = true;
        } else if (movement) {
            const int index = integer(annotation["change"]);
            if (index < 0 || index >= changes.size())
                fail("批注关联的移动不存在");
            const auto change = changes[index].toObject();
            note.movementSource = floatingJsonRect(change["from"].toObject());
            note.rect = floatingJsonRect(change["to"].toObject()).toAlignedRect()
                            .intersected(QRect(QPoint(0, 0), image.size()));
        } else if (point) {
            if (!annotation["point"].isObject())
                fail("点标注格式不正确");
            const auto position = annotation["point"].toObject();
            exactKeys(position, {"x", "y"});
            note.point = {integer(position["x"]), integer(position["y"])};
        } else {
            if (!annotation["rectangle"].isObject())
                fail("框标注格式不正确");
            note.rect = jsonRect(annotation["rectangle"].toObject());
        }
        doc.notes.append(note);
    }
    validateDocument(doc);
    if (doc.layout && !feedback.contains("annotationSpace"))
        doc.notes = remapNotes(doc.notes, createLayout(image.size(), {}), *doc.layout);
    validateDocument(doc);
    return doc;
}
Document loadDocument(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("无法打开文件");
    if (!path.endsWith(".json", Qt::CaseInsensitive) &&
        !path.endsWith(".helpdesign", Qt::CaseInsensitive)) {
        if (file.size() > MaxImageFileBytes)
            fail("图片文件不能超过 48 MB");
        QImageReader reader(&file);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > MaxPixels)
            fail("图片尺寸超出限制");
        return fromImage(reader.read(), "file", QFileInfo(path).fileName());
    }
    if (file.size() > MaxProjectFileBytes)
        fail("项目文件不能超过 96 MB");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        fail("JSON 格式不正确");
    auto root = parsed.object();
    if (!root.contains("schemaVersion")) {
        feedbackFields(root);
        const QFileInfo jsonFile(path);
        if (root.contains("image")) {
            auto document = loadFeedback(root, {});
            document.imageFile = jsonFile.completeBaseName() + ".png";
            document.title = jsonFile.completeBaseName();
            validateDocument(document);
            return document;
        }
        const QString imagePath = jsonFile.dir().filePath(jsonFile.completeBaseName() + ".png");
        if (!QFileInfo::exists(imagePath))
            fail("请将 JSON 与同名原图 " + QFileInfo(imagePath).fileName() + " 放在同一目录");
        const auto imageDocument = loadDocument(imagePath);
        auto document = loadFeedback(root, imageDocument.image);
        document.png = imageDocument.png;
        document.imageFile = QFileInfo(imagePath).fileName();
        document.title = jsonFile.completeBaseName();
        return document;
    }
    const QString version = root["schemaVersion"].toString();
    const bool current = version == "3.0.0";
    if (!QStringList{"1.0.0", "1.1.0", "2.0.0", "3.0.0"}.contains(version) ||
        root["tool"] != (current ? "HelpDesign" : "Help2Design Capture"))
        fail("不支持这个项目版本");
    QStringList fields{"schemaVersion", "tool", "exportedAt", "capture", "annotations"};
    if (version == "2.0.0" || current)
        fields.append("layout");
    if (current) {
        fields.append("annotationSpace");
        if (root["annotationSpace"] != "result" ||
            (!root["layout"].isNull() && !root["layout"].isObject()))
            fail("项目坐标或布局格式不正确");
    }
    exactKeys(root, fields);
    if (version == "2.0.0" && !root["layout"].isObject())
        fail("大爆炸项目必须包含布局对象");
    if (!QDateTime::fromString(root["exportedAt"].toString(), Qt::ISODateWithMs).isValid())
        fail("导出日期不正确");
    if (!root["annotations"].isArray() || root["annotations"].toArray().size() > MaxNotes)
        fail("批注列表格式或数量不正确");
    const auto c = root["capture"].toObject();
    exactKeys(c, {"id", "createdAt", "source", "title", "imageFile", "width", "height", "coordinateSpace",
                  "origin", "rectangleConvention", "screenBounds", "sha256", "pngBase64"});
    if (c["coordinateSpace"] != "image-pixels" || c["origin"] != "top-left" ||
        c["rectangleConvention"] != "top-left-inclusive-bottom-right-exclusive")
        fail("项目坐标约定不兼容");
    if (!c["title"].isString())
        fail("项目标题格式不正确");
    const int w = integer(c["width"]), h = integer(c["height"]);
    if (w <= 0 || h <= 0 || qint64(w) * h > MaxPixels)
        fail("项目图片尺寸不正确");
    const QString name = c["imageFile"].toString();
    if (QFileInfo(name).fileName() != name || name.contains('\\') || name.contains('/') ||
        !name.endsWith(".png", Qt::CaseInsensitive))
        fail("原图文件名不正确");
    QByteArray png;
    if (c["pngBase64"].isString()) {
        auto result = QByteArray::fromBase64Encoding(c["pngBase64"].toString().toLatin1(),
                                                     QByteArray::AbortOnBase64DecodingErrors);
        if (!result)
            fail("原图 Base64 不正确");
        png = result.decoded;
    } else if (c["pngBase64"].isNull()) {
        QFile original(QFileInfo(path).dir().filePath(name));
        if (!original.open(QIODevice::ReadOnly) || original.size() > MaxImageFileBytes)
            fail("请将 JSON 与原图 " + name + " 放在同一目录");
        png = original.readAll();
    } else
        fail("原图数据字段不正确");
    if (!png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) ||
        QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()) !=
            c["sha256"].toString())
        fail("原图校验不一致，无法保证标注位置");
    QBuffer pngBuffer(&png);
    pngBuffer.open(QIODevice::ReadOnly);
    QImageReader pngReader(&pngBuffer, "PNG");
    if (pngReader.size() != QSize(w, h))
        fail("原图尺寸与项目不一致");
    QImage image = pngReader.read();
    if (image.size() != QSize(w, h))
        fail("原图尺寸与项目不一致");
    Document d = fromImage(image, c["source"].toString(), c["title"].toString());
    d.png = png;
    d.id = c["id"].toString();
    d.createdAt = c["createdAt"].toString();
    d.imageFile = name;
    if (!c["screenBounds"].isNull())
        d.screenBounds = jsonRect(c["screenBounds"].toObject());
    if (!root["annotations"].isArray())
        fail("批注列表格式不正确");
    int number = 0;
    for (const auto &value : root["annotations"].toArray()) {
        auto o = value.toObject();
        QStringList noteFields{"id", "number", "kind", "point", "rectangle", "comment", "target",
                                "createdAt", "updatedAt"};
        if (current && o.contains("movementSource"))
            noteFields.append("movementSource");
        exactKeys(o, noteFields);
        if (integer(o["number"]) != ++number)
            fail("批注编号必须连续");
        if (o["kind"] != "point" && o["kind"] != "rectangle" && !(current && o["kind"] == "global"))
            fail("批注类型不正确");
        Note n;
        n.id = o["id"].toString();
        n.isGlobal = o["kind"] == "global";
        n.isPoint = n.isGlobal || o["kind"] == "point";
        if (o.contains("movementSource")) {
            if (!o["movementSource"].isObject())
                fail("关联移动区域格式不正确");
            n.movementSource = floatingJsonRect(o["movementSource"].toObject());
        }
        n.comment = o["comment"].toString();
        n.createdAt = o["createdAt"].toString();
        n.updatedAt = o["updatedAt"].toString();
        n.target = o["target"].toObject();
        if (root["schemaVersion"] == "1.0.0" && n.target["source"] == "accessibility")
            fail("辅助功能元素需要 1.1.0 格式");
        exactKeys(n.target, {"source", "label", "controlType", "automationId", "method",
                             "originalScreenBounds", "clipped"});
        if (n.isGlobal) {
            if (!o["rectangle"].isNull() || !o["point"].isNull())
                fail("全局批注不能包含坐标");
        } else if (n.isPoint) {
            if (!o["rectangle"].isNull())
                fail("点标注不能带框坐标");
            auto p = o["point"].toObject();
            exactKeys(p, {"x", "y"});
            n.point = {integer(p["x"]), integer(p["y"])};
        } else {
            if (!o["point"].isNull())
                fail("框标注不能带点坐标");
            auto r = o["rectangle"].toObject();
            exactKeys(r, {"x1", "y1", "x2", "y2"});
            n.rect = jsonRect(r);
        }
        d.notes.append(n);
    }
    if (version == "2.0.0" || (current && root["layout"].isObject()))
        d.layout = importLayout(root["layout"].toObject(), d.image.size());
    validateDocument(d);
    if (d.layout && !current)
        d.notes = remapNotes(d.notes, createLayout(d.image.size(), {}), *d.layout);
    validateDocument(d);
    return d;
}
void saveBytes(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        fail("文件保存失败，请检查目录权限和剩余空间");
}
void CandidatePicker::reset() {
    levels_.clear();
    index_ = 0;
    anchor_.reset();
}
std::optional<Candidate> CandidatePicker::current() const {
    return levels_.isEmpty() ? std::nullopt : std::optional<Candidate>(levels_[index_]);
}
void CandidatePicker::update(const QVector<Candidate> &all, QPoint p) {
    const auto previous = current();
    const bool nearby = anchor_ && (p - *anchor_).manhattanLength() <= 8;
    QVector<Candidate> candidates;
    for (const auto &candidate : all) {
        if (!containsPixel(candidate.bounds, p))
            continue;
        const bool duplicate =
            std::any_of(candidates.cbegin(), candidates.cend(),
                        [&](const Candidate &other) { return other.bounds == candidate.bounds; });
        if (!duplicate)
            candidates.append(candidate);
    }
    auto area = [](const QRect &r) { return qint64(r.width()) * r.height(); };
    // Containment of the pointer is the only hierarchy requirement. Overlapping
    // regions may describe different useful selections without containing each other.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Candidate &a, const Candidate &b) { return area(a.bounds) < area(b.bounds); });
    levels_ = std::move(candidates);
    index_ = 0;
    if (nearby && previous)
        for (int i = 0; i < levels_.size(); i++)
            if (levels_[i].bounds == previous->bounds) {
                index_ = i;
                break;
            }
    if (!nearby)
        anchor_ = p;
}
void CandidatePicker::step(int d) {
    if (d == 0)
        return;
    index_ = std::clamp(index_ + (d > 0 ? 1 : -1), 0, std::max(0, int(levels_.size()) - 1));
}
void History::push(const QVector<Note> &n) {
    undo_.append(n);
    if (undo_.size() > 100)
        undo_.removeFirst();
    redo_.clear();
}
QVector<Note> History::undo(const QVector<Note> &n) {
    if (undo_.isEmpty())
        return n;
    redo_.append(n);
    return undo_.takeLast();
}
QVector<Note> History::redo(const QVector<Note> &n) {
    if (redo_.isEmpty())
        return n;
    undo_.append(n);
    return redo_.takeLast();
}
void History::clear() {
    undo_.clear();
    redo_.clear();
}
QImage exampleImage() {
    QImage image(1120, 720, QImage::Format_ARGB32);
    image.fill(QColor("#f4f3ee"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(0, 0, 1120, 80, Qt::white);
    p.setPen(QColor("#35453c"));
    p.setFont(QFont("Segoe UI", 14));
    p.drawText(44, 48, "FIELDNOTES");
    p.setFont(QFont("Segoe UI", 11));
    p.drawText(878, 48, "探索     收藏     关于");
    p.setFont(QFont("Microsoft YaHei", 28, QFont::DemiBold));
    p.drawText(48, 164, "为日常，留一点空白。");
    p.setFont(QFont("Microsoft YaHei", 11));
    p.setPen(QColor("#788278"));
    p.drawText(48, 203, "点选这里，写下你希望改变的细节。");
    p.setBrush(QColor("#456b51"));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(870, 124, 200, 48), 9, 9);
    p.setPen(Qt::white);
    p.drawText(QRect(870, 124, 200, 48), Qt::AlignCenter, "发现灵感  →");
    QStringList titles{"林间的光", "山的轮廓", "慢一点的午后"};
    for (int i = 0; i < 3; i++) {
        int x = 48 + i * 348;
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(x, 268, 328, 366), 12, 12);
        p.fillRect(x + 16, 284, 296, 218, QColor("#e0e7dc"));
        p.setBrush(QColor("#456b51"));
        if (i == 0) {
            p.drawEllipse(QRectF(x + 80, 316, 160, 134));
            p.fillRect(x + 148, 421, 24, 81, QColor("#456b51"));
        } else if (i == 1)
            p.drawPolygon(QPolygonF{{qreal(x + 42), 472}, {qreal(x + 156), 310}, {qreal(x + 284), 472}});
        else {
            p.setBrush(QColor("#fcfcf9"));
            p.setPen(QPen(QColor("#97ab90"), 3));
            p.drawEllipse(QRectF(x + 63, 330, 203, 116));
        }
        p.setPen(QColor("#35453c"));
        p.setFont(QFont("Microsoft YaHei", 13));
        p.drawText(x + 22, 551, titles[i]);
        p.setPen(QColor("#929c91"));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(x + 22, 594, QString("生活观察   /   VOL. 0%1").arg(i + 1));
    }
    p.setPen(QColor("#8b948a"));
    p.drawText(48, 680, "© FIELDNOTES · 示例图片，仅用于体验批注");
    return image;
}
QImage previewImage(const Document &doc) {
    QFont font("Microsoft YaHei", 11);
    QFontMetrics fm(font);
    QVector<int> heights;
    int total = 76;
    for (const auto &n : doc.notes) {
        int h = std::max(
            110, fm.boundingRect(QRect(0, 0, 282, 100000), Qt::TextWordWrap, n.comment).height() + 72);
        heights.append(h);
        total += h + 12;
    }
    int width = doc.image.width() + 408, height = std::max(doc.image.height() + 48, total + 24);
    if (qint64(width) * height > MaxPixels || height > 32767)
        fail("预览图片过大，请保存项目");
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor("#f5f5f7"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(24, 24, doc.layout ? renderLayout(doc.image, *doc.layout) : doc.image);
    const auto markers = doc.layout ? movementMarkers(*doc.layout, doc.notes) : QVector<MovementMarker>{};
    for (const auto &marker : markers) {
        const auto from = marker.source.center() + QPointF(24, 24);
        const auto to = marker.destination.center() + QPointF(24, 24);
        const QLineF line(from, to);
        const auto direction = (to - from) / line.length();
        const QPointF normal(-direction.y(), direction.x());
        const double head = std::min(8.0, line.length() * 0.4);
        const QColor arrow(0, 122, 255, 95);
        p.setPen(QPen(arrow, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(arrow);
        p.drawLine(from, to);
        p.drawEllipse(from, 2, 2);
        p.drawLine(to, to - direction * head + normal * head * 0.5);
        p.drawLine(to, to - direction * head - normal * head * 0.5);
    }
    auto badge = [&](QPointF c, int n) {
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(QColor("#007aff"));
        p.drawEllipse(c, 14, 14);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(c.x() - 14, c.y() - 14, 28, 28), Qt::AlignCenter, QString::number(n));
    };
    QHash<int, QPointF> movementAnchors;
    for (const auto &marker : markers)
        if (marker.noteIndex >= 0)
            movementAnchors.insert(marker.noteIndex,
                                   movementMarkerAnchor(marker, 1, doc.image.size()) + QPointF(24, 24));
    int y = 72, i = 0, x = doc.image.width() + 60;
    p.setFont(QFont("Microsoft YaHei", 13, QFont::DemiBold));
    p.setPen(QColor("#242426"));
    p.drawText(x, 44, QString("批注 %1 条").arg(doc.notes.size()));
    for (const auto &n : doc.notes) {
        if (!n.isGlobal && !n.isPoint) {
            p.setPen(QPen(QColor("#007aff"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(n.rect).translated(24, 24));
        }
        if (!n.isGlobal)
            badge(movementAnchors.value(i, QPointF(n.isPoint ? n.point : n.rect.topLeft()) + QPointF(24, 24)),
                  i + 1);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(x, y, 324, heights[i]), 12, 12);
        badge(QPointF(x + 28, y + 26), i + 1);
        p.setPen(QColor("#85858b"));
        p.setFont(QFont("Segoe UI", 9));
        QString coords = n.isGlobal ? QString("整体意见") : n.isPoint ? QString("(%1, %2)").arg(n.point.x()).arg(n.point.y())
                                   : QString("(%1, %2) → (%3, %4)")
                                         .arg(n.rect.x())
                                         .arg(n.rect.y())
                                         .arg(n.rect.x() + n.rect.width())
                                         .arg(n.rect.y() + n.rect.height());
        p.drawText(x + 52, y + 31, coords);
        p.setPen(QColor("#242426"));
        p.setFont(font);
        p.drawText(QRect(x + 18, y + 50, 288, heights[i] - 56), Qt::TextWordWrap, n.comment);
        y += heights[i++] + 12;
    }
    return image;
}
} // namespace h2d
