#pragma once
#include <QString>
namespace h2d {
// Launch the desktop independently of the Agent caller's lifetime. An empty
// error means creation succeeded, not that the Agent endpoint is ready yet.
QString launchAgentDesktop(const QString &executable, qint64 *pid = nullptr);
} // namespace h2d
