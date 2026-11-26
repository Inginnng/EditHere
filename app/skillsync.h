#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace h2d {

struct SkillVersionInfo {
    QString skillVersion;
    QString minCompatibleSkillVersion;
};

struct SkillHostStatus {
    QString id;
    QString name;
    QString hostRoot;
    QString skillDir;
    bool hostPresent = false;
    bool installed = false;
    QString installedVersion;
    QString status; // not-installed | legacy | outdated | path-stale | up-to-date | newer
    QString recordedCliPath;
    QString recordedMcpPath;
    QStringList otherVariants;
};

struct SkillSyncSnapshot {
    QString bundledSkillVersion;
    QString minCompatibleSkillVersion;
    QString appVersion;
    QString sourceSkillDir;
    bool sourceAvailable = false;
    QString cliPath;
    QString unifiedCliPath;
    QString unifiedMcpPath;
    QVector<SkillHostStatus> hosts;
};

struct SkillSyncActionResult {
    QString id;
    QString name;
    QString destination;
    QString state; // installed | updated | up-to-date | skipped-newer | skipped-not-installed | failed
    QString fromVersion;
    QString toVersion;
    QString error;
};

int compareSkillSemver(const QString &a, const QString &b);
SkillVersionInfo bundledSkillVersionInfo(const QString &skillSourceDir = {});
QString resolveBundledSkillDir(const QString &appDir = {});

SkillSyncSnapshot inspectHostSkills(const QString &homeDir = {},
                                    const QString &codexHome = {},
                                    const QString &appDir = {});

QVector<SkillSyncActionResult> syncHostSkills(const QStringList &hostIds,
                                              bool onlyUpdateInstalled = true,
                                              bool force = false,
                                              const QString &homeDir = {},
                                              const QString &codexHome = {},
                                              const QString &appDir = {});

QJsonObject skillSyncSnapshotToJson(const SkillSyncSnapshot &snapshot);
QJsonArray skillSyncResultsToJson(const QVector<SkillSyncActionResult> &results);

} // namespace h2d
