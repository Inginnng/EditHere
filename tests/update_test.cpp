#include "updatechecker.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
using namespace h2d;
namespace h2d {
// Exercise the real verification/confirmation/launcher boundary without network
// traffic or ever running an installer against the developer's application.
class UpdateCheckerTestAccess {
  public:
    static void install(UpdateChecker &checker, const QString &path, const QString &hash) {
        checker.installing_ = true;
        checker.verifyAndInstall(path, hash, true);
    }
};
}
class UpdateTests : public QObject {
    Q_OBJECT
    QByteArray release(const QString &version) {
        return QJsonDocument(QJsonObject{{"tag_name", version},
                                         {"draft", false},
                                         {"prerelease", false},
                                         {"html_url",
                                          "https://github.com/Inginnng/EditHere/releases/tag/" + version}})
            .toJson();
    }
    static QJsonObject asset(const QString &name) {
        return QJsonObject{{"name", name},
                           {"size", 1024},
                           {"browser_download_url",
                            "https://github.com/Inginnng/EditHere/releases/latest/download/" + name}};
    }
    // Attach the Windows packages, either with the version in the file name
    // (historical layout) or without it (current layout).
    QByteArray releaseWithAssets(const QString &version, bool versioned) {
        const QString ver = version.startsWith("v") ? version.mid(1) : version;
        const QString suffix = versioned ? "-" + ver : QString();
        const QString installer = "EditHere" + suffix + "-win-x64-setup.exe";
        const QString portable = "EditHere" + suffix + "-win-x64.zip";
        auto object = QJsonDocument::fromJson(release(version)).object();
        object["assets"] = QJsonArray{asset(installer), asset(installer + ".sha256"), asset(portable),
                                      asset(portable + ".sha256")};
        return QJsonDocument(object).toJson();
    }
  private slots:
    void cancellationAndLaunchFailureNeverAnnounceInstallation() {
        QTemporaryDir directory;
        const QString path = directory.filePath("not-an-executable.exe");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("payload");
        file.close();
        const auto hash = QString::fromLatin1(QCryptographicHash::hash("payload", QCryptographicHash::Sha256).toHex());
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        int confirmations = 0;
        checker.setInstallConfirmation([&] { ++confirmations; return false; });
        UpdateCheckerTestAccess::install(checker, path, QString());
        QCOMPARE(confirmations, 0);
        UpdateCheckerTestAccess::install(checker, path, hash);
        QCOMPARE(confirmations, 1);
        QCOMPARE(started.size(), 0);
        QVERIFY(!checker.downloading());
        // Remove the verified payload during confirmation to force a launch
        // failure without Windows trying to load a deliberately corrupt EXE.
        // Its loader/error-reporting UI can outlive the test and retain CTest's
        // output handles, making an otherwise completed run wait indefinitely.
        checker.setInstallConfirmation([&] {
            ++confirmations;
            return QFile::remove(path);
        });
        UpdateCheckerTestAccess::install(checker, path, hash);
        QCOMPARE(confirmations, 2);
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 3);
        QVERIFY(!checker.downloading());
    }
    void hashMustBePresentWellFormedAndMatch() {
        QTemporaryDir directory;
        const QString path = directory.filePath("package.exe");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("payload");
        file.close();
        const auto hash = QString::fromLatin1(QCryptographicHash::hash("payload", QCryptographicHash::Sha256).toHex());
        QString error;
        QVERIFY(UpdateChecker::verifyPackage(path, hash.toUpper(), &error));
        QVERIFY(error.isEmpty());
        for (const auto &invalid : {QString(), QString(" "), QString(64, 'z'), QString(64, '0'), hash + "\n"}) {
            QVERIFY(!UpdateChecker::verifyPackage(path, invalid, &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!UpdateChecker::verifyPackage(directory.filePath("missing"), hash, &error));
    }
    void automaticUpdatesRespectPlatformAndInstallerCapabilities() {
        auto old = UpdateChecker::parseRelease(releaseWithAssets("v0.9.8", false), "0.9.7");
        QVERIFY(UpdateChecker::canAutoInstall(old, true, true));
        QVERIFY(!UpdateChecker::canAutoInstall(old, false, true));
        auto next = UpdateChecker::parseRelease(releaseWithAssets("v0.9.9", false), "0.9.8");
        QVERIFY(UpdateChecker::canAutoInstall(next, false, true));
        QVERIFY(!UpdateChecker::canAutoInstall(next, false, false));
        QVERIFY(!UpdateChecker::canAutoInstall(next, true, false));
        next.installerHash = {};
        QVERIFY(!UpdateChecker::canAutoInstall(next, true, true));
    }
    void hashAssetMustBelongToSelectedPackage() {
        auto object = QJsonDocument::fromJson(releaseWithAssets("v0.9.9", false)).object();
        auto assets = object["assets"].toArray();
        assets.append(asset("EditHere-0.9.9-win-x64-setup.exe"));
        object["assets"] = assets;
        const auto result = UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "0.9.8");
        QCOMPARE(result.installer.name, QString("EditHere-0.9.9-win-x64-setup.exe"));
        QVERIFY(result.installerHash.url.isEmpty());
        QVERIFY(!UpdateChecker::canAutoInstall(result, true, true));
    }
    void httpsBackendAvailable() {
        QVERIFY2(QSslSocket::supportsSsl(), "HTTPS support must be included in the portable app");
    }
    void numericOrdering_data() {
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("local");
        QTest::addColumn<int>("status");
        QTest::newRow("patch-ten") << "v0.8.10" << "0.8.9" << int(UpdateChecker::Available);
        QTest::newRow("same") << "v0.8.3" << "0.8.3" << int(UpdateChecker::Current);
        QTest::newRow("development") << "v0.8.2" << "0.8.3" << int(UpdateChecker::NewerLocal);
        QTest::newRow("minor") << "0.9.0" << "0.8.99" << int(UpdateChecker::Available);
        QTest::newRow("major") << "v1.0.0" << "0.99.99" << int(UpdateChecker::Available);
    }
    void numericOrdering() {
        QFETCH(QString, remote);
        QFETCH(QString, local);
        QFETCH(int, status);
        const auto result = UpdateChecker::parseRelease(release(remote), local);
        QCOMPARE(int(result.status), status);
        QVERIFY(result.url.path().endsWith(remote));
    }
    void rejectsUntrustedAndNonStableReleases() {
        auto base = QJsonDocument::fromJson(release("v0.8.3")).object();
        for (const auto &url : {"http://github.com/Inginnng/EditHere/releases/tag/v0.8.3",
                                "https://example.com/Inginnng/EditHere/releases/tag/v0.8.3",
                                "https://github.com/elsewhere/other/releases/tag/v0.8.3",
                                "https://github.com/Inginnng/EditHere/releases/tag/v0.8.3?x=1",
                                "https://user@github.com/Inginnng/EditHere/releases/tag/v0.8.3"}) {
            auto changed = base;
            changed["html_url"] = url;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
        }
        for (const auto &key : {"draft", "prerelease"}) {
            auto changed = base;
            changed[key] = true;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
            changed.remove(key);
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.8.2").status,
                     UpdateChecker::Failed);
        }
        for (const auto &version : {"v0.8.3-beta.1", "0.8", "unknown", "999999999999999999999.8.3"})
            QCOMPARE(UpdateChecker::parseRelease(release(version), "0.8.2").status, UpdateChecker::Failed);
        QCOMPARE(UpdateChecker::parseRelease(release("v0.8.3"), "invalid").status, UpdateChecker::Failed);
    }
    void resolvesPackagesWithAndWithoutVersionInName() {
        for (const bool versioned : {true, false}) {
            const auto result = UpdateChecker::parseRelease(releaseWithAssets("v0.9.5", versioned), "0.9.4");
            QCOMPARE(result.status, UpdateChecker::Available);
            const QString expected =
                versioned ? "EditHere-0.9.5-win-x64-setup.exe" : "EditHere-win-x64-setup.exe";
            QCOMPARE(result.installer.name, expected);
            QCOMPARE(result.installerHash.name, expected + ".sha256");
            QVERIFY(!result.installer.url.isEmpty());
            QVERIFY(!result.installerHash.url.isEmpty());
            QCOMPARE(result.portable.name,
                     versioned ? "EditHere-0.9.5-win-x64.zip" : "EditHere-win-x64.zip");
            QVERIFY(!result.portable.url.isEmpty());
            QVERIFY(!result.portableHash.url.isEmpty());
        }
        // A release without the Windows packages must not claim an installable update.
        const auto bare = UpdateChecker::parseRelease(release("v0.9.5"), "0.9.4");
        QCOMPARE(bare.status, UpdateChecker::Available);
        QVERIFY(bare.installer.name.isEmpty());
        QVERIFY(bare.portable.url.isEmpty());
    }
    void staleDownloadIsDroppedBeforeAppending() {
        // Two attempts at the same version used to share one append-only temporary
        // file: the second download landed behind the first package and the check
        // reported "校验失败：安装包已损坏或不完整" for a package that was intact.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString name = "EditHere-win-x64.zip";
        const QString path = UpdateChecker::prepareDownloadTarget(directory.path(), name);
        QCOMPARE(path, QDir(directory.path()).filePath("EditHere-update-" + name));
        QVERIFY(path.startsWith(directory.path()));
        QVERIFY(!QFile::exists(path));

        QFile leftover(path);
        QVERIFY(leftover.open(QIODevice::WriteOnly));
        QCOMPARE(leftover.write(QByteArray(4096, 'x')), qint64(4096));
        leftover.close();

        // Handing out the same target again must leave nothing behind, so the
        // append writer starts from an empty file.
        QCOMPARE(UpdateChecker::prepareDownloadTarget(directory.path(), name), path);
        QVERIFY(!QFile::exists(path));
        QFile fresh(path);
        QVERIFY(fresh.open(QIODevice::WriteOnly | QIODevice::Append));
        QCOMPARE(fresh.size(), qint64(0));
        QCOMPARE(fresh.write(QByteArray(8, 'y')), qint64(8));
        fresh.close();
        QCOMPARE(QFileInfo(path).size(), qint64(8));

        // An unrelated file next to the target is left alone, and the target for a
        // different package name is derived from that name.
        QFile unrelated(directory.filePath("EditHere-update-other.zip"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        QCOMPARE(unrelated.write("keep"), qint64(4));
        unrelated.close();
        QCOMPARE(UpdateChecker::prepareDownloadTarget(directory.path(), "EditHere-0.9.9-win-x64.zip"),
                 directory.filePath("EditHere-update-EditHere-0.9.9-win-x64.zip"));
        QCOMPARE(QFileInfo(unrelated.fileName()).size(), qint64(4));
    }
    void malformedOrInaccessibleNeverMeansCurrent() {
        for (const auto &bytes : {QByteArray("{\"message\":\"Not Found\"}"), QByteArray("<html>error</html>"),
                                  QByteArray("[]"), QByteArray(), QByteArray(1024 * 1024 + 1, ' ')}) {
            const auto result = UpdateChecker::parseRelease(bytes, "0.8.3");
            QCOMPARE(result.status, UpdateChecker::Failed);
            QCOMPARE(result.url, UpdateChecker::releasesUrl());
        }
        UpdateChecker checker;
        QVERIFY(!checker.busy()); // Construction is offline; only explicit check starts a request.
    }
    void liveReleaseCheck() {
        if (!qEnvironmentVariableIsSet("H2D_LIVE_UPDATES"))
            QSKIP("Opt-in real GitHub release integration check");
        UpdateChecker checker;
        QSignalSpy result(&checker, &UpdateChecker::finished);
        checker.check();
        checker.check(); // Coalesce concurrent checks.
        QVERIFY(result.wait(20000));
        QCOMPARE(result.size(), 1);
        QVERIFY(!checker.busy());
        qInfo().noquote() << result.first()[1].toString();
        QVERIFY(result.first()[0].value<UpdateChecker::Status>() != UpdateChecker::Failed);
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "update_test.moc"
