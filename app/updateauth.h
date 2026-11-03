#pragma once
#include <QString>

namespace h2d::updateauth {
// This trust root is compiled into the installed application. It is never
// populated from release metadata, downloads, environment variables or settings.
QString publisherCertificateSha256();
bool publisherConfigured();
bool verifyPublisherSignature(const QString &filePath, const QString &certificateSha256,
                              QString *error);
} // namespace h2d::updateauth
