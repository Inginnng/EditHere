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
#include <utility>
using namespace h2d;
namespace h2d {
// Exercise the real verification/confirmation/launcher boundary without network
// traffic or ever running an installer against the developer's application.
class UpdateCheckerTestAccess {
  public:
    static QString prepareInstall(UpdateChecker &checker, const QString &directory,
                                  std::function<bool(QProcess &)> launcher) {
        checker.downloadDirectory_ = std::make_unique<QTemporaryDir>(QDir(directory).filePath("EditHere-update-XXXXXX"));
        checker.expectedInstallVersion_ = "0.11.0";
        checker.launchInstaller_ = std::move(launcher);
        const auto path = checker.downloadDirectory_->filePath("package.exe");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("payload") != 7)
            return {};
        return path;
    }
    static QJsonObject request(const QString &package) {
        QFile file(QDir(QFileInfo(package).absolutePath()).filePath("request.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    static bool status(UpdateChecker &checker, const QString &package, QString state,
                       const QString &changedKey = {}, const QString &changedValue = {}) {
        auto object = request(package);
        object["status"] = state;
        object["message"] = "Maintenance preparation failed: target is not writable.";
        if (!changedKey.isEmpty()) object[changedKey] = changedValue;
        QSaveFile file(QDir(QFileInfo(package).absolutePath()).filePath("status.json"));
        const auto bytes = QJsonDocument(object).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return false;
        checker.pollHandoff();
        return true;
    }
    static QJsonObject action(const QString &package) {
        QFile file(QDir(QFileInfo(package).absolutePath()).filePath("ack.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    static void expire(UpdateChecker &checker) {
        checker.handoffTimeoutMs_ = 0;
        checker.pollHandoff();
    }
    static void install(UpdateChecker &checker, const QString &path, const QString &hash) {
        checker.installing_ = true;
        checker.verifyAndInstall(path, hash, true);
    }
    static QString beginDownload(UpdateChecker &checker, const QString &directory) {
        checker.installing_ = true;
        checker.downloadDirectory_ = std::make_unique<QTemporaryDir>(QDir(directory).filePath("download-XXXXXX"));
        checker.downloadPath_ = checker.downloadDirectory_->filePath("package.exe");
        checker.downloadFile_ = std::make_unique<QSaveFile>(checker.downloadPath_);
        if (!checker.downloadFile_->open(QIODevice::WriteOnly))
            return {};
        checker.downloadFile_->write("partial package");
        return checker.downloadDirectory_->path();
    }
    static void beginCheck(UpdateChecker &checker) {
        checker.githubResult_.reset();
        checker.giteeResult_.reset();
        checker.busy_ = true;
        checker.timeout_.start();
    }
    static void collect(UpdateChecker &checker, UpdateChecker::Source source, UpdateChecker::Result result) {
        checker.collectSource(source, std::move(result));
    }
    static void expireCheck(UpdateChecker &checker) {
        checker.completeCheck();
    }
    static const std::optional<UpdateChecker::Result> &sourceResult(const UpdateChecker &checker,
                                                                   UpdateChecker::Source source) {
        return source == UpdateChecker::GitHub ? checker.githubResult_ : checker.giteeResult_;
    }
};
}
class UpdateTests : public QObject {
    Q_OBJECT
    static QString payloadHash() {
        return QString::fromLatin1(QCryptographicHash::hash("payload", QCryptographicHash::Sha256).toHex());
    }
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
    static QJsonObject giteeAsset(const QString &name, const QString &version) {
        return QJsonObject{{"name", name},
                           {"browser_download_url",
                            "https://gitee.com/InnGing/EditHere/releases/download/" + version + "/" + name}};
    }
    // Gitee's public latest endpoint omits draft, html_url and asset size.
    QByteArray giteeRelease(const QString &version, bool withAssets = false, bool versioned = false) {
        QJsonObject object{{"tag_name", version}, {"prerelease", false}};
        if (withAssets) {
            const QString ver = version.startsWith("v") ? version.mid(1) : version;
            const QString suffix = versioned ? "-" + ver : QString();
            const QString installer = "EditHere" + suffix + "-win-x64-setup.exe";
            const QString portable = "EditHere" + suffix + "-win-x64.zip";
            object["assets"] = QJsonArray{giteeAsset(installer, version), giteeAsset(installer + ".sha256", version),
                                          giteeAsset(portable, version), giteeAsset(portable + ".sha256", version)};
        }
        return QJsonDocument(object).toJson();
    }
  private slots:
    void cancellingDownloadRemovesPartialFilesWithoutConfirmingOrLaunching() {
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        int confirmations = 0;
        checker.setInstallConfirmation([&] { ++confirmations; return true; });
        const auto download = UpdateCheckerTestAccess::beginDownload(checker, directory.path());
        QVERIFY(!download.isEmpty());
        QVERIFY(QFileInfo::exists(download));
        QVERIFY(checker.downloading());
        checker.cancelDownload();
        QVERIFY(!checker.downloading());
        QVERIFY(!QFileInfo::exists(download));
        QCOMPARE(confirmations, 0);
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 1);
        checker.cancelDownload();
        QCOMPARE(failed.size(), 1);
    }
    void cancellationAndLaunchFailureNeverAnnounceInstallation() {
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        int confirmations = 0;
        checker.setInstallConfirmation([&] { ++confirmations; return false; });
        int launches = 0;
        auto prepare = [&] {
            return UpdateCheckerTestAccess::prepareInstall(checker, directory.path(), [&](QProcess &) {
                ++launches;
                return false;
            });
        };
        auto path = prepare();
        QVERIFY(!path.isEmpty());
        UpdateCheckerTestAccess::install(checker, path, QString());
        QCOMPARE(confirmations, 0);
        path = prepare();
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QCOMPARE(confirmations, 1);
        QCOMPARE(started.size(), 0);
        QVERIFY(!checker.downloading());
        path = prepare();
        checker.setInstallConfirmation([&] { ++confirmations; return true; });
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QCOMPARE(confirmations, 2);
#ifdef Q_OS_WIN
        QCOMPARE(launches, 1);
#else
        QCOMPARE(launches, 0);
#endif
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 3);
        QVERIFY(!checker.downloading());
    }
    void detachedCreationWaitsForAuthenticatedReadyBeforeAnnouncingInstallation() {
#ifndef Q_OS_WIN
        QSKIP("Windows maintenance handoff");
#endif
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy preparing(&checker, &UpdateChecker::installerPreparing);
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        QStringList arguments;
        QString nativeArguments;
        const auto path = UpdateCheckerTestAccess::prepareInstall(checker, directory.path(), [&](QProcess &process) {
            arguments = process.arguments();
#ifdef Q_OS_WIN
            nativeArguments = process.nativeArguments();
#endif
            return true;
        });
        QVERIFY(!path.isEmpty());
        checker.setInstallConfirmation([] { return true; });
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QCOMPARE(preparing.size(), 1);
        QCOMPARE(started.size(), 0);
        QVERIFY(checker.downloading());
        const auto request = UpdateCheckerTestAccess::request(path);
        QVERIFY(!request.value("token").toString().isEmpty());
        QCOMPARE(request.value("version").toString(), QString("0.11.0"));
        QCOMPARE(request.value("sha256").toString(), payloadHash());
        QCOMPARE(request.value("processId").toInteger(), QCoreApplication::applicationPid());
        QVERIFY(arguments.contains("/HANDOFF=" + request.value("id").toString()));
        QCOMPARE(nativeArguments, "/D=" + QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
        QVERIFY(UpdateCheckerTestAccess::action(path).isEmpty());
        QVERIFY(UpdateCheckerTestAccess::status(checker, path, "ready"));
        QCOMPARE(started.size(), 1);
        QVERIFY(!checker.downloading());
        const auto ack = UpdateCheckerTestAccess::action(path);
        QCOMPARE(ack.value("action").toString(), QString("ack"));
        QCOMPARE(ack.value("token"), request.value("token"));
        QVERIFY(UpdateCheckerTestAccess::status(checker, path, "ready"));
        QCOMPARE(started.size(), 1);
        QCOMPARE(failed.size(), 0);
    }
    void failedPreparationKeepsApplicationRunningAndReclaimsDownload() {
#ifndef Q_OS_WIN
        QSKIP("Windows maintenance handoff");
#endif
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        const auto path = UpdateCheckerTestAccess::prepareInstall(checker, directory.path(), [](QProcess &) { return true; });
        checker.setInstallConfirmation([] { return true; });
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QVERIFY(UpdateCheckerTestAccess::status(checker, path, "failed"));
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QString("Maintenance preparation failed: target is not writable."));
        QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
        QVERIFY(!checker.downloading());
    }
    void unwritableAcknowledgementNeverAnnouncesInstallation() {
#ifndef Q_OS_WIN
        QSKIP("Windows maintenance handoff");
#endif
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        const auto path = UpdateCheckerTestAccess::prepareInstall(checker, directory.path(), [](QProcess &) { return true; });
        checker.setInstallConfirmation([] { return true; });
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QVERIFY(QDir().mkpath(QDir(QFileInfo(path).absolutePath()).filePath("ack.json")));
        QVERIFY(UpdateCheckerTestAccess::status(checker, path, "ready"));
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 1);
        QVERIFY(UpdateCheckerTestAccess::action(path).isEmpty());
        QVERIFY(!checker.downloading());
    }
    void mismatchedReadyNeverAcknowledgesOrExits_data() {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::newRow("token") << "token" << "other-request";
        QTest::newRow("target") << "target" << "C:/unrelated-installation";
        QTest::newRow("version") << "version" << "9.0.0";
        QTest::newRow("state") << "status" << "committed";
    }
    void mismatchedReadyNeverAcknowledgesOrExits() {
#ifndef Q_OS_WIN
        QSKIP("Windows maintenance handoff");
#endif
        QFETCH(QString, key);
        QFETCH(QString, value);
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy started(&checker, &UpdateChecker::installStarted);
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        const auto path = UpdateCheckerTestAccess::prepareInstall(checker, directory.path(), [](QProcess &) { return true; });
        checker.setInstallConfirmation([] { return true; });
        UpdateCheckerTestAccess::install(checker, path, payloadHash());
        QVERIFY(UpdateCheckerTestAccess::status(checker, path, "ready", key, value));
        QCOMPARE(started.size(), 0);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(UpdateCheckerTestAccess::action(path).value("action").toString(), QString("cancel"));
        QVERIFY(!checker.downloading());
    }
    void pendingHandoffCancelsOnUserCancellationTimeoutOrDestruction_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("cancel") << "cancel";
        QTest::newRow("timeout") << "timeout";
        QTest::newRow("close-dialog") << "destroy";
    }
    void pendingHandoffCancelsOnUserCancellationTimeoutOrDestruction() {
#ifndef Q_OS_WIN
        QSKIP("Windows maintenance handoff");
#endif
        QFETCH(QString, action);
        QTemporaryDir directory;
        auto checker = std::make_unique<UpdateChecker>();
        QSignalSpy started(checker.get(), &UpdateChecker::installStarted);
        const auto path = UpdateCheckerTestAccess::prepareInstall(*checker, directory.path(), [](QProcess &) { return true; });
        checker->setInstallConfirmation([] { return true; });
        UpdateCheckerTestAccess::install(*checker, path, payloadHash());
        if (action == "cancel") checker->cancelDownload();
        if (action == "timeout") UpdateCheckerTestAccess::expire(*checker);
        if (action == "destroy") checker.reset();
        QCOMPARE(UpdateCheckerTestAccess::action(path).value("action").toString(), QString("cancel"));
        QCOMPARE(started.size(), 0);
        // The worker must still be able to observe cancellation after the dialog
        // has gone away. It reclaims these exact files after reaching terminal state.
        QVERIFY(QFileInfo::exists(path));
        if (checker) {
            QVERIFY(UpdateCheckerTestAccess::status(*checker, path, "ready"));
            QCOMPARE(started.size(), 0);
            QVERIFY(!checker->downloading());
        }
    }
    void installationReceiptsMatchTargetAndExpectedRunningVersion() {
        const QString target = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
        QJsonObject receipt{{"id", "receipt-a"}, {"target", target}, {"version", "0.11.0"},
                             {"status", "failed"}, {"message", "Rollback completed; see the maintenance log."}};
        QCOMPARE(UpdateChecker::installationResultMessage(receipt, target, "0.10.4"), receipt.value("message").toString());
        QVERIFY(UpdateChecker::installationResultMessage(receipt, "C:/unrelated", "0.10.4").isEmpty());
        receipt["status"] = "committed";
        QVERIFY(UpdateChecker::installationResultMessage(receipt, target, "0.11.0").isEmpty());
        QVERIFY(!UpdateChecker::installationResultMessage(receipt, target, "0.10.4").isEmpty());
        receipt["status"] = "cancelled";
        QVERIFY(UpdateChecker::installationResultMessage(receipt, target, "0.10.4").isEmpty());
        receipt["status"] = "failed";
        receipt["version"] = "unknown";
        QVERIFY(UpdateChecker::installationResultMessage(receipt, target, "0.10.4").isEmpty());
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
    void parsesGiteePublicReleaseWithoutGithubOnlyFields() {
        for (const bool versioned : {false, true}) {
            const auto result = UpdateChecker::parseRelease(giteeRelease("v0.10.2", true, versioned),
                                                            "0.10.1", UpdateChecker::Gitee);
            QCOMPARE(result.status, UpdateChecker::Available);
            QCOMPARE(result.tagName, QString("v0.10.2"));
            QCOMPARE(result.url.host(), QString("gitee.com"));
            QVERIFY(result.url.path().startsWith("/InnGing/EditHere/releases/"));
            QVERIFY(result.url.path().endsWith("v0.10.2"));
            const QString installer = versioned ? "EditHere-0.10.2-win-x64-setup.exe"
                                               : "EditHere-win-x64-setup.exe";
            QCOMPARE(result.installer.name, installer);
            QCOMPARE(result.installerHash.name, installer + ".sha256");
            QCOMPARE(result.installer.url.host(), QString("gitee.com"));
            QCOMPARE(result.installer.size, qint64(0));
            QVERIFY(!result.portable.url.isEmpty());
            QVERIFY(!result.portableHash.url.isEmpty());
            QVERIFY(UpdateChecker::canAutoInstall(result, false, true));
        }
        QCOMPARE(UpdateChecker::releasesUrl(UpdateChecker::Gitee),
                 QUrl("https://gitee.com/InnGing/EditHere/releases"));
    }
    void rejectsInvalidGiteeReleaseFlagsAndUrls() {
        const auto base = QJsonDocument::fromJson(giteeRelease("v0.10.2")).object();
        for (const auto &key : {"draft", "prerelease"}) {
            for (const QJsonValue value : {QJsonValue(true), QJsonValue(QStringLiteral("false")), QJsonValue(0)}) {
                auto changed = base;
                changed[key] = value;
                QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                                      UpdateChecker::Gitee).status, UpdateChecker::Failed);
            }
        }
        auto changed = base;
        changed.remove("prerelease");
        QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                              UpdateChecker::Gitee).status, UpdateChecker::Failed);
        changed = base;
        changed["draft"] = false;
        const auto valid = UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                                        UpdateChecker::Gitee);
        QCOMPARE(valid.status, UpdateChecker::Available);
        changed["html_url"] = valid.url.toString();
        QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                              UpdateChecker::Gitee).status, UpdateChecker::Available);
        for (const auto &url : {"http://gitee.com/InnGing/EditHere/releases/tag/v0.10.2",
                                "https://gitee.com/elsewhere/other/releases/tag/v0.10.2",
                                "https://gitee.com/InnGing/EditHereElse/releases/tag/v0.10.2",
                                "https://github.com/Inginnng/EditHere/releases/tag/v0.10.2",
                                "https://user@gitee.com/InnGing/EditHere/releases/tag/v0.10.2",
                                "https://gitee.com:443/InnGing/EditHere/releases/tag/v0.10.2"}) {
            changed = base;
            changed["html_url"] = url;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                                  UpdateChecker::Gitee).status, UpdateChecker::Failed);
        }
        for (const auto &suffix : {"?x=1", "#other"}) {
            changed = base;
            changed["html_url"] = valid.url.toString() + suffix;
            QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(changed).toJson(), "0.10.1",
                                                  UpdateChecker::Gitee).status, UpdateChecker::Failed);
        }
        for (const auto &version : {"v0.10.2-beta.1", "0.10", "unknown"})
            QCOMPARE(UpdateChecker::parseRelease(giteeRelease(version), "0.10.1", UpdateChecker::Gitee).status,
                     UpdateChecker::Failed);
    }
    void rejectsGiteeAssetsFromOtherRepositoriesOrHosts() {
        const QString name = "EditHere-win-x64-setup.exe";
        for (const auto &prefix : {"https://gitee.com/elsewhere/other/releases/download/v0.10.2/",
                                   "https://gitee.com/InnGing/EditHereElse/releases/download/v0.10.2/",
                                   "https://github.com/Inginnng/EditHere/releases/download/v0.10.2/",
                                   "http://gitee.com/InnGing/EditHere/releases/download/v0.10.2/",
                                   "https://example.com/InnGing/EditHere/releases/download/v0.10.2/"}) {
            auto object = QJsonDocument::fromJson(giteeRelease("v0.10.2")).object();
            auto package = giteeAsset(name, "v0.10.2");
            auto hash = giteeAsset(name + ".sha256", "v0.10.2");
            package["browser_download_url"] = QString(prefix) + name;
            hash["browser_download_url"] = QString(prefix) + name + ".sha256";
            object["assets"] = QJsonArray{package, hash};
            const auto result = UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "0.10.1",
                                                            UpdateChecker::Gitee);
            QCOMPARE(result.status, UpdateChecker::Available);
            QVERIFY(result.installer.url.isEmpty());
            QVERIFY(result.installerHash.url.isEmpty());
            QVERIFY(!UpdateChecker::canAutoInstall(result, true, true));
        }
    }
    void giteeReleaseListSelectsHighestStableVersionRegardlessOfOrder() {
        const auto releaseObject = [this](const QString &version) {
            return QJsonDocument::fromJson(giteeRelease(version, true)).object();
        };
        auto prerelease = releaseObject("v0.10.99");
        prerelease["prerelease"] = true;
        auto draft = releaseObject("v1.0.0");
        draft["draft"] = true;
        auto wrongRepository = releaseObject("v9.0.0");
        wrongRepository["html_url"] = "https://gitee.com/elsewhere/other/releases/tag/v9.0.0";
        const QJsonArray releases{releaseObject("v0.10.9"), prerelease, releaseObject("v0.10.3"),
                                  QJsonObject{{"message", "Not Found"}}, draft, wrongRepository,
                                  releaseObject("v0.10.10"), releaseObject("v0.10.2")};
        const auto bytes = QJsonDocument(releases).toJson();
        const auto available = UpdateChecker::parseReleases(bytes, "0.10.1", UpdateChecker::Gitee);
        QCOMPARE(available.status, UpdateChecker::Available);
        QCOMPARE(available.tagName, QString("v0.10.10"));
        QCOMPARE(available.url.host(), QString("gitee.com"));
        QVERIFY(UpdateChecker::canAutoInstall(available, false, true));
        QCOMPARE(UpdateChecker::parseReleases(bytes, "0.10.10", UpdateChecker::Gitee).status,
                 UpdateChecker::Current);
        QCOMPARE(UpdateChecker::parseReleases(bytes, "0.10.11", UpdateChecker::Gitee).status,
                 UpdateChecker::NewerLocal);
    }
    void giteeReleaseListWithoutStableReleaseFails() {
        auto prerelease = QJsonDocument::fromJson(giteeRelease("v0.10.4")).object();
        prerelease["prerelease"] = true;
        auto draft = QJsonDocument::fromJson(giteeRelease("v0.10.5")).object();
        draft["draft"] = true;
        const auto unstable = QJsonDocument(QJsonArray{prerelease, draft,
            QJsonDocument::fromJson(giteeRelease("v0.10.6-beta.1")).object()}).toJson();
        for (const auto &bytes : {QByteArray("[]"), QByteArray("{}"), QByteArray("<html>error</html>"),
                                  QByteArray(), QByteArray(1024 * 1024 + 1, ' '), unstable}) {
            const auto result = UpdateChecker::parseReleases(bytes, "0.10.3", UpdateChecker::Gitee);
            QCOMPARE(result.status, UpdateChecker::Failed);
            QCOMPARE(result.url, UpdateChecker::releasesUrl(UpdateChecker::Gitee));
        }
    }
    void selectsNewestReleaseAcrossBothSources_data() {
        QTest::addColumn<QString>("githubVersion");
        QTest::addColumn<QString>("giteeVersion");
        QTest::addColumn<QString>("expectedVersion");
        QTest::addColumn<QString>("expectedHost");
        QTest::newRow("github-newer") << "v0.10.3" << "v0.10.2" << "v0.10.3" << "github.com";
        QTest::newRow("gitee-newer") << "v0.10.2" << "v0.10.3" << "v0.10.3" << "gitee.com";
        QTest::newRow("numeric-patch") << "v0.10.9" << "v0.10.10" << "v0.10.10" << "gitee.com";
        QTest::newRow("numeric-minor") << "v0.11.0" << "v0.10.99" << "v0.11.0" << "github.com";
        QTest::newRow("same-complete-prefer-gitee") << "v0.10.3" << "v0.10.3" << "v0.10.3" << "gitee.com";
    }
    void selectsNewestReleaseAcrossBothSources() {
        QFETCH(QString, githubVersion);
        QFETCH(QString, giteeVersion);
        QFETCH(QString, expectedVersion);
        QFETCH(QString, expectedHost);
        const auto github = UpdateChecker::parseRelease(releaseWithAssets(githubVersion, false), "0.10.1");
        const auto gitee = UpdateChecker::parseRelease(giteeRelease(giteeVersion, true), "0.10.1",
                                                      UpdateChecker::Gitee);
        const auto result = UpdateChecker::selectRelease(github, gitee);
        QCOMPARE(result.status, UpdateChecker::Available);
        QCOMPARE(result.tagName, expectedVersion);
        QCOMPARE(result.url.host(), expectedHost);
        QCOMPARE(result.installer.url.host(), expectedHost);
        QCOMPARE(result.installerHash.url.host(), expectedHost);
        QVERIFY(UpdateChecker::canAutoInstall(result, false, true));
    }
    void sameVersionKeepsPackageAndHashFromOneCompleteSource() {
        auto github = UpdateChecker::parseRelease(releaseWithAssets("v0.10.3", false), "0.10.1");
        auto gitee = UpdateChecker::parseRelease(giteeRelease("v0.10.3", true), "0.10.1",
                                                 UpdateChecker::Gitee);
        const auto completeGithub = github;
        const auto completeGitee = gitee;
        gitee.installerHash = {};
        auto result = UpdateChecker::selectRelease(github, gitee);
        QCOMPARE(result.url.host(), QString("github.com"));
        QCOMPARE(result.installer.url, github.installer.url);
        QCOMPARE(result.installerHash.url, github.installerHash.url);
        github.installerHash = {};
        result = UpdateChecker::selectRelease(github, completeGitee);
        QCOMPARE(result.url.host(), QString("gitee.com"));
        QCOMPARE(result.installer.url, completeGitee.installer.url);
        QCOMPARE(result.installerHash.url, completeGitee.installerHash.url);

        // Equal tags do not prove the binaries are identical: never borrow the
        // hash from one host for the package published on the other host.
        gitee = completeGitee;
        gitee.installer = {};
        result = UpdateChecker::selectRelease(github, gitee);
        QVERIFY(!UpdateChecker::canAutoInstall(result, true, true));
        QVERIFY(result.installer.url.isEmpty() || result.installerHash.url.isEmpty());

        // A newer release must remain selected even when only the older source
        // has an installer; its assets cannot be used for the newer version.
        const auto newerBare = UpdateChecker::parseRelease(giteeRelease("v0.10.4"), "0.10.1",
                                                           UpdateChecker::Gitee);
        result = UpdateChecker::selectRelease(completeGithub, newerBare);
        QCOMPARE(result.tagName, QString("v0.10.4"));
        QVERIFY(result.installer.url.isEmpty());
        QVERIFY(result.installerHash.url.isEmpty());
    }
    void partialFailurePreservesSuccessfulResultWithoutClaimingGlobalLatest() {
        const auto githubFailed = UpdateChecker::parseRelease("{}", "0.10.3");
        const auto giteeFailed = UpdateChecker::parseRelease("{}", "0.10.3", UpdateChecker::Gitee);
        for (const QString version : {QString("v0.10.4"), QString("v0.10.3"), QString("v0.10.2")}) {
            const auto github = UpdateChecker::parseRelease(releaseWithAssets(version, false), "0.10.3");
            const auto gitee = UpdateChecker::parseRelease(giteeRelease(version, true), "0.10.3",
                                                          UpdateChecker::Gitee);
            for (const auto &result : {UpdateChecker::selectRelease(github, giteeFailed),
                                       UpdateChecker::selectRelease(githubFailed, gitee)}) {
                QCOMPARE(result.status, github.status);
                QCOMPARE(result.tagName, version);
                if (result.status == UpdateChecker::Available) {
                    QVERIFY(UpdateChecker::canAutoInstall(result, false, true));
                    QCOMPARE(result.installer.url.host(), result.url.host());
                    QCOMPARE(result.installerHash.url.host(), result.url.host());
                } else {
                    QVERIFY(!result.message.contains(QStringLiteral("已是最新")));
                    QVERIFY(result.message.contains(QStringLiteral("无法确认")));
                }
            }
        }
        QCOMPARE(UpdateChecker::selectRelease(githubFailed, giteeFailed).status, UpdateChecker::Failed);
    }
    void collectionWaitsForBothSourcesAndEmitsOnlyOnce() {
        const auto github = UpdateChecker::parseRelease(releaseWithAssets("v0.10.3", false), "0.10.1");
        const auto gitee = UpdateChecker::parseRelease(giteeRelease("v0.10.4", true), "0.10.1",
                                                      UpdateChecker::Gitee);
        for (const bool githubFirst : {true, false}) {
            UpdateChecker checker;
            QSignalSpy finished(&checker, &UpdateChecker::finished);
            UpdateCheckerTestAccess::beginCheck(checker);
            const auto firstSource = githubFirst ? UpdateChecker::GitHub : UpdateChecker::Gitee;
            const auto secondSource = githubFirst ? UpdateChecker::Gitee : UpdateChecker::GitHub;
            const auto firstResult = githubFirst ? github : gitee;
            const auto secondResult = githubFirst ? gitee : github;
            UpdateCheckerTestAccess::collect(checker, firstSource, firstResult);
            QVERIFY(checker.busy());
            QCOMPARE(finished.size(), 0);
            UpdateCheckerTestAccess::collect(checker, firstSource, firstResult);
            QVERIFY(checker.busy());
            QCOMPARE(finished.size(), 0);
            UpdateCheckerTestAccess::collect(checker, secondSource, secondResult);
            QVERIFY(!checker.busy());
            QCOMPARE(finished.size(), 1);
            QCOMPARE(checker.lastResult().tagName, QString("v0.10.4"));
            QCOMPARE(checker.lastResult().installer.url, gitee.installer.url);
            UpdateCheckerTestAccess::collect(checker, secondSource, secondResult);
            UpdateCheckerTestAccess::expireCheck(checker);
            QCOMPARE(finished.size(), 1);
        }
    }
    void deadlinePreservesCompletedSourceAndIgnoresLateResults() {
        for (const auto source : {UpdateChecker::GitHub, UpdateChecker::Gitee}) {
            const auto successful = source == UpdateChecker::GitHub
                ? UpdateChecker::parseRelease(releaseWithAssets("v0.10.4", false), "0.10.3")
                : UpdateChecker::parseRelease(giteeRelease("v0.10.4", true), "0.10.3", source);
            UpdateChecker checker;
            QSignalSpy finished(&checker, &UpdateChecker::finished);
            UpdateCheckerTestAccess::beginCheck(checker);
            UpdateCheckerTestAccess::collect(checker, source, successful);
            UpdateCheckerTestAccess::expireCheck(checker);
            QCOMPARE(finished.size(), 1);
            QVERIFY(!checker.busy());
            QCOMPARE(checker.lastResult().status, UpdateChecker::Available);
            QCOMPARE(checker.lastResult().installer.url, successful.installer.url);
            const auto lateSource = source == UpdateChecker::GitHub ? UpdateChecker::Gitee : UpdateChecker::GitHub;
            UpdateCheckerTestAccess::collect(checker, lateSource, successful);
            QCOMPARE(finished.size(), 1);
            QCOMPARE(checker.lastResult().url, successful.url);
        }
        UpdateChecker checker;
        QSignalSpy finished(&checker, &UpdateChecker::finished);
        UpdateCheckerTestAccess::beginCheck(checker);
        UpdateCheckerTestAccess::expireCheck(checker);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(checker.lastResult().status, UpdateChecker::Failed);
        QVERIFY(!checker.busy());
    }
    void failedSourceStillWaitsForRemainingSource() {
        for (const auto failedSource : {UpdateChecker::GitHub, UpdateChecker::Gitee}) {
            UpdateChecker checker;
            QSignalSpy finished(&checker, &UpdateChecker::finished);
            UpdateCheckerTestAccess::beginCheck(checker);
            const auto failed = UpdateChecker::parseRelease("{}", "0.10.3", failedSource);
            UpdateCheckerTestAccess::collect(checker, failedSource, failed);
            QVERIFY(checker.busy());
            QCOMPARE(finished.size(), 0);
            const auto successfulSource = failedSource == UpdateChecker::GitHub
                ? UpdateChecker::Gitee : UpdateChecker::GitHub;
            const auto successful = successfulSource == UpdateChecker::GitHub
                ? UpdateChecker::parseRelease(releaseWithAssets("v0.10.4", false), "0.10.3")
                : UpdateChecker::parseRelease(giteeRelease("v0.10.4", true), "0.10.3", successfulSource);
            UpdateCheckerTestAccess::collect(checker, successfulSource, successful);
            QVERIFY(!checker.busy());
            QCOMPARE(finished.size(), 1);
            QCOMPARE(checker.lastResult().status, UpdateChecker::Available);
            QCOMPARE(checker.lastResult().installer.url, successful.installer.url);
        }
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
            QSKIP("Opt-in real GitHub+Gitee release integration check");
        UpdateChecker checker;
        QSignalSpy result(&checker, &UpdateChecker::finished);
        checker.check();
        checker.check(); // Coalesce concurrent checks.
        QVERIFY(result.wait(20000));
        QCOMPARE(result.size(), 1);
        QVERIFY(!checker.busy());
        qInfo().noquote() << result.first()[1].toString();
        QVERIFY(result.first()[0].value<UpdateChecker::Status>() != UpdateChecker::Failed);
        for (const auto source : {UpdateChecker::GitHub, UpdateChecker::Gitee}) {
            const QString sourceName = source == UpdateChecker::GitHub ? "GitHub" : "Gitee";
            const auto &sourceResult = UpdateCheckerTestAccess::sourceResult(checker, source);
            QVERIFY2(sourceResult.has_value(), qPrintable(sourceName + " did not complete before the deadline"));
            QVERIFY2(sourceResult->status != UpdateChecker::Failed, qPrintable(sourceResult->message));
            QCOMPARE(sourceResult->url.host(), source == UpdateChecker::GitHub
                         ? QString("github.com") : QString("gitee.com"));
            qInfo().noquote() << sourceName << sourceResult->tagName << sourceResult->url.host();
        }
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "update_test.moc"
