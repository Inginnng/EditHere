#include "capturesession.h"
#include "ui.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace h2d {
namespace {
// Free functions have no tr(); the enclosing "h2d" context groups them so the
// translation file stays easy to review.
inline QString tr(const char *text) {
    return QCoreApplication::translate("h2d", text);
}
double ratioScale(int width, int height, const QSize &unit) {
    if (unit.isEmpty())
        return 1.0;
    return std::max(double(width) / unit.width(), double(height) / unit.height());
}
QString selectionIndexPath(const QString &directory) {
    return QDir(directory).filePath(QStringLiteral("selections.json"));
}
} // namespace

QSize captureRatioSize(CaptureRatio ratio, int customWidth, int customHeight) {
    switch (ratio) {
    case CaptureRatio::Free:
        return {};
    case CaptureRatio::Square:
        return {1, 1};
    case CaptureRatio::FourThree:
        return {4, 3};
    case CaptureRatio::ThreeTwo:
        return {3, 2};
    case CaptureRatio::SixteenNine:
        return {16, 9};
    case CaptureRatio::NineSixteen:
        return {9, 16};
    case CaptureRatio::Custom:
        return {std::max(1, customWidth), std::max(1, customHeight)};
    }
    return {};
}

QString captureRatioLabel(CaptureRatio ratio, int customWidth, int customHeight) {
    switch (ratio) {
    case CaptureRatio::Free:
        return tr("自由");
    case CaptureRatio::Square:
        return QStringLiteral("1:1");
    case CaptureRatio::FourThree:
        return QStringLiteral("4:3");
    case CaptureRatio::ThreeTwo:
        return QStringLiteral("3:2");
    case CaptureRatio::SixteenNine:
        return QStringLiteral("16:9");
    case CaptureRatio::NineSixteen:
        return QStringLiteral("9:16");
    case CaptureRatio::Custom:
        return QStringLiteral("%1:%2")
            .arg(std::max(1, customWidth))
            .arg(std::max(1, customHeight));
    }
    return {};
}

QRect captureRectFromDrag(QPoint anchor, QPoint moving, CaptureRatio ratio, int customWidth,
                          int customHeight) {
    // The rectangle runs from the smaller coordinate to the larger one and does not
    // include the larger one, which is the same rule dragRect() in the model uses.
    // The two have to agree: one is what the capture window draws while the pointer
    // is down and the other is what the pointer ends up committing.
    const QRect free(std::min(anchor.x(), moving.x()), std::min(anchor.y(), moving.y()),
                     std::abs(anchor.x() - moving.x()), std::abs(anchor.y() - moving.y()));
    const QSize unit = captureRatioSize(ratio, customWidth, customHeight);
    if (unit.isEmpty())
        return free;
    // The longer of the two directions decides the size, so the selection keeps up
    // with the pointer instead of jumping ahead of it in the other direction.
    const double scale = ratioScale(free.width(), free.height(), unit);
    const int width = std::max(1, int(std::lround(unit.width() * scale)));
    const int height = std::max(1, int(std::lround(unit.height() * scale)));
    // The anchor corner stays put, whichever way the pointer went, and the shape grows
    // towards the pointer.
    const int x = moving.x() < anchor.x() ? anchor.x() - width : anchor.x();
    const int y = moving.y() < anchor.y() ? anchor.y() - height : anchor.y();
    return QRect(x, y, width, height);
}

QRect captureRectWithRatio(QRect current, CaptureRatio ratio, int customWidth, int customHeight) {
    const QSize unit = captureRatioSize(ratio, customWidth, customHeight);
    if (current.isEmpty() || unit.isEmpty())
        return current;
    const int height = std::max(1, int(std::lround(double(current.width()) * unit.height() / unit.width())));
    return QRect(current.topLeft(), QSize(current.width(), height));
}

QRect captureRectWithSize(QRect current, QSize requested, CaptureRatio ratio, int customWidth,
                          int customHeight) {
    if (current.isEmpty())
        return current;
    const int width = std::max(1, requested.width());
    const int height = std::max(1, requested.height());
    const QSize unit = captureRatioSize(ratio, customWidth, customHeight);
    if (unit.isEmpty())
        return QRect(current.topLeft(), QSize(width, height));
    // The width is what the user typed; the height follows the locked ratio, so the
    // two numbers can never contradict each other.
    return captureRectWithRatio(QRect(current.topLeft(), QSize(width, 1)), ratio, customWidth, customHeight);
}

