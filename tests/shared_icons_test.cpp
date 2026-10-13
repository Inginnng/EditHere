#include "ui.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QSet>
#include <QTest>
#include <QTransform>

using namespace h2d;

static bool hasInk(const QImage &image) {
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() > 0)
                return true;
    return false;
}

static QColor opaqueInk(const QImage &image) {
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).alpha() == 255)
                return image.pixelColor(x, y);
    return {};
}

// Compare silhouettes rather than SVG text or theme colors. Normalize by ink
// coverage so a few mismatched dash segments cannot hide in the empty canvas.
static qreal alphaDifferenceFraction(const QImage &a, const QImage &b) {
    if (a.size() != b.size())
        return 1.;
    quint64 difference = 0;
    quint64 coverage = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const int first = a.pixelColor(x, y).alpha();
            const int second = b.pixelColor(x, y).alpha();
            difference += qAbs(first - second);
            coverage += first + second;
        }
    }
    return coverage ? qreal(difference) * 2. / qreal(coverage) : 1.;
}

class SharedIconsTest : public QObject {
    Q_OBJECT
  private slots:
    void registeredAssetsRenderAtToolbarSizesAndDisplayScales() {
        const auto names = glyphNames(); // Also forces static resource registration.
        QVERIFY(!names.isEmpty());
        QFile manifest(":/ui-icons/manifest.json");
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        QSet<QString> registered;
        const auto icons = QJsonDocument::fromJson(manifest.readAll()).object()["icons"].toArray();
        for (const auto &entry : icons) {
            const auto object = entry.toObject();
            const auto name = object["id"].toString();
            QVERIFY2(!registered.contains(name), qPrintable(name));
            registered.insert(name);
            QVERIFY2(QFile::exists(":/ui-icons/" + object["file"].toString()), qPrintable(name));
        }
        QCOMPARE(registered, QSet<QString>(names.cbegin(), names.cend()));
        for (const auto &name : names) {
            const auto icon = glyph(name);
            QVERIFY2(!icon.isNull(), qPrintable(name));
            QCOMPARE(icon.name(), name);
            for (const int size : {16, 20, 24, 48}) {
                for (const qreal scale : {1., 1.25, 2., 3.}) {
                    const auto pixmap = icon.pixmap(QSize(size, size), scale);
                    QCOMPARE(pixmap.size(), QSize(size, size) * scale);
                    QCOMPARE(pixmap.devicePixelRatio(), scale);
                    QVERIFY2(hasInk(pixmap.toImage()), qPrintable(name));
                }
            }
        }
    }

    void outputActionsHaveDistinctSilhouettes() {
        const QStringList names{"save", "image-save", "feedback-save", "json",
                                "json-copy-file", "json-copy-text", "image-copy"};
        for (int a = 0; a < names.size(); ++a)
            for (int b = a + 1; b < names.size(); ++b)
                QVERIFY2(glyph(names[a]).pixmap(24).toImage() != glyph(names[b]).pixmap(24).toImage(),
                         qPrintable(names[a] + " / " + names[b]));
    }

    void trimDirectionsHaveSymmetricSilhouettes_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("target");
        QTest::addColumn<bool>("mirrorHorizontal");
        QTest::addColumn<bool>("mirrorVertical");
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("size");
        QTest::addColumn<qreal>("scale");

        const auto add = [](const QString &label, const QString &source, const QString &target,
                            bool horizontal, bool vertical, int rotation) {
            for (const int size : {16, 20, 24}) {
                for (const qreal scale : {1., 2.}) {
                    const auto row = QString("%1-%2px-dpr%3").arg(label).arg(size).arg(scale);
                    QTest::newRow(qPrintable(row))
                        << source << target << horizontal << vertical << rotation << size << scale;
                }
            }
        };
        add("top-balanced", "scroll-trim-top", "scroll-trim-top", true, false, 0);
        add("bottom-balanced", "scroll-trim-bottom", "scroll-trim-bottom", true, false, 0);
        add("left-balanced", "scroll-trim-left", "scroll-trim-left", false, true, 0);
        add("right-balanced", "scroll-trim-right", "scroll-trim-right", false, true, 0);
        add("top-bottom", "scroll-trim-top", "scroll-trim-bottom", false, true, 0);
        add("left-right", "scroll-trim-left", "scroll-trim-right", true, false, 0);
        add("top-right-quarter-turn", "scroll-trim-top", "scroll-trim-right", false, false, 90);
        add("top-bottom-half-turn", "scroll-trim-top", "scroll-trim-bottom", false, false, 180);
        add("top-left-quarter-turn", "scroll-trim-top", "scroll-trim-left", false, false, 270);
    }

    void trimDirectionsHaveSymmetricSilhouettes() {
        QFETCH(QString, source);
        QFETCH(QString, target);
        QFETCH(bool, mirrorHorizontal);
        QFETCH(bool, mirrorVertical);
        QFETCH(int, rotation);
        QFETCH(int, size);
        QFETCH(qreal, scale);

        const auto first = glyph(source, Qt::white).pixmap(QSize(size, size), scale).toImage();
        const auto second = glyph(target, Qt::white).pixmap(QSize(size, size), scale).toImage();
        QVERIFY(hasInk(first));
        QVERIFY(hasInk(second));
        const auto transformed = rotation
            ? first.transformed(QTransform().rotate(rotation))
            : first.mirrored(mirrorHorizontal, mirrorVertical);
        const qreal difference = alphaDifferenceFraction(transformed, second);
        // Qt's curve rasterization may differ slightly by drawing direction;
        // meaningful gaps or one-sided dash placement exceed this allowance.
        QVERIFY2(difference <= .025,
                 qPrintable(QString("%1 / %2: silhouette difference %3%, allowed 2.5%")
                                .arg(source, target).arg(difference * 100., 0, 'f', 2)));
    }

    void themesCheckedDisabledAndExplicitColors() {
        for (const auto theme : {ThemeMode::Light, ThemeMode::Dark}) {
            applyTheme(theme);
            const auto icon = glyph("save");
            QCOMPARE(opaqueInk(icon.pixmap(48, QIcon::Normal, QIcon::Off).toImage()),
                     QColor(theme == ThemeMode::Dark ? "#d4d5dd" : "#606069"));
            QCOMPARE(opaqueInk(icon.pixmap(48, QIcon::Normal, QIcon::On).toImage()), accent());
            QCOMPARE(opaqueInk(icon.pixmap(48, QIcon::Disabled).toImage()),
                     qApp->palette().color(QPalette::Disabled, QPalette::ButtonText));
            const auto white = glyph("save", Qt::white);
            QCOMPARE(opaqueInk(white.pixmap(48, QIcon::Normal, QIcon::On).toImage()), QColor(Qt::white));
            const auto red = glyph("scroll-stop", QColor("#bd252f"));
            QCOMPARE(opaqueInk(red.pixmap(48).toImage()), QColor("#bd252f"));
        }
    }

    void unknownIdFailsExplicitly() {
        QTest::ignoreMessage(QtWarningMsg, "Unknown shared UI icon: \"missing-action-for-test\"");
        QVERIFY(glyph("missing-action-for-test").isNull());
    }
};

QTEST_MAIN(SharedIconsTest)
#include "shared_icons_test.moc"
