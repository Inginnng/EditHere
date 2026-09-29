#include "fonts.h"
#include <QFontDatabase>
namespace h2d {
QString resolveFontFamily(const QStringList &candidates) {
    const QStringList installed = QFontDatabase::families();
    for (const QString &candidate : candidates)
        for (const QString &family : installed)
            if (family.compare(candidate, Qt::CaseInsensitive) == 0)
                return family;
    return QString();
}
QString latinFontFamily() {
    static const QString family =
        resolveFontFamily({"Segoe UI", "Tahoma", "Arial", "Helvetica", "Noto Sans"});
    return family;
}
QString cjkFontFamily() {
    static const QString family = resolveFontFamily({"Microsoft YaHei UI", "Microsoft YaHei",
                                                     "PingFang SC", "Noto Sans CJK SC", "SimHei",
                                                     "SimSun"});
    return family;
}
QString monoFontFamily() {
    static const QString family =
        resolveFontFamily({"Consolas", "Cascadia Mono", "Menlo", "Courier New"});
    return family;
}
} // namespace h2d
