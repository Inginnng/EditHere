#include "videoproject.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace h2d {
namespace {
constexpr qint64 MaxJsonInteger = 9007199254740991LL;
QString tr(const char *text) { return QCoreApplication::translate("h2d", text); }
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }

void exactKeys(const QJsonObject &object, const QStringList &keys) {
    if (object.size() != keys.size())
        fail(tr("视频项目字段缺失或包含未知字段"));
    for (const auto &key : keys)
        if (!object.contains(key))
            fail(tr("视频项目缺少字段：%1").arg(key));
}
qint64 integer(const QJsonValue &value, const QString &name, qint64 maximum = MaxJsonInteger) {
    const double number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || number < 0 ||
        number > double(maximum) || std::floor(number) != number)
        fail(tr("视频项目的 %1 必须是有效的非负整数").arg(name));
    return qint64(number);
}
void storageSize(qint64 bytes) {
    if (bytes < 0 || bytes > MaxVideoProjectFileBytes)
        fail(tr("视频项目不能超过 512 MiB，请减少标注帧或缩小截图后重试"));
}
void frameId(const QString &id) {
    if (id.isEmpty() || id.size() > 256 ||
        QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]").match(id).hasMatch())
        fail(tr("视频帧标识必须能用于安全的图片文件名"));
}
QString resolvedSource(const QString &source, const QString &projectPath) {
    const QUrl url(source);
    if (url.isLocalFile())
        return QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath());
    if (!QDir::isAbsolutePath(source) && url.scheme().isEmpty())
        return QDir::cleanPath(QFileInfo(projectPath).dir().absoluteFilePath(source));
    return source;
}
bool annotated(const Document &document) {
    if (!document.notes.isEmpty())
        return true;
    if (document.layout) {
        for (const auto &movement : layoutMovements(*document.layout))
            if (movement.source != movement.destination)
                return true;
    }
    return false;
}
QVector<const VideoFrame *> orderedFrames(const VideoProject &project) {
    if (project.source.trimmed().isEmpty() || project.source.size() > 32768 || project.source.contains(QChar::Null))
        fail(tr("视频地址不能为空或超出限制"));
    if (project.durationMs < 0 || project.durationMs > MaxJsonInteger / 1000 ||
        project.positionMs < 0 || project.positionMs > MaxJsonInteger / 1000 ||
        (project.durationMs && project.positionMs > project.durationMs))
        fail(tr("视频时长或播放位置不正确"));
    if (project.frames.size() > MaxVideoFrames)
        fail(tr("视频项目最多支持 1000 个标注帧"));

    QVector<const VideoFrame *> frames;
    QSet<QString> ids;
    QSet<qint64> times;
    qint64 pixels = 0, encodedBytes = 0, annotations = 0;
    QSize dimensions;
    for (const auto &frame : project.frames) {
        if (frame.timestampUs < 0 || frame.timestampUs > MaxJsonInteger ||
            (project.durationMs && frame.timestampUs / 1000 > project.durationMs))
            fail(tr("标注帧时间超出视频范围"));
        const auto &document = frame.document;
        validateDocument(document);
        frameId(document.id);
        if (document.png.isEmpty() || document.png.size() > MaxImageFileBytes)
            fail(tr("标注帧原图不能为空或超过 48 MiB"));
        if (ids.contains(document.id) || times.contains(frame.timestampUs))
            fail(tr("视频项目包含重复的帧标识或时间戳"));
        ids.insert(document.id);
        times.insert(frame.timestampUs);
        if (!dimensions.isValid())
            dimensions = document.image.size();
        else if (dimensions != document.image.size())
            fail(tr("视频标注帧尺寸必须一致"));
        if (!annotated(document))
            continue;
        pixels += qint64(document.image.width()) * document.image.height();
        encodedBytes += ((qint64(document.png.size()) + 2) / 3) * 4;
        annotations += document.notes.size();
        if (document.layout)
            annotations += layoutMovements(*document.layout).size();
        if (pixels > MaxVideoProjectPixels)
            fail(tr("标注帧总像素超过 1.28 亿，请减少标注帧或缩小截图"));
        if (annotations > MaxVideoAnnotations)
            fail(tr("视频项目的批注和移动总数不能超过 10000"));
        storageSize(encodedBytes);
        frames.append(&frame);
    }
    std::sort(frames.begin(), frames.end(), [](const VideoFrame *first, const VideoFrame *second) {
        return first->timestampUs < second->timestampUs;
    });
    return frames;
}
QJsonObject readRoot(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(tr("无法打开视频项目"));
    storageSize(file.size());
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        fail(tr("视频项目 JSON 格式不正确"));
    return parsed.object();
}
VideoProject loadVideoFeedbackRoot(const QJsonObject &root, const QString &path) {
    exactKeys(root, {"schemaVersion", "tool", "exportedAt", "video", "frames", "objects"});
    if (root["tool"] != "EditHere" ||
        !QDateTime::fromString(root["exportedAt"].toString(), Qt::ISODateWithMs).isValid())
        fail(tr("视频反馈工具或导出日期不正确"));
    if (!root["video"].isObject() || !root["frames"].isArray() || !root["objects"].isArray())
        fail(tr("视频信息、标注帧或对象列表格式不正确"));
    const auto video = root["video"].toObject();
    exactKeys(video, {"source", "durationMs", "width", "height"});
    if (!video["source"].isString())
        fail(tr("视频地址必须是字符串"));
    VideoProject project;
    project.source = video["source"].toString();
    project.durationMs = integer(video["durationMs"], "durationMs", MaxJsonInteger / 1000);
    orderedFrames(project);
    const qint64 width = integer(video["width"], "width", 32767);
    const qint64 height = integer(video["height"], "height", 32767);
    const auto frames = root["frames"].toArray();
    if (frames.size() > MaxVideoFrames || root["objects"].toArray().size() > MaxVideoAnnotations)
        fail(tr("视频反馈的帧或批注数量超出限制"));
    if ((!frames.isEmpty() && (!width || !height)) || width * height > MaxPixels ||
        qint64(frames.size()) * width * height > MaxVideoProjectPixels)
        fail(tr("视频反馈帧尺寸或总像素超出限制"));
    QSet<QString> ids;
    QSet<qint64> times;
    QVector<QJsonObject> ordered;
    qint64 annotations = 0;
    for (const auto &value : frames) {
        if (!value.isObject())
            fail(tr("视频标注帧必须是对象"));
        const auto frame = value.toObject();
        exactKeys(frame, {"id", "timestampMs", "timestampUs", "imageFile", "feedback"});
        const auto id = frame["id"].toString();
        if (!frame["id"].isString())
            fail(tr("视频帧标识必须是字符串"));
        frameId(id);
        const qint64 time = integer(frame["timestampUs"], "timestampUs");
        if (integer(frame["timestampMs"], "timestampMs") != time / 1000 ||
            (project.durationMs && time / 1000 > project.durationMs) || ids.contains(id) || times.contains(time))
            fail(tr("视频反馈包含重复、无效或不一致的帧时间或标识"));
        const QString imageFile = "frame-" + id + ".png";
        if (frame["imageFile"] != imageFile || !frame["feedback"].isObject())
            fail(tr("视频帧图片文件名或反馈格式不正确"));
        const auto feedback = frame["feedback"].toObject();
        if (!feedback["objects"].isArray() || feedback["objects"].toArray().isEmpty())
            fail(tr("视频反馈只能包含有批注或移动的对象帧"));
        QImage original;
        if (!feedback.contains("image"))
            original = loadDocument(QFileInfo(path).dir().filePath(imageFile)).image;
        auto document = loadFeedback(feedback, original);
        if (document.image.size() != QSize(int(width), int(height)))
            fail(tr("视频反馈图片尺寸与视频信息不一致"));
        document.id = id;
        document.imageFile = imageFile;
        document.title = QFileInfo(project.source).fileName() + " @ " + videoTimeLabel(time / 1000);
        annotations += document.notes.size();
        if (document.layout)
            annotations += layoutMovements(*document.layout).size();
        if (annotations > MaxVideoAnnotations)
            fail(tr("视频项目的批注和移动总数不能超过 10000"));
        ids.insert(id);
        times.insert(time);
        project.frames.append({time, std::move(document)});
        ordered.append(frame);
    }
    std::sort(ordered.begin(), ordered.end(), [](const QJsonObject &first, const QJsonObject &second) {
        return first["timestampUs"].toDouble() < second["timestampUs"].toDouble();
    });
    QJsonArray expectedIndex;
    for (const auto &frame : ordered)
        for (const auto &value : frame["feedback"].toObject()["objects"].toArray()) {
            auto object = value.toObject();
            object.insert("timestampMs", frame["timestampMs"]);
            object.insert("frameId", frame["id"]);
            expectedIndex.append(object);
        }
    // The index is for AI consumers, not a second source of editable notes.
    // Verify its image/time references before ignoring it during restoration.
    if (root["objects"].toArray() != expectedIndex)
        fail(tr("视频对象索引与标注帧的时间、图像或批注不一致"));
    const auto sorted = orderedFrames(project);
    QVector<VideoFrame> result;
    for (const auto *frame : sorted)
        result.append(*frame);
    project.frames = std::move(result);
    project.source = resolvedSource(project.source, path);
    return project;
}
} // namespace

