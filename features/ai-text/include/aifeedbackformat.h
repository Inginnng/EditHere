#pragma once
#include "aifeedback.h"

namespace h2d {
// Pure data/formatting boundary: no network, widgets, files or document mutation.
QJsonValue aiFeedbackData(const QJsonValue &value);
bool aiIsImageSet(const QJsonObject &feedback);
bool aiCollectFacts(const QJsonObject &feedback, QJsonArray *facts, QString *error, qsizetype firstIndex = 0);
QString aiOriginalNotes(const QJsonObject &item);
QString aiFactScope(const QJsonObject &item);
QString aiInputContext(const AiFeedbackInput &input);
QString aiFormatResult(QString content, const QJsonArray &facts, const QString &context, QString *error,
                       qsizetype firstIndex, bool requireStructured);
} // namespace h2d