QString captureColourText(const QColor &colour, ColourFormat format) {
    if (!colour.isValid())
        return {};
    switch (format) {
    case ColourFormat::Rgb:
        return QStringLiteral("RGB %1, %2, %3")
            .arg(colour.red())
            .arg(colour.green())
            .arg(colour.blue());
    case ColourFormat::Hex:
        return QStringLiteral("#%1")
            .arg(QString::number(colour.rgb() & 0xffffff, 16).rightJustified(6, QLatin1Char('0')))
            .toLower();
    case ColourFormat::Hsv: {
        int hue = 0, saturation = 0, value = 0;
        colour.getHsv(&hue, &saturation, &value);
        // A hue of -1 means the colour has no hue at all, which grey and black need
        // to be written down as rather than as a wrap-around.
        return QStringLiteral("HSV %1, %2, %3")
            .arg(std::max(hue, 0))
            .arg(qRound(saturation * 100.0 / 255.0))
            .arg(qRound(value * 100.0 / 255.0));
    }
    case ColourFormat::Hsl: {
        int hue = 0, saturation = 0, lightness = 0;
        colour.getHsl(&hue, &saturation, &lightness);
        return QStringLiteral("HSL %1, %2, %3")
            .arg(std::max(hue, 0))
            .arg(qRound(saturation * 100.0 / 255.0))
            .arg(qRound(lightness * 100.0 / 255.0));
    }
    }
    return {};
}

QString captureColourFormatLabel(ColourFormat format) {
    switch (format) {
    case ColourFormat::Rgb:
        return QStringLiteral("RGB");
    case ColourFormat::Hex:
        return QStringLiteral("HEX");
    case ColourFormat::Hsv:
        return QStringLiteral("HSV");
    case ColourFormat::Hsl:
        return QStringLiteral("HSL");
    }
    return {};
}

ColourFormat nextColourFormat(ColourFormat format) {
    switch (format) {
    case ColourFormat::Rgb:
        return ColourFormat::Hex;
    case ColourFormat::Hex:
        return ColourFormat::Hsv;
    case ColourFormat::Hsv:
        return ColourFormat::Hsl;
    case ColourFormat::Hsl:
        break;
    }
    return ColourFormat::Rgb;
}

int captureShadowRadius(bool shadow, int strength) {
    if (!shadow || strength <= 0)
        return 0;
    // A shadow that only reaches a couple of pixels reads as a border, and one wider
    // than the screen is wasted work, so the reach is kept within a useful band.
    return std::clamp(2 + int(std::lround(strength * 0.28)), 2, 40);
}

QColor captureShadowTint(const QColor &colour, bool shadow, int strength) {
    // No colour asked for means the accent: the halo is part of the program's look, and
    // a black one is what every other screenshot tool already does.
    QColor tint = colour.isValid() ? colour : accent();
    if (!shadow || strength <= 0) {
        tint.setAlpha(0);
        return tint;
    }
    // Even a full strength shadow stays short of opaque, otherwise it stops looking
    // like a shadow and starts looking like a second picture behind the first.
    tint.setAlpha(std::clamp(int(std::lround(strength * 2.2)), 0, 235));
    return tint;
}

int CaptureStyle::shadowRadius() const {
    return captureShadowRadius(shadow, shadowStrength);
}

QColor CaptureStyle::shadowTint() const {
    return captureShadowTint(shadowColor, shadow, shadowStrength);
}

