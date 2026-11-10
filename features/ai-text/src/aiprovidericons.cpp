#include "aiprovidericons.h"
#include "ui.h"
#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>

static void initializeAiProviderIcons() {
    Q_INIT_RESOURCE(ai_provider_icons);
}

namespace h2d {
namespace {
// Keep the original paths as SVG so device-scale and larger preview requests do
// not upscale a 24px bitmap. currentColor follows the active EditHere theme;
// colours explicitly supplied by the upstream brand asset remain unchanged.
class ProviderIconEngine final : public QIconEngine {
  public:
    ProviderIconEngine(QByteArray source, int nominalSize)
        : source_(std::move(source)), nominalSize_(nominalSize) {}
    QIconEngine *clone() const override { return new ProviderIconEngine(source_, nominalSize_); }
    QString key() const override { return QStringLiteral("EditHereAiProviderSvg"); }
    bool isNull() override { return source_.isEmpty(); }
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override {
        return {QSize(nominalSize_, nominalSize_)};
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        auto svg = source_;
        const auto foreground = isDarkTheme() ? QColor("#e9e9ef") : QColor("#25262b");
        svg.replace("currentColor", foreground.name().toUtf8());
        QSvgRenderer renderer(svg);
        painter->save();
        if (mode == QIcon::Disabled) painter->setOpacity(painter->opacity() * 0.45);
        renderer.render(painter, QRectF(rect));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        if (size.isEmpty() || scale <= 0) return {};
        QPixmap result(size * scale);
        result.fill(Qt::transparent);
        result.setDevicePixelRatio(scale);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
  private:
    QByteArray source_;
    int nominalSize_;
};
} // namespace

QIcon aiProviderIcon(const QString &family, int size) {
    const auto brand = family.section('-', 0, 0).toLower();
    if (brand != "openai" && brand != "claude" && brand != "deepseek" && brand != "glm" && brand != "kimi")
        return glyph("settings");
    static const bool initialized = [] { initializeAiProviderIcons(); return true; }();
    Q_UNUSED(initialized);
    QFile file(":/ai/providers/" + brand + ".svg");
    if (!file.open(QIODevice::ReadOnly)) return glyph("settings");
    return QIcon(new ProviderIconEngine(file.readAll(), qMax(1, size)));
}
} // namespace h2d