bool isVideoFile(const QString &path) {
    const auto extension = QFileInfo(path).suffix().toLower();
    return QStringList{"mp4", "mov", "m4v", "webm", "mkv", "avi", "wmv", "mpg", "mpeg",
                       "ogv", "ts", "mts", "m2ts"}.contains(extension);
}
bool isVideoProjectFile(const QString &path) {
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".edithere", Qt::CaseInsensitive))
        return false;
    try {
        // Materialize the value: operator[] on a temporary non-const object
        // returns a QJsonValueRef whose owner would already have been destroyed.
        const QJsonValue version = readRoot(path)["schemaVersion"];
        return version == "video-project-1" || version == "video-feedback-1";
    } catch (const std::exception &) {
        return false;
    }
}

VideoProject loadVideoProject(const QString &path) {
    const auto root = readRoot(path);
    if (root["schemaVersion"] == "video-feedback-1")
        return loadVideoFeedbackRoot(root, path);
    exactKeys(root, {"schemaVersion", "tool", "exportedAt", "video", "frames"});
    if (root["schemaVersion"] != "video-project-1" || root["tool"] != "EditHere")
        fail(tr("不支持这个视频项目版本"));
    if (!QDateTime::fromString(root["exportedAt"].toString(), Qt::ISODateWithMs).isValid())
        fail(tr("视频项目导出日期不正确"));
    if (!root["video"].isObject() || !root["frames"].isArray())
        fail(tr("视频信息或标注帧列表格式不正确"));
    const auto video = root["video"].toObject();
    exactKeys(video, {"source", "durationMs", "positionMs"});
    if (!video["source"].isString())
        fail(tr("视频地址必须是字符串"));
    VideoProject project;
    project.source = video["source"].toString();
    project.durationMs = integer(video["durationMs"], "durationMs", MaxJsonInteger / 1000);
    project.positionMs = integer(video["positionMs"], "positionMs", MaxJsonInteger / 1000);
    // Validate the video before allocating or decoding any screenshots. A missing
    // source file is intentionally allowed: the embedded frames remain editable.
    orderedFrames(project);
    const auto frames = root["frames"].toArray();
    if (frames.size() > MaxVideoFrames)
        fail(tr("视频项目最多支持 1000 个标注帧"));
    QTemporaryDir directory;
    if (!directory.isValid())
        fail(tr("无法创建视频帧读取目录"));
    const auto framePath = directory.filePath("frame.edithere");
    qint64 pixels = 0, annotations = 0;
    QSet<QString> ids;
    QSet<qint64> times;
    for (const auto &value : frames) {
        if (!value.isObject())
            fail(tr("视频标注帧必须是对象"));
        const auto frame = value.toObject();
        exactKeys(frame, {"id", "timestampMs", "timestampUs", "document"});
        const qint64 time = integer(frame["timestampUs"], "timestampUs");
        if (integer(frame["timestampMs"], "timestampMs") != time / 1000 ||
            (project.durationMs && time / 1000 > project.durationMs))
            fail(tr("标注帧时间不一致或超出视频范围"));
        if (!frame["id"].isString() || frame["id"].toString().isEmpty() ||
            ids.contains(frame["id"].toString()) || times.contains(time))
            fail(tr("视频项目包含无效或重复的帧标识或时间戳"));
        frameId(frame["id"].toString());
        if (!frame["document"].isObject())
            fail(tr("标注帧缺少可编辑图片项目"));
        const auto document = frame["document"].toObject();
        const auto capture = document["capture"].toObject();
        if (document["schemaVersion"] != "3.0.0" || capture["id"] != frame["id"])
            fail(tr("标注帧项目版本或标识不正确"));
        const qint64 width = integer(capture["width"], "width", 32767);
        const qint64 height = integer(capture["height"], "height", 32767);
        if (!width || !height || width * height > MaxPixels)
            fail(tr("标注帧尺寸超出限制"));
        pixels += width * height;
        if (pixels > MaxVideoProjectPixels)
            fail(tr("标注帧总像素超过 1.28 亿，请减少标注帧或缩小截图"));
        if (!capture["pngBase64"].isString() || capture["pngBase64"].toString().isEmpty() ||
            capture["pngBase64"].toString().size() > ((MaxImageFileBytes + 2) / 3) * 4)
            fail(tr("视频项目的标注帧必须内嵌不超过 48 MiB 的原图"));
        // Use the same strict lossless project reader as single-image editing.
        // Its 96 MiB limit applies to one frame, never to the complete video.
        saveBytes(framePath, QJsonDocument(document).toJson(QJsonDocument::Compact));
        auto restored = loadDocument(framePath);
        annotations += restored.notes.size();
        if (restored.layout)
            annotations += layoutMovements(*restored.layout).size();
        if (annotations > MaxVideoAnnotations)
            fail(tr("视频项目的批注和移动总数不能超过 10000"));
        if (!annotated(restored))
            fail(tr("视频项目只能包含有批注或移动的帧"));
        ids.insert(restored.id);
        times.insert(time);
        project.frames.append({time, std::move(restored)});
    }
    const auto ordered = orderedFrames(project);
    QVector<VideoFrame> sorted;
    sorted.reserve(ordered.size());
    for (const auto *frame : ordered)
        sorted.append(*frame);
    project.frames = std::move(sorted);
    project.source = resolvedSource(project.source, path);
    return project;
}

