#pragma once
#include <QIcon>
#include <QString>

namespace h2d {
// Embedded, theme-aware brand artwork. Region-qualified provider IDs are also
// accepted; unknown/custom services use the application's settings symbol.
QIcon aiProviderIcon(const QString &family, int size = 24);
} // namespace h2d
