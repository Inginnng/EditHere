#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Export time is intentionally generated for each operation. Compare all other
// feedback fields, including metadata, without depending on clock resolution.
inline QJsonValue withoutFeedbackExportTime(QJsonValue value) {
    if (value.isObject()) {
        auto object = value.toObject();
        if (object["schemaVersion"] == "feedback-minimal-2")
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
