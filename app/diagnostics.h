#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Local diagnostics have no UI, network or project-model dependency. Callers
// should pass operation metadata and errors, never image/project/annotation data.
namespace h2d::diagnostics {
enum class Level { Info, Warning, Error };
struct Options {
    QString directory;
    qint64 maxFileBytes = 1024 * 1024;
    int maxFiles = 10;
};

// Start once per process, after setting the application's name and version.
// An empty directory uses AppLocalDataLocation/logs. Failure is non-fatal.
bool start(const Options &options = {}, QString *error = nullptr);
void stop();
void write(Level level, const QString &component, const QString &message,
           const QJsonObject &fields = {});
QString directory();
QString lastError();
// Absolute paths, oldest first. Active processes' files are never pruned.
QStringList logFiles();
// Atomically export up to 4 MiB of recent diagnostics. Empty means success.
// Basic path/URL/secret redaction is best effort, not a privacy guarantee.
QString exportReport(const QString &destination);
} // namespace h2d::diagnostics
