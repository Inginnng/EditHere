#pragma once
#include "aifeedback.h"
#include "model.h"
#include "videoproject.h"
namespace h2d {
AiFeedbackInput aiFeedbackSnapshot(const Document &, FeedbackExportOptions options = {});
AiFeedbackInput aiFeedbackSnapshot(const VideoProject &, FeedbackExportOptions options = {});
} // namespace h2d
