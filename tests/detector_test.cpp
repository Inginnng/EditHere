#include "detector.h"
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QTest>
using namespace h2d;
class DetectorTests : public QObject {
    Q_OBJECT
    QImage grid(QSize size, QRect rect, int columns, int rows, QColor background, QColor ink,
                int stroke = 1) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(background);
        QPainter p(&image);
        for (int x = 0; x <= columns; ++x)
            p.fillRect(rect.x() + x * rect.width() / columns, rect.y(), stroke, rect.height() + stroke, ink);
        for (int y = 0; y <= rows; ++y)
            p.fillRect(rect.x(), rect.y() + y * rect.height() / rows, rect.width() + stroke, stroke, ink);
        return image;
    }
    bool has(const QVector<Candidate> &candidates, QRect rect, QString method = {}) {
        for (const auto &c : candidates)
            if ((method.isEmpty() || c.target["method"] == method) &&
                std::abs(c.bounds.x() - rect.x()) <= 2 && std::abs(c.bounds.y() - rect.y()) <= 2 &&
                std::abs(c.bounds.width() - rect.width()) <= 3 &&
                std::abs(c.bounds.height() - rect.height()) <= 3)
                return true;
        return false;
    }
  private slots:
    void linedTables_data() {
        QTest::addColumn<QColor>("background");
        QTest::addColumn<QColor>("ink");
        QTest::addColumn<int>("stroke");
        QTest::newRow("faint-one-pixel") << QColor("#ffffff") << QColor("#f3f3f3") << 1;
        QTest::newRow("dark-one-pixel") << QColor("#202126") << QColor("#34353a") << 1;
        QTest::newRow("thick-border") << QColor("#ffffff") << QColor("#404040") << 3;
    }
    void linedTables() {
        QFETCH(QColor, background);
        QFETCH(QColor, ink);
        QFETCH(int, stroke);
        auto image = grid({800, 600}, {70, 60, 600, 400}, 3, 4, background, ink, stroke);
        const auto candidates = detectBlocks(image);
        QVERIFY(has(candidates, {70, 60, 600, 400}, "table-outer"));
        QVERIFY(has(candidates, {270, 160, 200, 100}, "table-cell"));
        QVERIFY(has(candidates, {70, 160, 600, 100}, "table-row"));
        QVERIFY(has(candidates, {270, 60, 200, 400}, "table-column"));
        int cells = 0;
        for (auto c : candidates)
            cells += c.target["method"] == "table-cell";
        QCOMPARE(cells, 12);
        CandidatePicker picker;
        picker.update(candidates, {330, 210});
        bool outer = false, cell = false, row = false, column = false;
        for (int i = 0; i < picker.count(); ++i) {
            const auto c = picker.current();
            QVERIFY(c.has_value());
            const auto method = c->target["method"].toString();
            outer |= method == "table-outer";
            cell |= method == "table-cell";
            row |= method == "table-row";
            column |= method == "table-column";
            picker.step(1);
        }
        QVERIFY(outer && cell && row && column);
        const auto layout = createLayout(image.size(), candidates);
        validateLayout(layout, image.size());
        QCOMPARE(renderLayout(image, layout).convertToFormat(QImage::Format_RGB32), image);
        const auto choices = layoutChoices(layout, {330, 210});
        bool whole = false;
        for (auto choice : choices)
            whole |= std::abs(choice.bounds.width() - 600) < 3 && std::abs(choice.bounds.height() - 400) < 3;
        QVERIFY(whole);
    }
    void textAndAlternatingRows() {
        QImage image(1400, 900, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        for (int row = 0; row < 8; ++row) {
            p.fillRect(80, 70 + row * 90, 1200, 90, row % 2 ? QColor("#f7f8fa") : Qt::white);
            for (int col = 0; col < 6; ++col) {
                // Deterministic text-like strokes, not solid empty cells.
                for (int letter = 0; letter < 8; ++letter) {
                    const int x = 100 + col * 200 + letter * 12, y = 102 + row * 90;
                    p.fillRect(x, y, 2, 14, QColor("#404448"));
                    p.fillRect(x, y, 8, 2, QColor("#404448"));
                    p.fillRect(x, y + 7, 7, 2, QColor("#404448"));
                }
            }
        }
        for (int x = 0; x <= 6; ++x)
            p.fillRect(80 + x * 200, 70, 1, 721, QColor("#e9ebee"));
        for (int y = 0; y <= 8; ++y)
            p.fillRect(80, 70 + y * 90, 1201, 1, QColor("#e9ebee"));
        p.end();
        auto candidates = detectBlocks(image);
        QVERIFY(has(candidates, {80, 70, 1200, 720}, "table-outer"));
        int cells = 0;
        for (auto c : candidates)
            cells += c.target["method"] == "table-cell";
        QCOMPARE(cells, 48);
        const auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            QVERIFY(image.save(QDir(folder).filePath("table-with-text.png")));
            QImage overlay = image;
            QPainter boxes(&overlay);
            boxes.setPen(QPen(QColor("#007aff"), 2));
            for (auto c : candidates)
                if (c.target["method"] == "table-cell")
                    boxes.drawRect(c.bounds.adjusted(2, 2, -2, -2));
            boxes.end();
            QVERIFY(overlay.save(QDir(folder).filePath("table-detected.png")));
        }
    }
    void denseTableKeepsStructureInExplosion() {
        auto image = grid({1100, 1100}, {40, 40, 1000, 1000}, 20, 20, Qt::white, QColor("#f3f3f3"));
        auto candidates = detectBlocks(image);
        // Add many native/legacy proposals, as on a complex web page.
        for (int i = 0; i < 300; ++i)
            candidates.append({{i % 20 * 45, i / 20 * 40, 16, 16}, manualTarget()});
        const auto layout = createLayout(image.size(), candidates);
        validateLayout(layout, image.size());
        auto choices = layoutChoices(layout, {555, 555});
        bool outer = false, row = false, column = false;
        for (auto choice : choices) {
            outer |= choice.label == "整个表格";
            row |= choice.label == "表格行";
            column |= choice.label == "表格列";
        }
        QVERIFY(outer && row && column);
    }
    void nativeResolutionAndDenseCells() {
        auto image = grid({3840, 2160}, {420, 200, 2880, 1620}, 18, 18, Qt::white, QColor("#f3f3f3"));
        QElapsedTimer timer;
        timer.start();
        auto candidates = detectBlocks(image);
        qInfo() << "4K detection milliseconds:" << timer.elapsed();
        QVERIFY(candidates.size() <= 480);
        QVERIFY(has(candidates, {420, 200, 2880, 1620}, "table-outer"));
        int cells = 0;
        for (auto c : candidates)
            cells += c.target["method"] == "table-cell";
        QCOMPARE(cells, 324);
        QVERIFY(has(candidates, {3140, 1730, 160, 90}, "table-cell"));
        auto folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            QVERIFY(image.save(QDir(folder).filePath("faint-table-4k.png")));
        }
    }
    void mergedCellsAndBrokenRules() {
        auto image = grid({720, 480}, {60, 60, 600, 300}, 3, 3, Qt::white, QColor("#f3f3f3"));
        QPainter p(&image);
        p.fillRect(260, 61, 1, 99, Qt::white);
        p.fillRect(460, 61, 1, 99, Qt::white);
        p.fillRect(360, 260, 2, 1, Qt::white);
        p.end();
        auto candidates = detectBlocks(image);
        QVERIFY(has(candidates, {60, 60, 600, 100}));
        QVERIFY(!has(candidates, {260, 60, 200, 100}));
        QVERIFY(has(candidates, {260, 160, 200, 100}, "table-cell"));
        QVERIFY(has(candidates, {60, 60, 600, 300}, "table-outer"));
    }
    void separatedTablesAndFramedImage() {
        QImage image(1000, 600, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        p.drawImage(0, 0, grid({450, 450}, {30, 30, 360, 360}, 3, 3, Qt::white, QColor("#eeeeee")));
        p.drawImage(500, 0, grid({450, 450}, {30, 30, 360, 360}, 3, 3, Qt::white, QColor("#eeeeee")));
        p.fillRect(40, 480, 200, 80, QColor("#555555"));
        p.fillRect(41, 481, 198, 78, QColor("#999999"));
        p.end();
        auto candidates = detectBlocks(image);
        QVERIFY(has(candidates, {30, 30, 360, 360}, "table-outer"));
        QVERIFY(has(candidates, {530, 30, 360, 360}, "table-outer"));
        QVERIFY(!has(candidates, {30, 30, 860, 360}));
        QVERIFY(has(candidates, {40, 480, 199, 79}, "table-frame"));
    }
    void rejectsNonGridTextureAndLines() {
        QImage image(800, 600, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(detectTableBlocks(image).isEmpty());
        QPainter p(&image);
        for (int y = 40; y < 500; y += 35)
            p.fillRect(40, y, 700, 1, QColor("#eeeeee"));
        p.end();
        QVERIFY(detectTableBlocks(image).isEmpty());
        quint32 seed = 42;
        for (int y = 0; y < image.height(); ++y) {
            auto row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                seed = seed * 1664525 + 1013904223;
                int v = seed >> 24;
                row[x] = qRgb(v, v, v);
            }
        }
        QVERIFY(detectTableBlocks(image).isEmpty());
    }
};
QTEST_GUILESS_MAIN(DetectorTests)
#include "detector_test.moc"