#pragma once
#include "layout.h"
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

namespace h2d {
constexpr qint64 MaxPixels = 32000000;
constexpr int MaxNotes = 1000;
constexpr qint64 MaxImageFileBytes = 48LL * 1024 * 1024;
constexpr qint64 MaxProjectFileBytes = 96LL * 1024 * 1024;
QString uniqueId();
QString timestamp();
QJsonObject rectJson(const QRect &rect);
QRect jsonRect(const QJsonObject &json);
bool containsPixel(const QRect &rect, QPoint point);
QRect dragRect(QPoint a, QPoint b, QSize bounds);
QRect moveRect(QRect rect, QPoint delta, QSize bounds, int handle = -1);
QVector<QPoint> handles(const QRect &rect);
struct Candidate {
    QRect bounds;
    QJsonObject target;
};
QJsonObject manualTarget();
struct Note {
    QString id = uniqueId();
    bool isPoint = true;
    bool isGlobal = false;
    std::optional<QRectF> movementSource;
    QPoint point;
    QRect rect;
    QString comment;
    QJsonObject target = manualTarget();
    QString createdAt = timestamp();
    QString updatedAt = createdAt;
    bool operator==(const Note &) const = default;
};
struct MovementMarker {
    QRectF source, destination;
    int number = 0;
    int noteIndex = -1;
};
struct Document {
    QString id = uniqueId();
    QString createdAt = timestamp();
    QString title;
    QString source = "file";
    QString imageFile;
    QImage image;
    QByteArray png;
    std::optional<QRect> screenBounds;
    QVector<Note> notes;
    QVector<Candidate> candidates;
    std::optional<LayoutState> layout;
    bool dirty = false;
};
QByteArray encodePng(const QImage &image);
Document fromImage(const QImage &image, const QString &source, const QString &title);
Document loadDocument(const QString &path);
QJsonObject exportFeedback(const Document &doc, bool embed = false, bool compress = false);
QByteArray serializeFeedback(const Document &doc, bool embed = false, bool compress = false);
int movementAnnotationIndex(const Note &note, const LayoutState &layout);
QVector<MovementMarker> movementMarkers(const LayoutState &layout, const QVector<Note> &notes);
QPointF movementMarkerAnchor(const MovementMarker &marker, double zoom, QSizeF viewport);
std::optional<QRectF> movementAnnotationDestination(const Note &note, const LayoutState &layout);
QVector<Note> remapNotes(const QVector<Note> &notes, const LayoutState &before, const LayoutState &after);
Document loadFeedback(const QJsonObject &feedback, const QImage &original);
QJsonObject exportDocument(const Document &doc, bool embed = false);
void validateProjectStorageSize(qint64 jsonBytes, qint64 externalImageBytes = 0);
QByteArray serializeDocument(const Document &doc, bool embed = false);
void validateDocument(const Document &doc);
void saveBytes(const QString &path, const QByteArray &bytes);
QImage exampleImage();
QImage previewImage(const Document &doc);
class CandidatePicker {
  public:
    void update(const QVector<Candidate> &candidates, QPoint point);
    void step(int direction);
    void reset();
    std::optional<Candidate> current() const;
    int level() const {
        return index_ + 1;
    }
    int count() const {
        return levels_.size();
    }

  private:
    QVector<Candidate> levels_;
    int index_ = 0;
    std::optional<QPoint> anchor_;
};
class History {
  public:
    void push(const QVector<Note> &notes);
    QVector<Note> undo(const QVector<Note> &notes);
    QVector<Note> redo(const QVector<Note> &notes);
    bool canUndo() const {
        return !undo_.isEmpty();
    }
    bool canRedo() const {
        return !redo_.isEmpty();
    }
    void clear();

  private:
    QVector<QVector<Note>> undo_, redo_;
};
} // namespace h2d
