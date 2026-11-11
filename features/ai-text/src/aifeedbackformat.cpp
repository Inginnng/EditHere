#include "aifeedbackformat.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QMap>
#include <functional>
namespace h2d {
namespace {
constexpr qsizetype MaxObjects = 100;
QString tr(const char *message) { return QCoreApplication::translate("h2d", message); }
} // namespace
QJsonValue aiFeedbackData(const QJsonValue &value) {
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item : value.toArray()) result.append(aiFeedbackData(item));
        return result;
    }
    if (!value.isObject()) return value;
    auto object = value.toObject();
    // Screenshot bytes are supplied only through the explicit image list.
    for (const auto &key : {"dataUrl", "pngBase64", "imageData", "previewDataUrl"}) object.remove(key);
    if (object["image"].isString()) object["image"] = QJsonObject{{"bytes", "provided separately"}};
    for (auto it = object.begin(); it != object.end(); ++it) it.value() = aiFeedbackData(it.value());
    return object;
}
bool aiCollectFacts(const QJsonObject &feedback, QJsonArray *facts, QString *error) {
    std::function<bool(const QJsonArray &, const QJsonObject &, const QString &)> collect;
    collect = [&](const QJsonArray &objects, const QJsonObject &frame, const QString &parent) {
        for (const auto &value : objects) {
            if (!value.isObject()) { *error = tr("批注对象格式无效。"); return false; }
            auto object = value.toObject();
            const auto children = object.take("children").toArray();
            if (facts->size() >= MaxObjects) { *error = tr("一次最多整理 100 个批注对象，请分批处理。"); return false; }
            const auto originalId = object["id"].toString();
            const auto id = QString("item-%1").arg(facts->size() + 1);
            object["objectId"] = originalId;
            object["id"] = id;
            if (!parent.isEmpty()) object["parentItemId"] = parent;
            for (auto it = frame.begin(); it != frame.end(); ++it) object[it.key()] = it.value();
            facts->append(object);
            if (!collect(children, frame, id)) return false;
        }
        return true;
    };
    if (feedback["schemaVersion"].toString().startsWith("video-feedback-")) {
        const auto frames = feedback["frames"].toArray();
        if (frames.size() > 8) { *error = tr("一次可解析 8 个视频帧，请分批处理。"); return false; }
        for (const auto &value : frames) {
            const auto frame = value.toObject();
            const QJsonObject context{{"frameId", frame["id"]}, {"timestampMs", frame["timestampMs"]},
                                      {"timestampUs", frame["timestampUs"]}};
            if (!collect(frame["feedback"].toObject()["objects"].toArray(), context, {})) return false;
        }
    } else if (!collect(feedback["objects"].toArray(), {}, {})) return false;
    if (facts->isEmpty()) { *error = tr("请先添加批注或调整组件，再生成修改意见。"); return false; }
    return true;
}
QString aiOriginalNotes(const QJsonObject &fact) {
    QStringList notes;
    for (const auto &value : fact["annotations"].toArray()) {
        const auto text = value.isString() ? value.toString() : value.toObject()["text"].toString();
        if (!text.trimmed().isEmpty()) notes << text;
    }
    return notes.join('\n');
}
namespace {
QString unfenced(QString text) {
    text = text.trimmed();
    if (text.startsWith("```")) {
        const auto newline = text.indexOf('\n');
        const auto end = text.lastIndexOf("```");
        if (newline >= 0 && end > newline) text = text.mid(newline + 1, end - newline - 1).trimmed();
    }
    return text;
}
}
QString aiInputContext(const AiFeedbackInput &input) {
    // A human page name can identify the task; capture metadata is model input only.
    return input.feedback["capture"].toObject()["page"].toString().trimmed();
}
QString aiFormatResult(QString content, const QJsonArray &facts, const QString &context, QString *error) {
    content = unfenced(content);
    auto document = QJsonDocument::fromJson(content.toUtf8());
    if (document.isNull()) {
        const auto first = content.indexOf('{'), last = content.lastIndexOf('}');
        if (first >= 0 && last > first) document = QJsonDocument::fromJson(content.mid(first, last - first + 1).toUtf8());
    }
    const bool structured = document.isObject() && document.object()["items"].isArray();
    if (!structured) {
        if (content.isEmpty() || content.startsWith('{') || content.startsWith('[')) {
            *error = tr("未能读出修改说明，请重新解析。");
            return {};
        }
        return content;
    }
    QMap<QString, QJsonObject> byId;
    for (const auto &value : document.object()["items"].toArray()) {
        const auto item = value.toObject();
        const auto id = item["id"].toString();
        if (!id.isEmpty() && !byId.contains(id) && !item["instruction"].toString().trimmed().isEmpty()) byId[id] = item;
    }
    int missing = 0;
    for (const auto &value : facts)
        if (!byId.contains(value.toObject()["id"].toString())) ++missing;
    if (missing) {
        // A raw note or coordinates cannot replace the missing visual interpretation.
        *error = tr("还有 %1 处图形化修改未转成文字需求，请重新解析。").arg(missing);
        return {};
    }
    QStringList sections;
    if (!context.isEmpty()) sections << context;
    int index = 0;
    for (const auto &value : facts) {
        const auto item = byId.value(value.toObject()["id"].toString());
        const auto target = item["target"].toString().trimmed();
        const auto instruction = item["instruction"].toString().trimmed();
        QString section = QString("%1. %2").arg(++index).arg(target.isEmpty() ? instruction : target + '\n' + instruction);
        const auto uncertainty = item["uncertainty"].toString().trimmed();
        if (!uncertainty.isEmpty()) section += '\n' + tr("待确认：%1").arg(uncertainty);
        sections << section;
    }
    return sections.join("\n\n");
}
} // namespace h2d
