#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Export time is intentionally generated for each operation. Compare all other
// feedback fields, including metadata, without depending on clock resolution.
inline QJsonValue withoutFeedbackExportTime(QJsonValue value) {
    if (value.isObject()) {
        auto object = value.toObject();
        if (object.value("schemaVersion") == "feedback-minimal-2" || object.value("schemaVersion") == "feedback-minimal-3")
            object.remove("exportedAt");
        for (auto it = object.begin(); it != object.end(); ++it)
            it.value() = withoutFeedbackExportTime(it.value());
        return object;
    }
    if (value.isArray()) {
        auto array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i)
            array[i] = withoutFeedbackExportTime(array[i]);
        return array;
    }
    return value;
}
inline QJsonObject stableFeedback(const QJsonObject &object) {
    return withoutFeedbackExportTime(object).toObject();
}
inline QJsonDocument stableFeedback(const QJsonDocument &document) {
    return QJsonDocument(stableFeedback(document.object()));
}
inline QJsonObject stableFeedback(const QByteArray &bytes) {
    return stableFeedback(QJsonDocument::fromJson(bytes).object());
}

// Convert a current typed export to the unversioned object shape used by
// compatibility fixtures. This keeps those tests about old-reader behavior
// instead of accidentally presenting a v3 payload without its header.
inline QJsonObject unversionedLegacyFeedback(QJsonObject value) {
    value.remove("$schema"); value.remove("schemaVersion"); value.remove("tool"); value.remove("exportedAt");
    auto objects = value["objects"].toArray();
    for (qsizetype i = 0; i < objects.size(); ++i) {
        auto object = objects[i].toObject();
        const auto kind = object["kind"].toString();
        if (kind == "point") {
            const auto point = object["point"].toObject();
            object.insert("source", QJsonObject{{"x1", point["x"]}, {"y1", point["y"]},
                                                {"x2", point["x"]}, {"y2", point["y"]}});
        } else if (kind == "global") {
            object.insert("source", QJsonValue(QJsonValue::Null));
        }
        if (object.contains("movement")) {
            const auto movement = object.take("movement").toObject();
            object.insert("movements", QJsonArray{QJsonObject{{"to", movement["rect"]}}});
        } else object.insert("movements", object["movements"].toArray());
        object.remove("kind"); object.remove("point"); object.remove("result");
        objects[i] = object;
    }
    value["objects"] = objects;
    return value;
}
