#include "model.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
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
        if (n.isPoint) {
            if (!containsPixel(QRect(QPoint(0, 0), d.image.size()), n.point))
                fail("点标注超出原图");
        } else if (n.rect.isEmpty() || n.rect.x() < 0 || n.rect.y() < 0 ||
                   n.rect.x() + n.rect.width() > d.image.width() ||
                   n.rect.y() + n.rect.height() > d.image.height())
            fail("框选超出原图");
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
QJsonObject exportDocument(const Document &d, bool embed) {
    validateDocument(d);
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
    for (const auto &n : d.notes) {
        ax |= n.target["source"] == "accessibility";
        notes.append(QJsonObject{
            {"id", n.id},
            {"number", ++number},
            {"kind", n.isPoint ? "point" : "rectangle"},
            {"point", n.isPoint ? QJsonValue(QJsonObject{{"x", n.point.x()}, {"y", n.point.y()}})
                                : QJsonValue(QJsonValue::Null)},
            {"rectangle", n.isPoint ? QJsonValue(QJsonValue::Null) : QJsonValue(rectJson(n.rect))},
            {"comment", n.comment},
            {"target", n.target},
            {"createdAt", n.createdAt},
            {"updatedAt", n.updatedAt}});
    }
    QJsonObject result{{"schemaVersion", d.layout ? "2.0.0"
                                         : ax     ? "1.1.0"
                                                  : "1.0.0"},
                       {"tool", "Help2Design Capture"},
                       {"exportedAt", timestamp()},
                       {"capture", capture},
                       {"annotations", notes}};
    if (d.layout)
        result.insert("layout", exportLayout(*d.layout));
    return result;
}
void validateProjectStorageSize(qint64 jsonBytes, qint64 externalImageBytes) {
    if (jsonBytes < 0 || jsonBytes > MaxProjectFileBytes)
        fail("项目不能超过 96 MiB，未保存当前修改。请缩小图片或减少批注和分块后重试");
    if (externalImageBytes < 0 || externalImageBytes > MaxImageFileBytes)
        fail("配套原图不能超过 48 MiB。请尝试勾选“包含原图数据”，或缩小图片后重试");
}
QByteArray serializeDocument(const Document &doc, bool embed) {
    // Reject an oversized embedded image before allocating its Base64 representation.
    if (embed)
        validateProjectStorageSize(((qint64(doc.png.size()) + 2) / 3) * 4);
    else
        validateProjectStorageSize(0, doc.png.size());
    auto bytes = QJsonDocument(exportDocument(doc, embed)).toJson(QJsonDocument::Indented);
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
Document loadDocument(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("无法打开文件");
    if (!path.endsWith(".json", Qt::CaseInsensitive)) {
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
    const QString version = root["schemaVersion"].toString();
    if (!QStringList{"1.0.0", "1.1.0", "2.0.0"}.contains(version) || root["tool"] != "Help2Design Capture")
        fail("不支持这个项目版本");
    QStringList fields{"schemaVersion", "tool", "exportedAt", "capture", "annotations"};
    if (version == "2.0.0")
        fields.append("layout");
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
        exactKeys(
            o, {"id", "number", "kind", "point", "rectangle", "comment", "target", "createdAt", "updatedAt"});
        if (integer(o["number"]) != ++number)
            fail("批注编号必须连续");
        if (o["kind"] != "point" && o["kind"] != "rectangle")
            fail("批注类型不正确");
        Note n;
        n.id = o["id"].toString();
        n.isPoint = o["kind"] == "point";
        n.comment = o["comment"].toString();
        n.createdAt = o["createdAt"].toString();
        n.updatedAt = o["updatedAt"].toString();
        n.target = o["target"].toObject();
        if (root["schemaVersion"] == "1.0.0" && n.target["source"] == "accessibility")
            fail("辅助功能元素需要 1.1.0 格式");
        exactKeys(n.target, {"source", "label", "controlType", "automationId", "method",
                             "originalScreenBounds", "clipped"});
        if (n.isPoint) {
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
    if (version == "2.0.0")
        d.layout = importLayout(root["layout"].toObject(), d.image.size());
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
    p.drawImage(24, 24, doc.image);
    auto badge = [&](QPointF c, int n) {
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(QColor("#007aff"));
        p.drawEllipse(c, 14, 14);
        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.drawText(QRectF(c.x() - 14, c.y() - 14, 28, 28), Qt::AlignCenter, QString::number(n));
    };
    int y = 72, i = 0, x = doc.image.width() + 60;
    p.setFont(QFont("Microsoft YaHei", 13, QFont::DemiBold));
    p.setPen(QColor("#242426"));
    p.drawText(x, 44, QString("批注 %1").arg(doc.notes.size()));
    for (const auto &n : doc.notes) {
        if (!n.isPoint) {
            p.setPen(QPen(QColor("#007aff"), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(n.rect).translated(24, 24));
        }
        badge(QPointF(n.isPoint ? n.point : n.rect.topLeft()) + QPointF(24, 24), i + 1);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(x, y, 324, heights[i]), 12, 12);
        badge(QPointF(x + 28, y + 26), i + 1);
        p.setPen(QColor("#85858b"));
        p.setFont(QFont("Segoe UI", 9));
        QString coords = n.isPoint ? QString("(%1, %2)").arg(n.point.x()).arg(n.point.y())
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
