#pragma once
#include "model.h"
#include <QJsonArray>

namespace h2d {
inline constexpr auto ImageSetVersion = "edithere-image-set-1";
inline constexpr auto ImageSetFeedbackVersion = "image-set-feedback-1";
struct ImageSetPage {
    QString id, name;
    Document document;
};
struct ImageSet {
    QString name;
    QVector<ImageSetPage> pages;
    QJsonArray groupNotes;
    bool dirty = false;
};
bool isImageSetFile(const QString &path);
ImageSet loadImageSet(const QString &path);
void validateImageSet(const ImageSet &set);
QByteArray serializeImageSet(const ImageSet &set);
QByteArray serializeImageSetFeedback(const ImageSet &set, bool embed = true, bool compress = false, bool includeRecognizedText = true);
FeedbackBundle buildImageSetFeedbackBundle(const ImageSet &set, const FeedbackExportOptions &options = {});
int imageSetAnnotationCount(const ImageSet &set);
} // namespace h2d
