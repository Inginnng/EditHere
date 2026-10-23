#pragma once
#include "model.h"

namespace h2d {
// These limits are independent of the single-image project size. The pixel
// budget also bounds decoded screenshots, including unusually compressible PNGs.
constexpr qint64 MaxVideoProjectFileBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxVideoProjectPixels = 128000000;
constexpr int MaxVideoFrames = 1000;
constexpr int MaxVideoAnnotations = 10000;

struct VideoFrame {
    qint64 timestampUs = 0;
    Document document;
};
struct VideoProject {
    QString source;
    qint64 durationMs = 0;
    qint64 positionMs = 0;
    QVector<VideoFrame> frames;
    bool dirty = false;
};

bool isVideoFile(const QString &path);
bool isVideoProjectFile(const QString &path);
VideoProject loadVideoProject(const QString &path);
QByteArray serializeVideoProject(const VideoProject &project);
QByteArray serializeVideoFeedback(const VideoProject &project, bool embed = true, bool compress = false);
int videoAnnotationCount(const VideoProject &project);
QString videoTimeLabel(qint64 ms);
} // namespace h2d
