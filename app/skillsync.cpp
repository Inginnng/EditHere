#include "skillsync.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <array>

namespace h2d {
namespace {

struct HostDef {
    QString id;
    QString name;
    QString hostRoot;
};

bool isValidSemver(const QString &v) {
    static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
    return re.match(v.trimmed()).hasMatch();
}

std::array<int, 3> parseSemverParts(const QString &v) {
    const QString cleaned = v.trimmed();
    if (!isValidSemver(cleaned)) return {0, 0, 0};
    const QStringList parts = cleaned.split('.');
    return {parts[0].toInt(), parts[1].toInt(), parts[2].toInt()};
}

QString normPath(const QString &p) {
    if (p.isEmpty()) return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

bool sameNormalizedPath(const QString &a, const QString &b) {
    const QString na = normPath(a);
    const QString nb = normPath(b);
    if (na.isEmpty() || nb.isEmpty()) return na == nb;
    if (QFileInfo::exists(na) && QFileInfo::exists(nb)) {
        const QString ca = QFileInfo(na).canonicalFilePath();
        const QString cb = QFileInfo(nb).canonicalFilePath();
        if (!ca.isEmpty() && !cb.isEmpty()) {
#ifdef Q_OS_WIN
            return ca.compare(cb, Qt::CaseInsensitive) == 0;
#else
            return ca == cb;
#endif
        }
    }
#ifdef Q_OS_WIN
    return na.compare(nb, Qt::CaseInsensitive) == 0;
#else
    return na == nb;
#endif
}

QStringList bundledDataDirectories(const QString &appDir) {
    if (appDir.isEmpty())
        return {};
    const QDir dir(appDir);
    QStringList directories{appDir};
#ifdef Q_OS_MACOS
    directories << dir.filePath("../Resources");
#elif defined(Q_OS_LINUX)
    directories << dir.filePath("../share/edithere");
#endif
    directories << dir.filePath("..");
    return directories;
}

QString resolveBundledFile(const QString &appDir, const QString &relative) {
    for (const auto &directory : bundledDataDirectories(appDir)) {
        const QFileInfo file(QDir(directory).filePath(relative));
        if (file.isFile()) {
            const QString canonical = file.canonicalFilePath();
            return canonical.isEmpty() ? normPath(file.absoluteFilePath()) : canonical;
        }
    }
    return {};
}

bool renameSkillDirectory(const QString &source, const QString &target) {
#ifdef Q_OS_WIN
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (QDir().rename(source, target))
            return true;
        if (!QFileInfo::exists(source) || QFileInfo::exists(target))
            return false;
        QThread::msleep(100);
    }
#endif
    return QDir().rename(source, target);
}

QString pickExistingRoot(const QString &primary, const QString &fallback = {}) {
    const QString p = normPath(primary);
    const QString f = normPath(fallback);
    if (f.isEmpty()) return p;
    const bool pSkill = QFileInfo::exists(QDir(p).filePath("skills/edithere/SKILL.md"));
    const bool fSkill = QFileInfo::exists(QDir(f).filePath("skills/edithere/SKILL.md"));
    if (!pSkill && fSkill) return f;
    const QFileInfo pInfo(p);
    const QFileInfo fInfo(f);
    if (!(pInfo.exists() && pInfo.isDir()) && (fInfo.exists() && fInfo.isDir())) return f;
    return p;
}

QVector<HostDef> hostDefinitions(const QString &homeDir, const QString &codexHome) {
    QFile file(QStringLiteral(":/shared/ai-hosts.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto hosts = QJsonDocument::fromJson(file.readAll()).array();
    const QDir home(homeDir.isEmpty() ? normPath(QDir::homePath()) : normPath(homeDir));
    QVector<HostDef> definitions;
    definitions.reserve(hosts.size());
    for (const auto &value : hosts) {
        const auto host = value.toObject();
        const auto id = host.value("id").toString();
        const auto paths = host.value("paths").toArray();
        const auto envVar = host.value("envVar").toString().toLatin1();
        QString root;
        if (id == "codex")
            root = normPath(codexHome);
        if (root.isEmpty() && homeDir.isEmpty() && !envVar.isEmpty())
            root = normPath(qEnvironmentVariable(envVar.constData()));
        if (root.isEmpty()) {
            const auto primary = home.filePath(paths.at(0).toString());
            const auto fallback = paths.size() > 1 ? home.filePath(paths.at(1).toString()) : QString{};
            root = pickExistingRoot(primary, fallback);
        }
        definitions.append({id, host.value("name").toString(), root});
    }
    return definitions;
}

bool readSkillMetaFile(const QString &skillDir, QJsonObject *outMeta) {
    const QString metaPath = QDir(skillDir).filePath(".edithere-skill.json");
    QFile f(metaPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    *outMeta = doc.object();
    return true;
}

QString withHostSkillSyncHint(const QString &reason) {
    return reason + QLatin1Char('\n') + QCoreApplication::translate("h2d", "请让AI工具自行更新skills或刷新路径");
}

bool copyDirectoryTree(const QString &srcDir, const QString &dstDir, QString *error) {
    QDir rootSrc(srcDir);
    if (!rootSrc.exists()) {
        if (error) *error = QCoreApplication::translate("h2d", "找不到 Skill 源目录：%1")
            .arg(QDir::toNativeSeparators(srcDir));
        return false;
    }
    if (!QDir().mkpath(dstDir)) {
        if (error) *error = withHostSkillSyncHint(
            QCoreApplication::translate("h2d", "无法创建 Skill 同步临时目录：%1")
                .arg(QDir::toNativeSeparators(dstDir)));
        return false;
    }
    QDirIterator it(srcDir, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isSymLink()) continue;
        const QString rel = rootSrc.relativeFilePath(fi.filePath());
        if (rel == ".edithere-skill.json" || rel == "references/local-installation.md") continue;
        const QString targetPath = QDir(dstDir).filePath(rel);
        if (fi.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                if (error) *error = withHostSkillSyncHint(
                    QCoreApplication::translate("h2d", "无法创建 Skill 同步子目录：%1")
                        .arg(QDir::toNativeSeparators(targetPath)));
                return false;
            }
        } else {
            const QString parent = QFileInfo(targetPath).absolutePath();
            if (!QDir().mkpath(parent)) {
                if (error) *error = withHostSkillSyncHint(
                    QCoreApplication::translate("h2d", "无法创建 Skill 同步子目录：%1")
                        .arg(QDir::toNativeSeparators(parent)));
                return false;
            }
            if (QFileInfo::exists(targetPath)) QFile::remove(targetPath);
            if (!QFile::copy(fi.filePath(), targetPath)) {
                if (error) *error = QCoreApplication::translate("h2d", "无法复制 Skill 文件：%1")
                    .arg(QDir::toNativeSeparators(rel));
                return false;
            }
        }
    }
    return true;
}

QString buildLocalInstallationMarkdown(const SkillSyncSnapshot &snapshot) {
    QString nodeExec = QStandardPaths::findExecutable("node");
    if (nodeExec.isEmpty()) nodeExec = QStringLiteral("node");
    nodeExec = normPath(nodeExec);

    QJsonObject mcpConfig;
    if (!snapshot.unifiedMcpPath.isEmpty()) {
        mcpConfig = QJsonObject{
            {"mcpServers", QJsonObject{
                {"edithere", QJsonObject{
                    {"command", nodeExec},
                    {"args", QJsonArray{snapshot.unifiedMcpPath}},
                }},
            }},
        };
    }

    QStringList lines;
    lines << "# Local EditHere Entry Points"
          << ""
          << "Generated by EditHere Skill Sync (`" + snapshot.bundledSkillVersion + "`)."
          << "";
    if (!snapshot.unifiedCliPath.isEmpty() || !snapshot.unifiedMcpPath.isEmpty()) {
        lines << "## Unified CLI & MCP (Auto-Routes Desktop or Web)"
              << ""
              << "- Node.js: `" + nodeExec + "`";
        if (!snapshot.unifiedCliPath.isEmpty())
            lines << "- Unified CLI: `" + snapshot.unifiedCliPath + "`";
        if (!snapshot.unifiedMcpPath.isEmpty())
            lines << "- Unified MCP Server: `" + snapshot.unifiedMcpPath + "`";
        lines << "";
    }
    lines << "## Desktop CLI"
          << ""
          << (!snapshot.cliPath.isEmpty()
                  ? "- Path (desktop-app): `" + snapshot.cliPath + "`"
                  : "- Desktop CLI binary was not located next to EditHere.")
          << "";
    if (!mcpConfig.isEmpty()) {
        lines << "## Host MCP Configuration Snippet"
              << ""
              << "```json"
              << QString::fromUtf8(QJsonDocument(mcpConfig).toJson(QJsonDocument::Indented)).trimmed()
              << "```"
              << "";
    }
    return lines.join('\n');
}

bool writeTextUtf8(const QString &filePath, const QByteArray &bytes, QString *error) {
    const QString parent = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(parent)) {
        if (error) *error = withHostSkillSyncHint(
            QCoreApplication::translate("h2d", "无法创建 Skill 同步子目录：%1")
                .arg(QDir::toNativeSeparators(parent)));
        return false;
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = withHostSkillSyncHint(
            QCoreApplication::translate("h2d", "无法打开 Skill 文件进行写入：%1")
                .arg(QDir::toNativeSeparators(filePath)));
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = withHostSkillSyncHint(
            QCoreApplication::translate("h2d", "未能完整写入 Skill 文件：%1")
                .arg(QDir::toNativeSeparators(filePath)));
        return false;
    }
    return true;
}

} // namespace

int compareSkillSemver(const QString &a, const QString &b) {
    const auto pa = parseSemverParts(a);
    const auto pb = parseSemverParts(b);
    for (int i = 0; i < 3; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

SkillVersionInfo bundledSkillVersionInfo(const QString &skillSourceDir) {
    SkillVersionInfo info{
        QStringLiteral(EDITHERE_SKILL_VERSION),
        QStringLiteral(EDITHERE_MIN_SKILL_VERSION),
    };
    if (skillSourceDir.isEmpty()) return info;
    QFile f(QDir(skillSourceDir).filePath("skill-version.json"));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return info;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return info;
    const QJsonObject obj = doc.object();
    const QString sv = obj.value("skillVersion").toString().trimmed();
    const QString msv = obj.value("minCompatibleSkillVersion").toString().trimmed();
    if (isValidSemver(sv)) info.skillVersion = sv;
    if (isValidSemver(msv)) info.minCompatibleSkillVersion = msv;
    return info;
}

QString resolveBundledSkillDir(const QString &appDir) {
    const QString base = appDir.isEmpty() ? normPath(QCoreApplication::applicationDirPath()) : normPath(appDir);
    const QString marker = resolveBundledFile(base, QStringLiteral("skills/edithere/SKILL.md"));
    return marker.isEmpty() ? QString() : QFileInfo(marker).absolutePath();
}

SkillSyncSnapshot inspectHostSkills(const QString &homeDir, const QString &codexHome, const QString &appDir) {
    const QString base = appDir.isEmpty() ? normPath(QCoreApplication::applicationDirPath()) : normPath(appDir);
    SkillSyncSnapshot snapshot;
    snapshot.appVersion = QStringLiteral(EDITHERE_VERSION);
    snapshot.sourceSkillDir = resolveBundledSkillDir(base);
    snapshot.sourceAvailable = !snapshot.sourceSkillDir.isEmpty();

    const SkillVersionInfo verInfo = bundledSkillVersionInfo(snapshot.sourceSkillDir);
    snapshot.bundledSkillVersion = verInfo.skillVersion;
    snapshot.minCompatibleSkillVersion = verInfo.minCompatibleSkillVersion;

#ifdef Q_OS_WIN
    snapshot.cliPath = resolveBundledFile(base, QStringLiteral("edithere-cli.exe"));
#else
    snapshot.cliPath = resolveBundledFile(base, QStringLiteral("edithere-cli"));
#endif
    snapshot.unifiedCliPath = resolveBundledFile(base, QStringLiteral("cli/edithere.mjs"));
    snapshot.unifiedMcpPath = resolveBundledFile(base, QStringLiteral("cli/unified-mcp.mjs"));

    const auto defs = hostDefinitions(homeDir, codexHome);
    snapshot.hosts.reserve(defs.size());
    for (const auto &def : defs) {
        SkillHostStatus st;
        st.id = def.id;
        st.name = def.name;
        st.hostRoot = def.hostRoot;
        st.skillDir = normPath(QDir(def.hostRoot).filePath("skills/edithere"));
        st.hostPresent = QFileInfo::exists(def.hostRoot) && QFileInfo(def.hostRoot).isDir();
        st.installed = QFileInfo::exists(QDir(st.skillDir).filePath("SKILL.md"));
        for (const QString &variant : {QStringLiteral("edithere-web"), QStringLiteral("edithere-dsh")}) {
            if (QFileInfo::exists(QDir(def.hostRoot).filePath(QString("skills/%1/SKILL.md").arg(variant)))) {
                st.otherVariants.append(variant);
            }
        }

        if (!st.installed) {
            st.status = QStringLiteral("not-installed");
        } else {
            QJsonObject meta;
            if (!readSkillMetaFile(st.skillDir, &meta)) {
                st.status = QStringLiteral("legacy");
            } else {
                const QString ver = meta.value("skillVersion").toString().trimmed();
                st.recordedCliPath = normPath(meta.value("cliPath").toString());
                st.recordedMcpPath = normPath(meta.value("unifiedMcpPath").toString());
                if (!isValidSemver(ver)) {
                    st.status = QStringLiteral("legacy");
                } else {
                    st.installedVersion = ver;
                    const int cmp = compareSkillSemver(ver, snapshot.bundledSkillVersion);
                    if (cmp < 0) {
                        st.status = QStringLiteral("outdated");
                    } else if (cmp > 0) {
                        st.status = QStringLiteral("newer");
                    } else {
                        const bool cliStale = !snapshot.cliPath.isEmpty() &&
                            (!st.recordedCliPath.isEmpty()
                                 ? (!QFileInfo::exists(st.recordedCliPath) || !sameNormalizedPath(st.recordedCliPath, snapshot.cliPath))
                                 : st.recordedMcpPath.isEmpty());
                        const bool mcpStale = !snapshot.unifiedMcpPath.isEmpty() && !st.recordedMcpPath.isEmpty() &&
                            (!QFileInfo::exists(st.recordedMcpPath) || !sameNormalizedPath(st.recordedMcpPath, snapshot.unifiedMcpPath));
                        const bool missingRecordedTarget =
                            (!st.recordedCliPath.isEmpty() && !QFileInfo::exists(st.recordedCliPath)) ||
                            (!st.recordedMcpPath.isEmpty() && !QFileInfo::exists(st.recordedMcpPath));
                        st.status = (cliStale || mcpStale || missingRecordedTarget)
                            ? QStringLiteral("path-stale")
                            : QStringLiteral("up-to-date");
                    }
                }
            }
        }
        snapshot.hosts.append(std::move(st));
    }
    return snapshot;
}

QVector<SkillSyncActionResult> syncHostSkills(const QStringList &hostIds,
                                              bool onlyUpdateInstalled,
                                              bool force,
                                              const QString &homeDir,
                                              const QString &codexHome,
                                              const QString &appDir) {
    const SkillSyncSnapshot snapshot = inspectHostSkills(homeDir, codexHome, appDir);
    QVector<SkillSyncActionResult> results;

    QStringList selectedIds = hostIds;
    if (selectedIds.isEmpty()) {
        if (onlyUpdateInstalled) {
            for (const auto &h : snapshot.hosts) {
                if (h.status == "outdated" || h.status == "legacy" || h.status == "path-stale" ||
                    (force && (h.status == "up-to-date" || h.status == "newer"))) {
                    selectedIds.append(h.id);
                }
            }
        } else {
            for (const auto &h : snapshot.hosts) {
                if (h.hostPresent) selectedIds.append(h.id);
            }
            if (selectedIds.isEmpty()) selectedIds.append(QStringLiteral("codex"));
        }
    }

    for (const QString &id : selectedIds) {
        auto it = std::find_if(snapshot.hosts.cbegin(), snapshot.hosts.cend(),
                               [&id](const SkillHostStatus &h) { return h.id == id; });
        if (it == snapshot.hosts.cend()) {
            results.append({id, id, {}, QStringLiteral("failed"), {}, {}, QString("Unknown AI host: %1").arg(id)});
            continue;
        }
        const SkillHostStatus &h = *it;
        SkillSyncActionResult res;
        res.id = h.id;
        res.name = h.name;
        res.destination = h.skillDir;
        res.fromVersion = h.installedVersion;
        res.toVersion = snapshot.bundledSkillVersion;

        if (onlyUpdateInstalled && h.status == "not-installed") {
            res.state = QStringLiteral("skipped-not-installed");
            results.append(std::move(res));
            continue;
        }
        if (!force && h.status == "up-to-date") {
            res.state = QStringLiteral("up-to-date");
            results.append(std::move(res));
            continue;
        }
        if (!force && h.status == "newer") {
            res.state = QStringLiteral("skipped-newer");
            results.append(std::move(res));
            continue;
        }
        if (!snapshot.sourceAvailable) {
            res.state = QStringLiteral("failed");
            res.error = QCoreApplication::translate("h2d", "找不到 EditHere 随附的 Skill 目录。");
            results.append(std::move(res));
            continue;
        }

        const QString parentDir = QFileInfo(h.skillDir).absolutePath();
        if (!QDir().mkpath(parentDir)) {
            res.state = QStringLiteral("failed");
            res.error = withHostSkillSyncHint(
                QCoreApplication::translate("h2d", "无法创建 AI 工具的 skills 目录：%1")
                    .arg(QDir::toNativeSeparators(parentDir)));
            results.append(std::move(res));
            continue;
        }

        const QString suffix = QUuid::createUuid().toString(QUuid::Id128).left(12);
        const QString staging = QDir(parentDir).filePath(QString(".edithere-skill-sync-%1").arg(suffix));
        const QString backup = QDir(parentDir).filePath(QString(".edithere-skill-old-%1").arg(suffix));

        QString err;
        if (!copyDirectoryTree(snapshot.sourceSkillDir, staging, &err) ||
            !writeTextUtf8(QDir(staging).filePath("references/local-installation.md"),
                           buildLocalInstallationMarkdown(snapshot).toUtf8(), &err)) {
            QDir(staging).removeRecursively();
            res.state = QStringLiteral("failed");
            res.error = err;
            results.append(std::move(res));
            continue;
        }

        const QJsonObject meta{
            {"schemaVersion", 1},
            {"skillVersion", snapshot.bundledSkillVersion},
            {"appVersion", snapshot.appVersion},
            {"channel", QStringLiteral("desktop")},
            {"cliPath", snapshot.cliPath},
            {"unifiedCliPath", snapshot.unifiedCliPath},
            {"unifiedMcpPath", snapshot.unifiedMcpPath},
            {"installedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        };
        if (!writeTextUtf8(QDir(staging).filePath(".edithere-skill.json"),
                           QJsonDocument(meta).toJson(QJsonDocument::Indented), &err)) {
            QDir(staging).removeRecursively();
            res.state = QStringLiteral("failed");
            res.error = err;
            results.append(std::move(res));
            continue;
        }

        const bool hadExisting = QFileInfo::exists(h.skillDir);
        if (hadExisting) {
            if (!renameSkillDirectory(h.skillDir, backup)) {
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = withHostSkillSyncHint(
                    QCoreApplication::translate("h2d", "无法备份现有的 Skill 目录：%1")
                        .arg(QDir::toNativeSeparators(h.skillDir)));
                results.append(std::move(res));
                continue;
            }
            if (!renameSkillDirectory(staging, h.skillDir)) {
                renameSkillDirectory(backup, h.skillDir);
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = withHostSkillSyncHint(
                    QCoreApplication::translate("h2d", "无法替换现有的 Skill 目录：%1")
                        .arg(QDir::toNativeSeparators(h.skillDir)));
                results.append(std::move(res));
                continue;
            }
            QDir(backup).removeRecursively();
            res.state = QStringLiteral("updated");
        } else {
            if (!renameSkillDirectory(staging, h.skillDir)) {
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = withHostSkillSyncHint(
                    QCoreApplication::translate("h2d", "无法安装 Skill 到目标目录：%1")
                        .arg(QDir::toNativeSeparators(h.skillDir)));
                results.append(std::move(res));
                continue;
            }
            res.state = QStringLiteral("installed");
        }
        results.append(std::move(res));
    }

    return results;
}

QJsonObject skillSyncSnapshotToJson(const SkillSyncSnapshot &snapshot) {
    QJsonArray hostsArr;
    int detectedCount = 0;
    int outdatedCount = 0;
    int installedCount = 0;
    for (const auto &h : snapshot.hosts) {
        if (h.hostPresent || h.installed) ++detectedCount;
        if (h.installed) ++installedCount;
        if (h.status == "outdated" || h.status == "legacy" || h.status == "path-stale") ++outdatedCount;
        QJsonArray variantsArr;
        for (const auto &v : h.otherVariants) variantsArr.append(v);
        hostsArr.append(QJsonObject{
            {"id", h.id},
            {"name", h.name},
            {"hostRoot", QDir::toNativeSeparators(h.hostRoot)},
            {"skillDir", QDir::toNativeSeparators(h.skillDir)},
            {"hostPresent", h.hostPresent},
            {"installed", h.installed},
            {"installedVersion", h.installedVersion},
            {"status", h.status},
            {"recordedCliPath", h.recordedCliPath},
            {"recordedMcpPath", h.recordedMcpPath},
            {"otherVariants", variantsArr},
        });
    }
    return QJsonObject{
        {"bundledSkillVersion", snapshot.bundledSkillVersion},
        {"minCompatibleSkillVersion", snapshot.minCompatibleSkillVersion},
        {"appVersion", snapshot.appVersion},
        {"sourceAvailable", snapshot.sourceAvailable},
        {"sourceSkillDir", QDir::toNativeSeparators(snapshot.sourceSkillDir)},
        {"cliPath", snapshot.cliPath},
        {"unifiedCliPath", snapshot.unifiedCliPath},
        {"unifiedMcpPath", snapshot.unifiedMcpPath},
        {"detectedCount", detectedCount},
        {"installedCount", installedCount},
        {"outdatedCount", outdatedCount},
        {"hosts", hostsArr},
    };
}

QJsonArray skillSyncResultsToJson(const QVector<SkillSyncActionResult> &results) {
    QJsonArray arr;
    for (const auto &r : results) {
        QJsonObject obj{
            {"id", r.id},
            {"name", r.name},
            {"destination", QDir::toNativeSeparators(r.destination)},
            {"state", r.state},
            {"fromVersion", r.fromVersion},
            {"toVersion", r.toVersion},
        };
        if (!r.error.isEmpty()) obj["error"] = r.error;
        arr.append(obj);
    }
    return arr;
}

} // namespace h2d
