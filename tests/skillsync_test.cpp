#include "skillsync.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace h2d;

class SkillSyncTests : public QObject {
    Q_OBJECT
  private slots:
    void semverComparison();
    void inspectAllSixStatesAndPreventDowngrade();
    void syncUpdatesLegacyOutdatedAndPathStaleAtomically();
    void expandedCatalogFallbackRootsAndVariantsSafe();
};

namespace {
bool writeFile(const QString &path, const QByteArray &content) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(content) == content.size();
}
} // namespace

void SkillSyncTests::semverComparison() {
    QCOMPARE(compareSkillSemver("1.0.0", "1.0.0"), 0);
    QCOMPARE(compareSkillSemver("1.0.0", "1.1.0"), -1);
    QCOMPARE(compareSkillSemver("1.2.0", "1.1.9"), 1);
    QCOMPARE(compareSkillSemver("2.0.0", "1.99.99"), 1);
    QCOMPARE(compareSkillSemver("invalid", "0.0.0"), 0);
}

void SkillSyncTests::inspectAllSixStatesAndPreventDowngrade() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    const QString appDir = root + "/app";
    const QString homeDir = root + "/home";
    const QString skillSrc = appDir + "/skills/edithere";

    QVERIFY(writeFile(skillSrc + "/SKILL.md", "---\nname: edithere\n---\n"));
    QVERIFY(writeFile(skillSrc + "/references/capture.md", "capture\n"));
    QVERIFY(writeFile(skillSrc + "/skill-version.json",
                      R"({"skillVersion":"1.2.0","minCompatibleSkillVersion":"1.0.0"})"));
#ifdef Q_OS_WIN
    QVERIFY(writeFile(appDir + "/edithere-cli.exe", "cli"));
#else
    QVERIFY(writeFile(appDir + "/edithere-cli", "cli"));
#endif
    QVERIFY(writeFile(appDir + "/cli/edithere.mjs", "// cli\n"));
    QVERIFY(writeFile(appDir + "/cli/unified-mcp.mjs", "// mcp\n"));

    // 1. codex -> legacy (SKILL.md without .edithere-skill.json)
    QVERIFY(writeFile(homeDir + "/.codex/skills/edithere/SKILL.md", "old\n"));

    // 2. claude -> outdated (1.1.0 < 1.2.0)
    QVERIFY(writeFile(homeDir + "/.claude/skills/edithere/SKILL.md", "old\n"));
    QVERIFY(writeFile(homeDir + "/.claude/skills/edithere/.edithere-skill.json",
                      R"({"schemaVersion":1,"skillVersion":"1.1.0"})"));

    // 3. cursor -> newer (1.3.0 > 1.2.0)
    QVERIFY(writeFile(homeDir + "/.cursor/skills/edithere/SKILL.md", "newer\n"));
    QVERIFY(writeFile(homeDir + "/.cursor/skills/edithere/.edithere-skill.json",
                      R"({"schemaVersion":1,"skillVersion":"1.3.0"})"));

    // 4. trae -> up-to-date (same skillVersion + current CLI/MCP path, even if older appVersion)
    const QString curCli =
#ifdef Q_OS_WIN
        appDir + "/edithere-cli.exe";
#else
        appDir + "/edithere-cli";
#endif
    const QString curMcp = appDir + "/cli/unified-mcp.mjs";
    QVERIFY(writeFile(homeDir + "/.trae/skills/edithere/SKILL.md", "cur\n"));
    QVERIFY(writeFile(
        homeDir + "/.trae/skills/edithere/.edithere-skill.json",
        QJsonDocument(QJsonObject{
                          {"schemaVersion", 1},
                          {"skillVersion", "1.2.0"},
                          {"appVersion", "0.14.0"},
                          {"cliPath", curCli},
                          {"unifiedMcpPath", curMcp},
                      })
            .toJson()));

    // 5. agents -> path-stale (same skillVersion 1.2.0, stale cliPath)
    QVERIFY(writeFile(homeDir + "/.agents/skills/edithere/SKILL.md", "stale\n"));
    QVERIFY(writeFile(
        homeDir + "/.agents/skills/edithere/.edithere-skill.json",
        QJsonDocument(QJsonObject{
                          {"schemaVersion", 1},
                          {"skillVersion", "1.2.0"},
                          {"cliPath", root + "/missing-old-dir/edithere-cli.exe"},
                          {"unifiedMcpPath", curMcp},
                      })
            .toJson()));

    // 6. antigravity -> not-installed

    const SkillSyncSnapshot snap = inspectHostSkills(homeDir, {}, appDir);
    QCOMPARE(snap.bundledSkillVersion, QString("1.2.0"));
    QVERIFY(snap.sourceAvailable);

    auto statusFor = [&](const QString &id) {
        for (const auto &h : snap.hosts) {
            if (h.id == id) return h.status;
        }
        return QString();
    };
    QCOMPARE(statusFor("codex"), QString("legacy"));
    QCOMPARE(statusFor("claude"), QString("outdated"));
    QCOMPARE(statusFor("cursor"), QString("newer"));
    QCOMPARE(statusFor("trae"), QString("up-to-date"));
    QCOMPARE(statusFor("agents"), QString("path-stale"));
    QCOMPARE(statusFor("antigravity"), QString("not-installed"));
}

