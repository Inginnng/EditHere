#include "ocr.h"
#include <QMap>
namespace h2d {
bool parseTesseractTsv(const QByteArray &payload, const QRect &band, const QSize &size,
                      QVector<OcrLine> *lines) {
    if (!lines || !size.isValid() || !band.isValid() || payload.size() > 8 * 1024 * 1024) return false;
    const auto rows = QString::fromUtf8(payload).split('\n');
    if (rows.isEmpty() || !rows.first().startsWith("level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext")) return false;
    struct Line { QString text; QRectF box; };
    QVector<Line> collected;
    QMap<QString, int> indexes;
    for (int row = 1; row < rows.size(); ++row) {
        if (rows[row].trimmed().isEmpty()) continue;
        const auto fields = rows[row].split('\t');
        if (fields.size() < 12) return false;
        if (fields[0] != "5") continue;
        bool ok = false;
        const int left = fields[6].toInt(&ok); if (!ok) return false;
        const int top = fields[7].toInt(&ok); if (!ok) return false;
        const int width = fields[8].toInt(&ok); if (!ok) return false;
        const int height = fields[9].toInt(&ok); if (!ok) return false;
        const QString text = fields.mid(11).join('\t').trimmed();
        if (text.isEmpty()) continue;
        if (left < 0 || top < 0 || width <= 0 || height <= 0 ||
            qint64(left) + width > band.width() || qint64(top) + height > band.height()) return false;
        const auto key = fields.mid(1, 4).join(':');
        const QRectF box(left, top, width, height);
        if (!indexes.contains(key)) { indexes.insert(key, collected.size()); collected.append({text, box}); }
        else { auto &line = collected[indexes[key]]; line.text += ' ' + text; line.box = line.box.united(box); }
    }
    QVector<OcrLine> result;
    for (const auto &line : collected) result.append({line.text, ocrBandBoxToImage(line.box, band, size)});
    *lines = result;
    return true;
}
}