QByteArray serializeVideoProject(const VideoProject &project) {
    const auto ordered = orderedFrames(project);
    QJsonArray frames;
    for (const auto *frame : ordered) {
        // serializeDocument is the lossless current format. exportDocument is a
        // legacy exporter and cannot retain movement-attached or global notes.
        const auto document = QJsonDocument::fromJson(serializeDocument(frame->document, true)).object();
        frames.append(QJsonObject{{"id", frame->document.id}, {"timestampMs", frame->timestampUs / 1000},
                                  {"timestampUs", frame->timestampUs}, {"document", document}});
    }
    const QJsonObject root{{"schemaVersion", "video-project-1"}, {"tool", "EditHere"}, {"exportedAt", timestamp()},
                           {"video", QJsonObject{{"source", project.source}, {"durationMs", project.durationMs},
                                                  {"positionMs", project.positionMs}}},
                           {"frames", frames}};
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
    storageSize(bytes.size());
    return bytes;
}
QByteArray serializeVideoFeedback(const VideoProject &project, bool embed, bool compress) {
    const auto ordered = orderedFrames(project);
    QJsonArray frames, objects;
    for (const auto *frame : ordered) {
        const auto feedback = exportFeedback(frame->document, embed, compress);
        frames.append(QJsonObject{{"id", frame->document.id}, {"timestampMs", frame->timestampUs / 1000},
                                  {"timestampUs", frame->timestampUs},
                                  {"imageFile", "frame-" + frame->document.id + ".png"}, {"feedback", feedback}});
        // This index repeats only lightweight annotation metadata. The image is
        // stored once in its frame's independently readable existing feedback.
        for (const auto &value : feedback["objects"].toArray()) {
            auto object = value.toObject();
            object.insert("timestampMs", frame->timestampUs / 1000);
            object.insert("frameId", frame->document.id);
            objects.append(object);
        }
    }
    const QSize dimensions = ordered.isEmpty() ? QSize(0, 0) : ordered.first()->document.image.size();
    const QJsonObject root{{"schemaVersion", "video-feedback-1"}, {"tool", "EditHere"}, {"exportedAt", timestamp()},
                           {"video", QJsonObject{{"source", project.source}, {"durationMs", project.durationMs},
                                                  {"width", dimensions.width()}, {"height", dimensions.height()}}},
                           {"frames", frames}, {"objects", objects}};
    auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
    storageSize(bytes.size());
    return bytes;
}
int videoAnnotationCount(const VideoProject &project) {
    int count = 0;
    for (const auto *frame : orderedFrames(project))
        count += exportFeedback(frame->document)["objects"].toArray().size();
    return count;
}
QString videoTimeLabel(qint64 ms) {
    ms = std::max<qint64>(0, ms);
    const qint64 hours = ms / 3600000;
    const auto seconds = QString("%1:%2.%3").arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
                             .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
                             .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    return hours ? QString("%1:").arg(hours, 2, 10, QLatin1Char('0')) + seconds : seconds;
}
} // namespace h2d
