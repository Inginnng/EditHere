#pragma once
#include <QString>
#include <QByteArray>

namespace h2d::updateauth {
// This trust root is compiled into the installed application. It is never
// populated from release metadata, downloads, environment variables or settings.
QString publisherCertificateSha256();
bool publisherConfigured();
bool certificateMode();
bool installationConfigured();
bool canonicalVersion(const QString &version);
// The real installation boundary always uses the compiled key; no caller can
// supply a downloaded key or choose a fallback policy.
bool verifyProjectSignature(const QByteArray &metadata, const QString &version,
                            const QString &assetName, const QString &sha256, QString *error);
bool verifyPublisherSignature(const QString &filePath, const QString &certificateSha256,
                              QString *error);
} // namespace h2d::updateauth
