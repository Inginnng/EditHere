#include "aifeedbacksnapshot.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <stdexcept>
namespace h2d {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("h2d", text); }
void appendImages(AiFeedbackInput &input, const Document &document, const QString &prefix) {
    input.images.append(document.image);
    input.imageLabels.append(prefix + tr("原图；像素尺寸 %1 × %2。组件移动的 source 在原图中定位。")
                                         .arg(document.image.width()).arg(document.image.height()));
    if (document.layout && !exportLayoutChanges(*document.layout).isEmpty()) {
        input.images.append(renderLayout(document.image, *document.layout));
        input.imageLabels.append(prefix + tr("布局调整后的结果图，与原图尺寸相同。普通批注及 children 的 source 位于此图；movement 的 source 位于原图。"));
    } else {
        input.imageLabels.last() += tr("本帧没有布局移动，原图也是结果图。普通批注在此图中定位。");
    }
}
}
AiFeedbackInput aiFeedbackSnapshot(const Document &document, FeedbackExportOptions options) {
    options.mode = ImageExportMode::None; options.preview = false;
    AiFeedbackInput input;
    input.feedback = exportFeedback(document, options);
    appendImages(input, document, {});
    return input;
}
AiFeedbackInput aiFeedbackSnapshot(const VideoProject &project, FeedbackExportOptions options) {
    options.mode = ImageExportMode::None; options.preview = false;
    AiFeedbackInput input;
    input.feedback = QJsonDocument::fromJson(buildVideoFeedbackBundle(project, options).json).object();
    const auto frames = input.feedback["frames"].toArray();
    if (frames.size() > 8)
        throw std::runtime_error(tr("一次最多整理 8 个视频帧，请分批处理；未发送任何图片。").toUtf8().constData());
    for (const auto &value : frames) {
        const auto frame = value.toObject();
        for (const auto &source : project.frames) {
            if (source.document.id != frame["id"].toString()) continue;
            appendImages(input, source.document, tr("视频帧 %1，时间戳 %2 µs：")
                                                  .arg(source.document.id).arg(source.timestampUs));
            break;
        }
    }
    return input;
}
} // namespace h2d
