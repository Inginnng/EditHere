#include "aistyle.h"
#include "aiconfig.h"
#include "ui.h"
#include <QMap>
namespace h2d {
QString aiTextStyleSheet(const QString &name) {
    const bool dark = isDarkTheme();
    auto css = aiTextResource(name);
    const QMap<QString, QString> colors{
        {"@surface", dark ? "#23252b" : "#ffffff"},
        {"@canvas", dark ? "#1b1d22" : "#f4f5f7"},
        {"@hover", dark ? "#30343d" : "#eef0f3"},
        {"@border", dark ? "#353943" : "#e2e5ea"},
        {"@muted", dark ? "#a5adbc" : "#747f90"},
        {"@text", dark ? "#edf0f5" : "#283141"},
        {"@accent", accent().name()},
        {"@tint", dark ? "#27364d" : "#edf3fc"},
        {"@error", dark ? "#ff929c" : "#bd3545"}
    };
    for (auto it = colors.cbegin(); it != colors.cend(); ++it) css.replace(it.key(), it.value());
    return css;
}
}