namespace {
// The distance from a point to the edge of a rounded rectangle: negative inside,
// zero on the edge, positive outside. The shadow is painted from this instead of
// from a stack of rings, because a stack of rings can only ever fade in steps and
// the innermost step hides behind the picture.
double roundedRectDistance(double px, double py, double halfWidth, double halfHeight,
                           double radius) {
    const double dx = std::abs(px) - (halfWidth - radius);
    const double dy = std::abs(py) - (halfHeight - radius);
    const double outside = std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
    return outside + std::min(std::max(dx, dy), 0.0) - radius;
}

// Paints the halo a shadow of this strength casts, in place, under a body of the
// given size. It is written scanline by scanline rather than with QPainter because
// the falloff has to be smooth and measured, not approximated by overlapping shapes.
// `corner` is how round the body's own corners are: the halo has to hug them, and
// taking the short side instead draws the shadow of a capsule around a rectangle,
// which leaves the corners of a wide picture bare.
QImage shadowHalo(const QSize &size, const QRectF &body, double radius, const QColor &tint,
                  double corner) {
    QImage halo(size, QImage::Format_ARGB32_Premultiplied);
    halo.fill(Qt::transparent);
    if (radius <= 0.0 || tint.alpha() == 0)
        return halo;
    const double halfWidth = body.width() / 2.0;
    const double halfHeight = body.height() / 2.0;
    corner = std::clamp(corner, 0.0, std::min(halfWidth, halfHeight));
    const QPointF centre = body.center();
    const double base = tint.alphaF();
    for (int y = 0; y < size.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(halo.scanLine(y));
        const double py = y + 0.5 - centre.y();
        for (int x = 0; x < size.width(); ++x) {
            const double distance =
                roundedRectDistance(x + 0.5 - centre.x(), py, halfWidth, halfHeight, corner);
            // Inside the body the reach would run past one and the easing would run
            // past its own end, so it is held at one: the halo is at its darkest where
            // it leaves the picture, and what is under the picture is none of its
            // business.
            const double reach = std::clamp((radius - distance) / radius, 0.0, 1.0);
            if (reach <= 0.0)
                continue;
            // The falloff is eased at both ends: squared alone leaves the halo hugging
            // the picture and then stopping, which reads as a second edge rather than
            // as light falling away.
            const double a = base * reach * reach * (3.0 - 2.0 * reach);
            line[x] = qRgba(int(tint.red() * a), int(tint.green() * a), int(tint.blue() * a),
                            int(a * 255.0));
        }
    }
    return halo;
}
} // namespace

QImage composeCapture(const QImage &region, const CaptureStyle &style) {
    if (region.isNull())
        return region;
    const int shortest = std::min(region.width(), region.height());
    const int border = style.border ? std::clamp(style.borderWidth, 1, std::max(1, shortest / 2)) : 0;
    const int radius = std::clamp(style.cornerRadius, 0, shortest / 2);
    const int padding = style.shadowRadius();
    if (border == 0 && radius == 0 && padding == 0)
        return region;
    QImage result(region.size() + QSize(padding * 2, padding * 2), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    const QRectF body(padding, padding, region.width(), region.height());
    if (padding > 0) {
        QPainter halo(&result);
        halo.drawImage(0, 0, shadowHalo(result.size(), body, padding, style.shadowTint(), radius));
        halo.end();
    }
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(body, radius, radius);
    painter.setClipPath(shape);
    painter.drawImage(body.topLeft(), region);
    painter.setClipping(false);
    if (border > 0) {
        // Half the pen falls outside the edge, so the rectangle is inset by half its
        // width to keep the border inside the picture.
        const double inset = border / 2.0;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(style.borderColor, border));
        painter.drawRoundedRect(body.adjusted(inset, inset, -inset, -inset),
                                std::max(0.0, radius - inset), std::max(0.0, radius - inset));
    }
    painter.end();
    return result;
}

QImage composeShadowPreview(const QSize &body, const CaptureStyle &style) {
    const int padding = style.shadowRadius();
    if (padding <= 0 || body.isEmpty())
        return {};
    // One halo is a scan over every pixel around the region, which on a full-screen
    // selection is a couple of million square roots. The capture window asks for the
    // same one on every repaint, and a repaint happens on every move of the pointer,
    // so the last one asked for is kept rather than recomputed.
    static QImage kept;
    static QSize keptBody;
    static CaptureStyle keptStyle;
    if (!kept.isNull() && keptBody == body && keptStyle == style)
        return kept;
    const int shortest = std::min(body.width(), body.height());
    const double corner = std::clamp(style.cornerRadius, 0, std::max(0, shortest / 2));
    QImage halo = shadowHalo(body + QSize(padding * 2, padding * 2),
                             QRectF(padding, padding, body.width(), body.height()), padding,
                             style.shadowTint(), corner);
    // What sits under the picture has to be clear rather than at its darkest, because
    // what is under the picture is the picture.
    QPainter cut(&halo);
    cut.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    cut.fillRect(QRect(padding, padding, body.width(), body.height()), Qt::black);
    cut.end();
    kept = halo;
    keptBody = body;
    keptStyle = style;
    return halo;
}

