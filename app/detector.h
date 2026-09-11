#pragma once
#include "model.h"
namespace h2d {
QVector<Candidate> detectTableBlocks(const QImage &image);
QVector<Candidate> detectBlocks(const QImage &image);
} // namespace h2d
