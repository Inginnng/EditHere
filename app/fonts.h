#pragma once
#include <QString>
#include <QStringList>
namespace h2d {
// A family asked for by name can be missing on a slimmed-down Windows install,
// and the digits then fall back into a symbol font. These resolve the first
// candidate that is actually installed; an empty result means "leave the family
// unset and let the platform default answer", which always renders.
QString resolveFontFamily(const QStringList &candidates);
QString latinFontFamily();
QString cjkFontFamily();
QString monoFontFamily();
} // namespace h2d
