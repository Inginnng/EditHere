#pragma once
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>
#include <QVector>
#include <optional>
class QPainter;
namespace h2d {
struct Candidate;
constexpr int MaxLayoutPieces = 8192;
constexpr int MaxLayoutGroups = 512;
constexpr int MaxLayoutMovements = MaxLayoutPieces + MaxLayoutGroups;
struct LayoutPiece {
    QString id;
    QRectF source, destination;
    bool operator==(const LayoutPiece &) const = default;
};
struct LayoutGroup {
    QString id, label, origin;
    QRectF originalBounds; // Empty for a manually selected region in the current composition.
    QStringList pieces;
    bool operator==(const LayoutGroup &) const = default;
};
struct LayoutMovement {
    QString groupId; // Empty only for a legacy pixel change without a matching selectable group.
    QRectF source, destination;
    bool operator==(const LayoutMovement &) const = default;
};
struct LayoutState {
    QSize canvas;
    QVector<LayoutPiece> pieces;
    QVector<LayoutGroup> groups;
    // Missing history in old projects falls back to reconstructed pixel changes.
    std::optional<QVector<LayoutMovement>> movements;
    bool operator==(const LayoutState &) const = default;
};
struct LayoutChoice {
    QString id, label;
    QRectF bounds;
};
LayoutState createLayout(QSize size, const QVector<Candidate> &candidates);
QString addLayoutRegion(LayoutState &state, QRectF currentBounds, const QString &label = "手动区域");
QRectF layoutBounds(const LayoutState &state, const QString &groupId);
QVector<LayoutChoice> layoutChoices(const LayoutState &state, QPointF point);
QRectF constrainLayoutRect(QRectF rect, QSize canvas);
QRectF resizeLayoutRect(QRectF rect, QPointF delta, int handle, QSize canvas);
QRectF scaleLayoutRect(QRectF rect, double factor, QSize canvas);
void transformLayoutGroup(LayoutState &state, const QString &id, QRectF destination);
QVector<LayoutMovement> layoutMovements(const LayoutState &state);
void paintLayout(QPainter &painter, const QImage &original, const LayoutState &state);
QImage renderLayout(const QImage &original, const LayoutState &state);
void validateLayout(const LayoutState &state, QSize originalSize);
QJsonArray exportLayoutChanges(const LayoutState &state);
LayoutState importLayoutChanges(const QJsonArray &changes, QSize originalSize);
QJsonObject exportLayout(const LayoutState &state);
LayoutState importLayout(const QJsonObject &json, QSize originalSize);
} // namespace h2d
