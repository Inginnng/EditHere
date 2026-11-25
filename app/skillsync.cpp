#include "skillsync.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

#include <array>

namespace h2d {
namespace {

struct HostDef {
    const char *id;
    const char *name;
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

QString resolveRelativeTool(const QString &baseAppDir, const QString &relative) {
    const QString direct = normPath(QDir(baseAppDir).filePath(relative));
    if (QFileInfo::exists(direct)) {
        const QString canonical = QFileInfo(direct).canonicalFilePath();
        return canonical.isEmpty() ? direct : canonical;
    }
    const QString devFallback = normPath(QDir(baseAppDir).filePath("../" + relative));
    if (QFileInfo::exists(devFallback)) {
        const QString canonical = QFileInfo(devFallback).canonicalFilePath();
        return canonical.isEmpty() ? devFallback : canonical;
    }
    return {};
}

QVector<HostDef> hostDefinitions(const QString &homeDir, const QString &codexHome) {
    const QString home = homeDir.isEmpty() ? normPath(QDir::homePath()) : normPath(homeDir);
    QString codexRoot = normPath(codexHome);
    if (codexRoot.isEmpty()) {
        if (homeDir.isEmpty() && qEnvironmentVariableIsSet("CODEX_HOME")) {
            const QString envVal = normPath(qEnvironmentVariable("CODEX_HOME"));
            if (!envVal.isEmpty()) codexRoot = envVal;
        }
        if (codexRoot.isEmpty()) {
            codexRoot = normPath(QDir(home).filePath(".codex"));
        }
    }
    return {
        {"codex", "Codex", codexRoot},
        {"agents", "Codex Agents (~/.agents)", normPath(QDir(home).filePath(".agents"))},
        {"claude", "Claude Code", normPath(QDir(home).filePath(".claude"))},
        {"cursor", "Cursor", normPath(QDir(home).filePath(".cursor"))},
        {"trae", "Trae", normPath(QDir(home).filePath(".trae"))},
        {"antigravity", "Antigravity", normPath(QDir(home).filePath(".gemini/antigravity"))},
    };
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

bool copyDirectoryTree(const QString &srcDir, const QString &dstDir, QString *error) {
    QDir rootSrc(srcDir);
    if (!rootSrc.exists()) {
        if (error) *error = QString("Source skill directory does not exist: %1").arg(srcDir);
        return false;
    }
    if (!QDir().mkpath(dstDir)) {
        if (error) *error = QString("Cannot create staging directory: %1").arg(dstDir);
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
                if (error) *error = QString("Cannot create directory: %1").arg(targetPath);
                return false;
            }
        } else {
            const QString parent = QFileInfo(targetPath).absolutePath();
            if (!QDir().mkpath(parent)) {
                if (error) *error = QString("Cannot create parent directory: %1").arg(parent);
                return false;
            }
            if (QFileInfo::exists(targetPath)) QFile::remove(targetPath);
            if (!QFile::copy(fi.filePath(), targetPath)) {
                if (error) *error = QString("Cannot copy skill file: %1").arg(rel);
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
        if (error) *error = QString("Cannot create directory: %1").arg(parent);
        return false;
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString("Cannot write file %1: %2").arg(filePath, f.errorString());
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = QString("Short write on %1").arg(filePath);
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
    if (base.isEmpty()) return {};
    for (const QString &rel : {QStringLiteral("skills/edithere"), QStringLiteral("../skills/edithere")}) {
        const QString candidate = normPath(QDir(base).filePath(rel));
        if (QFileInfo::exists(QDir(candidate).filePath("SKILL.md"))) {
            const QString canonical = QFileInfo(candidate).canonicalFilePath();
            return canonical.isEmpty() ? candidate : canonical;
        }
    }
    return {};
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
    snapshot.cliPath = resolveRelativeTool(base, QStringLiteral("edithere-cli.exe"));
#else
    snapshot.cliPath = resolveRelativeTool(base, QStringLiteral("edithere-cli"));
#endif
    snapshot.unifiedCliPath = resolveRelativeTool(base, QStringLiteral("cli/edithere.mjs"));
    snapshot.unifiedMcpPath = resolveRelativeTool(base, QStringLiteral("cli/unified-mcp.mjs"));

    const auto defs = hostDefinitions(homeDir, codexHome);
    snapshot.hosts.reserve(defs.size());
    for (const auto &def : defs) {
        SkillHostStatus st;
        st.id = QString::fromLatin1(def.id);
        st.name = QString::fromUtf8(def.name);
        st.hostRoot = def.hostRoot;
        st.skillDir = normPath(QDir(def.hostRoot).filePath("skills/edithere"));
        st.hostPresent = QFileInfo::exists(def.hostRoot) && QFileInfo(def.hostRoot).isDir();
        st.installed = QFileInfo::exists(QDir(st.skillDir).filePath("SKILL.md"));

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
            res.error = QStringLiteral("Bundled skills/edithere directory was not found.");
            results.append(std::move(res));
            continue;
        }

        const QString parentDir = QFileInfo(h.skillDir).absolutePath();
        if (!QDir().mkpath(parentDir)) {
            res.state = QStringLiteral("failed");
            res.error = QString("Cannot create skills parent directory: %1").arg(parentDir);
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
            if (!QDir().rename(h.skillDir, backup)) {
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = QString("Cannot move existing skill directory aside: %1").arg(h.skillDir);
                results.append(std::move(res));
                continue;
            }
            if (!QDir().rename(staging, h.skillDir)) {
                QDir().rename(backup, h.skillDir);
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = QString("Cannot place updated skill directory: %1").arg(h.skillDir);
                results.append(std::move(res));
                continue;
            }
            QDir(backup).removeRecursively();
            res.state = QStringLiteral("updated");
        } else {
            if (!QDir().rename(staging, h.skillDir)) {
                QDir(staging).removeRecursively();
                res.state = QStringLiteral("failed");
                res.error = QString("Cannot place installed skill directory: %1").arg(h.skillDir);
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
    int outdatedCount = 0;
    int installedCount = 0;
    for (const auto &h : snapshot.hosts) {
        if (h.installed) ++installedCount;
        if (h.status == "outdated" || h.status == "legacy" || h.status == "path-stale") ++outdatedCount;
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
