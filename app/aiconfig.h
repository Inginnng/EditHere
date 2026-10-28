#pragma once
#include <QString>
#include <QVector>

namespace h2d {
struct AiProviderPreset {
    QString id, name, baseUrl, model, protocol, docsUrl, note;
};
struct AiConnectionSettings {
    QString providerId = "openai";
    QString baseUrl;
    QString model;
    QString protocol = "responses";
    QString extraInstructions;
    int timeoutSeconds = 120;
    bool operator==(const AiConnectionSettings &) const = default;
};
QVector<AiProviderPreset> aiProviderPresets();
AiConnectionSettings defaultAiConnection(const QString &providerId = "openai");
QString validateAiConnection(const AiConnectionSettings &, bool requireModel = true);
QString aiSystemPrompt();
} // namespace h2d
