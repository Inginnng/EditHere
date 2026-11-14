#include "imageset.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

namespace h2d {
namespace {
constexpr qint64 MaxSetBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxSetPixels = 128000000;
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
QJsonObject root(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxSetBytes) fail("Cannot read image set (maximum 512 MiB).");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) fail("Invalid image set JSON.");
    return json.object();
}
bool annotated(const Document &document) { return !exportFeedback(document)["objects"].toArray().isEmpty(); }
QJsonObject metadata() {
    return {{"$schema", "urn:edithere:image-set-feedback:1"}, {"schemaVersion", ImageSetFeedbackVersion},
            {"tool", QJsonObject{{"name", "EditHere"}, {"version", EDITHERE_VERSION}}},
            {"exportedAt", timestamp()}, {"annotationSpace", "result"}};
}
}
bool isImageSetFile(const QString &path) {
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".edithere", Qt::CaseInsensitive)) return false;
    const auto json = root(path);
    return json["schemaVersion"] == ImageSetVersion || json["schemaVersion"] == ImageSetFeedbackVersion;
}
void validateImageSet(const ImageSet &set) {
    if (set.name.trimmed().isEmpty() || set.name.size() > 200 || set.pages.isEmpty() || set.pages.size() > 64) fail("An image set needs a name and 1–64 pages.");
    QSet<QString> ids;
    qint64 pixels = 0, bytes = 0;
    for (const auto &page : set.pages) {
        if (page.id.isEmpty() || page.id.size() > 128 || ids.contains(page.id) || QRegularExpression("[^a-zA-Z0-9._-]").match(page.id).hasMatch() || page.name.trimmed().isEmpty() || page.name.size() > 200) fail("Invalid or duplicate image set page.");
        ids.insert(page.id); validateDocument(page.document);
        pixels += qint64(page.document.image.width()) * page.document.image.height();
        bytes += page.document.png.size();
        if (pixels > MaxSetPixels || bytes > MaxSetBytes * 3 / 4) fail("Image set exceeds 128 million pixels or 384 MiB of images.");
    }
    if (set.groupNotes.size() > 1000) fail("Too many group notes.");
    for (const auto &value : set.groupNotes) {
        const auto note = value.toObject();
        if (!note["text"].isString() || note["text"].toString().trimmed().isEmpty() || note["text"].toString().size() > 10000) fail("Invalid group note.");
    }
}
ImageSet loadImageSet(const QString &path) {
    const auto json = root(path);
    const bool feedback = json["schemaVersion"] == ImageSetFeedbackVersion;
    if (!feedback && json["schemaVersion"] != ImageSetVersion) fail("Unsupported image set version.");
    if (!json["pages"].isArray() || json["pages"].toArray().size() > 64) fail("Invalid image set pages.");
    ImageSet set;
    set.name = feedback ? json["set"].toObject()["name"].toString() : json["name"].toString();
    if (json.contains("groupNotes") && !json["groupNotes"].isArray()) fail("Invalid group notes.");
    set.groupNotes = json["groupNotes"].toArray();
    qint64 pixels = 0;
    for (const auto &value : json["pages"].toArray()) {
        const auto page = value.toObject();
        Document document;
        if (page.contains("document")) {
            document = loadDocumentObject(page["document"].toObject(), path);
        } else if (feedback) {
            document = loadFeedback(page["feedback"].toObject(), {});
        } else {
            const QString reference = page["path"].toString();
            if (reference.isEmpty() || reference.contains(QChar::Null)) fail("A page image path is required.");
            const auto filePath = QFileInfo(path).dir().absoluteFilePath(reference);
            if (page.contains("sha256")) {
                QFile image(filePath);
                if (!image.open(QIODevice::ReadOnly) || image.size() > MaxImageFileBytes) fail("Cannot read page image.");
                const auto hash = QString::fromLatin1(QCryptographicHash::hash(image.readAll(), QCryptographicHash::Sha256).toHex());
                if (page["sha256"].toString() != hash) fail("Page image changed; update its manifest with image-set upsert.");
            }
            document = loadDocument(filePath);
        }
        pixels += qint64(document.image.width()) * document.image.height();
        if (pixels > MaxSetPixels) fail("Image set exceeds 128 million pixels.");
        document.title = page["name"].toString();
        set.pages.append({page["id"].toString(), page["name"].toString(), std::move(document)});
    }
    validateImageSet(set);
    return set;
}
QByteArray serializeImageSet(const ImageSet &set) {
    validateImageSet(set);
    QJsonArray pages;
    for (const auto &page : set.pages) pages.append(QJsonObject{{"id", page.id}, {"name", page.name}, {"document", QJsonDocument::fromJson(serializeDocument(page.document, true)).object()}});
    const auto result = QJsonDocument(QJsonObject{{"schemaVersion", ImageSetVersion}, {"name", set.name}, {"pages", pages}, {"groupNotes", set.groupNotes}}).toJson(QJsonDocument::Indented);
    if (result.size() > MaxSetBytes) fail("Image set project exceeds 512 MiB.");
    return result;
}
FeedbackBundle buildImageSetFeedbackBundle(const ImageSet &set, const FeedbackExportOptions &options) {
    validateImageSet(set);
    FeedbackBundle bundle;
    QJsonArray index, pages, notes;
    for (int i = 0; i < set.pages.size(); ++i) {
        const auto &page = set.pages[i];
        auto pageOptions = options;
        const QString prefix = QFileInfo(options.imageFile).completeBaseName().left(60);
        pageOptions.imageFile = QString("page-%1-%2-%3.png").arg(i + 1).arg(page.id.left(80), prefix);
        pageOptions.previewFile = pageOptions.imageFile.chopped(4) + ".annotated.jpg";
        const auto pageBundle = buildFeedbackBundle(page.document, pageOptions);
        const auto feedback = QJsonDocument::fromJson(pageBundle.json).object();
        const bool included = !feedback["objects"].toArray().isEmpty();
        QJsonObject item{{"id", page.id}, {"name", page.name}, {"index", i + 1}, {"width", page.document.image.width()}, {"height", page.document.image.height()}};
        if (included) {
            item["imageFile"] = pageOptions.imageFile;
            auto output = item; output["feedback"] = feedback; pages.append(output);
            for (auto file = pageBundle.files.cbegin(); file != pageBundle.files.cend(); ++file) bundle.files.insert(file.key(), file.value());
        }
        item["annotated"] = included; index.append(item);
    }
    for (const auto &note : set.groupNotes) notes.append(QJsonObject{{"number", notes.size() + 1}, {"text", note.toObject()["text"]}});
    auto output = metadata();
    output["set"] = QJsonObject{{"name", set.name}, {"pageCount", set.pages.size()}, {"annotatedPageCount", pages.size()}};
    output["pageIndex"] = index; output["pages"] = pages; output["groupNotes"] = notes;
    bundle.json = QJsonDocument(output).toJson(QJsonDocument::Indented);
    return bundle;
}
QByteArray serializeImageSetFeedback(const ImageSet &set, bool embed, bool compress, bool includeRecognizedText) {
    // The legacy compress flag is ignored: embedded originals stay lossless, the
    // same rule app/model.h documents for single-picture feedback.
    Q_UNUSED(compress);
    FeedbackExportOptions options; options.mode = embed ? ImageExportMode::Embedded : ImageExportMode::None;
    options.includeRecognizedText = includeRecognizedText;
    return buildImageSetFeedbackBundle(set, options).json;
}
int imageSetAnnotationCount(const ImageSet &set) {
    int count = set.groupNotes.size();
    for (const auto &page : set.pages) {
        count += page.document.notes.size();
        if (page.document.layout) for (const auto &marker : movementMarkers(*page.document.layout, page.document.notes)) if (marker.noteIndex < 0) ++count;
    }
    return count;
}
} // namespace h2d
