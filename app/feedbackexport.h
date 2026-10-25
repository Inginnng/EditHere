#pragma once
#include <QByteArray>
#include <QMap>
#include <QRegularExpression>
#include <QString>

namespace h2d {
enum class ImageExportMode { File, Embedded, None };
struct FeedbackExportOptions {
    ImageExportMode mode = ImageExportMode::File;
    bool compress = false;
    bool includeRecognizedText = true;
    bool includeWindowTitle = true;
    bool preview = false;
    QString imageFile = "feedback.png";
    QString previewFile;
};
// All encoded bytes exist before publication. Filenames are relative basenames;
// the publisher commits sidecars before the JSON that references them.
struct FeedbackBundle {
    QByteArray json;
    QMap<QString, QByteArray> files;
    QString imageFile;
    QString previewFile;
};
inline bool safeFeedbackImageName(const QString &name, bool jpegOnly = false) {
    static const QRegularExpression invalid("[\\\\/:*?\"<>|\\x{0000}-\\x{001f}]");
    static const QRegularExpression reserved("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && name.size() <= 255 && !name.startsWith('.') &&
        !name.endsWith('.') && !name.endsWith(' ') && !invalid.match(name).hasMatch() &&
        !reserved.match(name).hasMatch() &&
        (name.endsWith(".jpg", Qt::CaseInsensitive) || name.endsWith(".jpeg", Qt::CaseInsensitive) ||
         (!jpegOnly && (name.endsWith(".png", Qt::CaseInsensitive) || name.endsWith(".webp", Qt::CaseInsensitive) ||
                        name.endsWith(".gif", Qt::CaseInsensitive) || name.endsWith(".bmp", Qt::CaseInsensitive))));
}
} // namespace h2d