CaptureHistory::CaptureHistory(const QString &directory)
    : directory_(directory.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                                           .filePath(QStringLiteral("captures"))
                                     : directory) {}

int CaptureHistory::imageLimit() {
    return 20;
}

int CaptureHistory::selectionLimit() {
    return 10;
}

QString CaptureHistory::indexPath() const {
    return selectionIndexPath(directory_);
}

bool CaptureHistory::add(const QImage &image) {
    if (image.isNull())
        return false;
    if (!QDir().mkpath(directory_))
        return false;
    if (!image.save(QDir(directory_).filePath(nextCaptureName()), "PNG"))
        return false;
    const auto files = QDir(directory_).entryInfoList({QStringLiteral("capture-*.png")}, QDir::Files,
                                                      QDir::Name);
    for (int index = 0; index + imageLimit() < files.size(); ++index)
        QFile::remove(files.at(index).absoluteFilePath());
    return true;
}

QString CaptureHistory::nextCaptureName() const {
    // Names sort by age as plain text, so the newest is always the highest number and
    // there is no clock granularity to collide with: two captures taken in the same
    // millisecond still get their own file.
    static const QRegularExpression pattern(QStringLiteral("^capture-(\\d+)\\.png$"));
    int highest = 0;
    for (const auto &name : QDir(directory_).entryList({QStringLiteral("capture-*.png")}, QDir::Files)) {
        const auto match = pattern.match(name);
        if (match.hasMatch())
            highest = std::max(highest, match.captured(1).toInt());
    }
    return QStringLiteral("capture-%1.png").arg(highest + 1, 8, 10, QLatin1Char('0'));
}

int CaptureHistory::count() const {
    return int(QDir(directory_).entryList({QStringLiteral("capture-*.png")}, QDir::Files).size());
}

QImage CaptureHistory::at(int index) const {
    const auto files = QDir(directory_).entryInfoList({QStringLiteral("capture-*.png")}, QDir::Files,
                                                      QDir::Name | QDir::Reversed);
    if (index < 0 || index >= files.size())
        return {};
    return QImage(files.at(index).absoluteFilePath());
}

void CaptureHistory::clear() {
    QDir directory(directory_);
    for (const auto &name : directory.entryList({QStringLiteral("capture-*.png")}, QDir::Files))
        directory.remove(name);
    directory.remove(QFileInfo(indexPath()).fileName());
}

QVector<QRect> CaptureHistory::selections() const {
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray())
        return {};
    QVector<QRect> result;
    for (const auto &entry : document.array()) {
        const auto item = entry.toObject();
        result.append(QRect(item.value(QStringLiteral("x")).toInt(), item.value(QStringLiteral("y")).toInt(),
                            item.value(QStringLiteral("w")).toInt(), item.value(QStringLiteral("h")).toInt()));
    }
    return result;
}

void CaptureHistory::rememberSelection(const QRect &selection) {
    if (selection.isEmpty())
        return;
    QVector<QRect> updated{selection};
    // A rectangle the user picked twice belongs at the front, not in the list twice.
    for (const auto &existing : selections()) {
        if (existing == selection)
            continue;
        updated.append(existing);
        if (updated.size() >= selectionLimit())
            break;
    }
    if (!QDir().mkpath(directory_))
        return;
    QJsonArray array;
    for (const auto &rect : updated) {
        array.append(QJsonObject{{QStringLiteral("x"), rect.x()},
                                 {QStringLiteral("y"), rect.y()},
                                 {QStringLiteral("w"), rect.width()},
                                 {QStringLiteral("h"), rect.height()}});
    }
    QFile file(indexPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
} // namespace h2d
