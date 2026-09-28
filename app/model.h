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
    // Forgets the level the wheel picked, so the next look starts from the smallest
    // block again. Only a fresh start should do this: moving the pointer over the
    // screen is not a fresh start, and a level that was asked for has to survive it.
    void forget();
    // Whether a block that still contains the pointer is kept when a smaller one turns
    // up underneath it. The capture window wants that: having been asked for a row, it
    // must not hand back a cell the moment the pointer travels into one. The editor
    // wants the opposite, because pointing at a component is how a component is chosen.
    void setKeepInside(bool keep) {
        keepInside_ = keep;
    }
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
    // The block to stay with while the pointer is still inside it, kept across looks
    // so that arriving in a smaller block does not quietly shrink the choice.
    std::optional<Candidate> held_;
    bool keepInside_ = false;
    // How far out in the pile of blocks the wheel left the choice. Kept across looks
    // so that a level asked for once is the level offered everywhere until it is
    // asked for again; without it every step of the pointer handed back the smallest
    // block under it, which is how a whole row turned back into a single cell.
    std::optional<int> chosen_;
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