void SkillSyncTests::syncUpdatesLegacyOutdatedAndPathStaleAtomically() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    const QString appDir = root + "/app";
    const QString homeDir = root + "/home";
    const QString skillSrc = appDir + "/skills/edithere";

    QVERIFY(writeFile(skillSrc + "/SKILL.md", "bundled v1.2.0\n"));
    QVERIFY(writeFile(skillSrc + "/skill-version.json",
                      R"({"skillVersion":"1.2.0","minCompatibleSkillVersion":"1.0.0"})"));
#ifdef Q_OS_WIN
    const QString curCli = appDir + "/edithere-cli.exe";
#else
    const QString curCli = appDir + "/edithere-cli";
#endif
    QVERIFY(writeFile(curCli, "cli"));
    QVERIFY(writeFile(appDir + "/cli/edithere.mjs", "// cli\n"));
    QVERIFY(writeFile(appDir + "/cli/unified-mcp.mjs", "// mcp\n"));

    QVERIFY(writeFile(homeDir + "/.codex/skills/edithere/SKILL.md", "legacy\n"));
    QVERIFY(writeFile(homeDir + "/.codex/skills/edithere/stale-old-file.txt", "remove me\n"));
    QVERIFY(writeFile(homeDir + "/.cursor/skills/edithere/SKILL.md", "newer\n"));
    QVERIFY(writeFile(homeDir + "/.cursor/skills/edithere/.edithere-skill.json",
                      R"({"schemaVersion":1,"skillVersion":"2.0.0"})"));
    QDir().mkpath(homeDir + "/.claude");

    // Batch update installed only: codex upgraded, cursor untouched, claude untouched.
    const auto results = syncHostSkills({}, true, false, homeDir, {}, appDir);
    auto stateFor = [&](const QString &id) {
        for (const auto &r : results) {
            if (r.id == id) return r.state;
        }
        return QString();
    };
    QCOMPARE(stateFor("codex"), QString("updated"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.codex/skills/edithere/stale-old-file.txt"));
    QVERIFY(QFileInfo::exists(homeDir + "/.codex/skills/edithere/.edithere-skill.json"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.claude/skills/edithere/SKILL.md"));

    const auto afterSnap = inspectHostSkills(homeDir, {}, appDir);
    for (const auto &h : afterSnap.hosts) {
        if (h.id == "codex") {
            QCOMPARE(h.status, QString("up-to-date"));
            QCOMPARE(h.installedVersion, QString("1.2.0"));
        } else if (h.id == "cursor") {
            QCOMPARE(h.status, QString("newer"));
            QCOMPARE(h.installedVersion, QString("2.0.0"));
        }
    }

    // Subsequent update does nothing on up-to-date codex or newer cursor.
    const auto noopRes = syncHostSkills({"codex", "cursor"}, true, false, homeDir, {}, appDir);
    QCOMPARE(noopRes[0].state, QString("up-to-date"));
    QCOMPARE(noopRes[1].state, QString("skipped-newer"));

    // Explicit force switches even newer cursor.
    const auto forcedRes = syncHostSkills({"cursor"}, false, true, homeDir, {}, appDir);
    QCOMPARE(forcedRes[0].state, QString("updated"));
    bool foundCursorUpToDate = false;
    for (const auto &h : inspectHostSkills(homeDir, {}, appDir).hosts) {
        if (h.id == "cursor") {
            QCOMPARE(h.status, QString("up-to-date"));
            foundCursorUpToDate = true;
        }
    }
    QVERIFY(foundCursorUpToDate);

    // Explicit install creates claude.
    const auto installRes = syncHostSkills({"claude"}, false, false, homeDir, {}, appDir);
    QCOMPARE(installRes[0].state, QString("installed"));
    QVERIFY(QFileInfo::exists(homeDir + "/.claude/skills/edithere/SKILL.md"));
}

void SkillSyncTests::expandedCatalogFallbackRootsAndVariantsSafe() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    const QString appDir = root + "/app";
    const QString homeDir = root + "/home";
    const QString skillSrc = appDir + "/skills/edithere";

    QVERIFY(writeFile(skillSrc + "/SKILL.md", "bundled v1.2.0\n"));
    QVERIFY(writeFile(skillSrc + "/skill-version.json",
                      R"({"skillVersion":"1.2.0","minCompatibleSkillVersion":"1.0.0"})"));
#ifdef Q_OS_WIN
    QVERIFY(writeFile(appDir + "/edithere-cli.exe", "cli"));
#else
    QVERIFY(writeFile(appDir + "/edithere-cli", "cli"));
#endif
    QVERIFY(writeFile(appDir + "/cli/edithere.mjs", "// cli\n"));
    QVERIFY(writeFile(appDir + "/cli/unified-mcp.mjs", "// mcp\n"));

    // Detectable ecosystems: WorkBuddy (with edithere + edithere-web), DSH (edithere-dsh only), Qoder, Grok, ZCode, Trae CN, CodeBuddy, OpenCode, Kimi fallback
    QVERIFY(writeFile(homeDir + "/.workbuddy/skills/edithere/SKILL.md", "old wb\n"));
    QVERIFY(writeFile(homeDir + "/.workbuddy/skills/edithere-web/SKILL.md", "web variant\n"));
    QVERIFY(writeFile(homeDir + "/.dsh/skills/edithere-dsh/SKILL.md", "dsh variant\n"));
    QVERIFY(QDir().mkpath(homeDir + "/.qoder"));
    QVERIFY(QDir().mkpath(homeDir + "/.grok"));
    QVERIFY(QDir().mkpath(homeDir + "/.zcode"));
    QVERIFY(QDir().mkpath(homeDir + "/.trae-cn"));
    QVERIFY(QDir().mkpath(homeDir + "/.codebuddy"));
    QVERIFY(QDir().mkpath(homeDir + "/.config/opencode"));
    QVERIFY(writeFile(homeDir + "/.kimi/skills/edithere/SKILL.md", "kimi fallback old\n"));
    QVERIFY(QDir().mkpath(homeDir + "/.zed"));

    // A stray regular file named ".windsurf" must NOT be falsely treated as a host directory.
    QVERIFY(writeFile(homeDir + "/.windsurf", "not-a-dir"));

    const SkillSyncSnapshot snap = inspectHostSkills(homeDir, {}, appDir);
    auto hostById = [&](const QString &id) -> SkillHostStatus {
        for (const auto &h : snap.hosts) {
            if (h.id == id) return h;
        }
        return {};
    };
    QCOMPARE(hostById("workbuddy").hostPresent, true);
    QCOMPARE(hostById("workbuddy").status, QString("legacy"));
    QVERIFY(hostById("workbuddy").otherVariants.contains("edithere-web"));
    QCOMPARE(hostById("dsh").hostPresent, true);
    QCOMPARE(hostById("dsh").status, QString("not-installed"));
    QVERIFY(hostById("dsh").otherVariants.contains("edithere-dsh"));
    QCOMPARE(hostById("qoder").hostPresent, true);
    QCOMPARE(hostById("grok").hostPresent, true);
    QCOMPARE(hostById("zcode").hostPresent, true);
    QCOMPARE(hostById("trae-cn").hostPresent, true);
    QCOMPARE(hostById("codebuddy").hostPresent, true);
    QCOMPARE(hostById("opencode").hostPresent, true);
    QCOMPARE(hostById("kimi").hostPresent, true);
    QCOMPARE(hostById("kimi").status, QString("legacy"));
    QCOMPARE(hostById("zed").hostPresent, true);
    QCOMPARE(hostById("windsurf").hostPresent, false);

    // Auto/bulk sync with onlyUpdateInstalled=true MUST update only workbuddy & kimi, preserve variants, and NOT install into qoder/dsh/grok/zcode.
    const auto syncRes = syncHostSkills({}, true, false, homeDir, {}, appDir);
    QCOMPARE(syncRes.size(), 2);
    QVERIFY(QFileInfo::exists(homeDir + "/.workbuddy/skills/edithere-web/SKILL.md"));
    QVERIFY(QFileInfo::exists(homeDir + "/.dsh/skills/edithere-dsh/SKILL.md"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.dsh/skills/edithere/SKILL.md"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.qoder/skills/edithere/SKILL.md"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.grok/skills/edithere/SKILL.md"));
    QVERIFY(!QFileInfo::exists(homeDir + "/.zcode/skills/edithere/SKILL.md"));
}

QTEST_MAIN(SkillSyncTests)
#include "skillsync_test.moc"
