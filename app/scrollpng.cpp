#include "scrollstitch.h"
#include <QCoreApplication>
#include <QSaveFile>
#include <QScopeGuard>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace h2d {
bool ScrollStitcher::savePng(const QString &path, bool transpose, QString *error) const {
    if (error) error->clear();
    const auto fail = [&] {
        if (error) *error = QCoreApplication::translate("h2d::ScrollCapture",
            "无法保存超长 PNG，请检查磁盘空间和目录权限。");
        return false;
    };
    if (height() <= 0 || lastFrame().isNull() || path.isEmpty()) return fail();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail();
    const auto write = [&](const char *data, qint64 size) {
        return file.write(data, size) == size;
    };
    const auto chunk = [&](const char *type, const char *data, quint32 size) {
        char word[4];
        qToBigEndian(size, word);
        if (!write(word, 4) || !write(type, 4) || (size && !write(data, size))) return false;
        uLong crc = crc32(0, reinterpret_cast<const Bytef *>(type), 4);
        if (size) crc = crc32(crc, reinterpret_cast<const Bytef *>(data), size);
        qToBigEndian(quint32(crc), word);
        return write(word, 4);
    };
    const int width = transpose ? height() : lastFrame().width();
    const int outputHeight = transpose ? lastFrame().width() : height();
    const char signature[] = "\x89PNG\r\n\x1a\n";
    char header[13] = {};
    qToBigEndian(quint32(width), header);
    qToBigEndian(quint32(outputHeight), header + 4);
    header[8] = 8;
    header[9] = 6; // Eight-bit RGBA, non-interlaced.
    if (!write(signature, 8) || !chunk("IHDR", header, sizeof(header))) return fail();

    // Feed one zlib stream in strips; neither a giant bitmap nor the platform
    // image encoder's dimension limit applies. Z_SOLO uses these allocators.
    z_stream stream = {};
    stream.zalloc = [](voidpf, uInt count, uInt size) -> voidpf { return std::calloc(count, size); };
    stream.zfree = [](voidpf, voidpf address) { std::free(address); };
    if (deflateInit(&stream, Z_BEST_SPEED) != Z_OK) return fail();
    const auto cleanup = qScopeGuard([&] { deflateEnd(&stream); });
    char compressed[65536];
    const auto compress = [&](int flush) {
        int result;
        do {
            stream.next_out = reinterpret_cast<Bytef *>(compressed);
            stream.avail_out = sizeof(compressed);
            result = deflate(&stream, flush);
            if (result != Z_OK && result != Z_STREAM_END) return false;
            const auto size = quint32(sizeof(compressed) - stream.avail_out);
            if (size && !chunk("IDAT", compressed, size)) return false;
        } while (stream.avail_in || (flush == Z_FINISH && result != Z_STREAM_END));
        return true;
    };
    const qsizetype rowBytes = qsizetype(width) * 4;
    const int stripRows = int(std::max<qsizetype>(1, 4194304 / (rowBytes + 1)));
    for (int y = 0; y < outputHeight;) {
        const QImage strip = readRows(y, std::min(stripRows, outputHeight - y), transpose)
            .convertToFormat(QImage::Format_RGBA8888);
        if (strip.isNull()) return fail();
        QByteArray filtered((rowBytes + 1) * strip.height(), Qt::Uninitialized);
        for (int row = 0; row < strip.height(); ++row) {
            char *target = filtered.data() + (rowBytes + 1) * row;
            *target = 0; // PNG filter "None" preserves arbitrary captured pixels.
            std::memcpy(target + 1, strip.constScanLine(row), size_t(rowBytes));
        }
        stream.next_in = reinterpret_cast<Bytef *>(filtered.data());
        stream.avail_in = uInt(filtered.size());
        if (!compress(Z_NO_FLUSH)) return fail();
        y += strip.height();
    }
    if (!compress(Z_FINISH) || !chunk("IEND", nullptr, 0) || !file.commit()) return fail();
    return true;
}
} // namespace h2d