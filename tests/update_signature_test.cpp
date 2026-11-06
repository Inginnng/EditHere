#include "updatechecker.h"
#include "updateauth.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace h2d;

// In-memory transport exercises the production download state machine. No real
// server, policy override, shell, installer or user installation is involved.
class Reply final : public QNetworkReply {
    QByteArray data_;
    qint64 position_ = 0;
  public:
    bool aborted = false;
    Reply(const QNetworkRequest &request, QByteArray data, QObject *parent)
        : QNetworkReply(parent), data_(std::move(data)) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
    }
    void deliver(bool error = false) {
        if (aborted) return;
        if (error) setError(QNetworkReply::ContentNotFoundError, "missing fixture");
        emit readyRead();
        if (aborted) return;
        setFinished(true); emit finished();
    }
    void abort() override { aborted = true; }
    qint64 bytesAvailable() const override { return data_.size() - position_ + QNetworkReply::bytesAvailable(); }
  protected:
    qint64 readData(char *target, qint64 maximum) override {
        const auto count = qMin(maximum, qint64(data_.size()) - position_);
        if (count <= 0) return -1;
        memcpy(target, data_.constData() + position_, size_t(count));
        position_ += count; return count;
    }
};
namespace h2d {
class UpdateCheckerTestAccess {
  public:
    static void transport(UpdateChecker &checker, std::function<QNetworkReply *(const QNetworkRequest &)> get) {
        checker.testGet_ = std::move(get);
    }
    static void launcher(UpdateChecker &checker, std::function<bool(QProcess &)> launch) {
        checker.launchInstaller_ = std::move(launch);
    }
    static QString directory(UpdateChecker &checker) { return checker.downloadDirectory_ ? checker.downloadDirectory_->path() : QString(); }
    static void install(UpdateChecker &checker, const QString &directory, const QString &name,
                        const QString &version, const QString &hash, const QByteArray &metadata) {
        checker.downloadDirectory_ = std::make_unique<QTemporaryDir>(QDir(directory).filePath("EditHere-update-XXXXXX"));
        checker.downloadPath_ = checker.downloadDirectory_->filePath(name);
        checker.expectedInstallVersion_ = version;
        checker.signatureMetadata_ = metadata;
        QFile file(checker.downloadPath_);
        file.open(QIODevice::WriteOnly); file.write("payload"); file.close();
        checker.installing_ = true;
        checker.verifyAndInstall(checker.downloadPath_, hash, true);
    }
};
}
class SignatureTests : public QObject {
    Q_OBJECT
    QByteArray metadata() {
        QFile file(QFINDTESTDATA("fixtures/update-signature.txt"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    QString hash() { return QString::fromLatin1(QCryptographicHash::hash("payload", QCryptographicHash::Sha256).toHex()); }
    QByteArray release(UpdateChecker::Source source, const QString &version = "99.0.0", bool signedSet = true) {
        const auto base = UpdateChecker::releasesUrl(source).toString();
        const auto download = base + "/download/v" + version + "/";
        QJsonArray assets;
        const QString name = "EditHere-win-x64-setup.exe";
        for (const auto &suffix : {QString(), QString(".sha256"), QString(".sig")}) {
            if (suffix == ".sig" && !signedSet) continue;
            assets.append(QJsonObject{{"name", name + suffix}, {"browser_download_url", download + name + suffix}});
        }
        return QJsonDocument(QJsonObject{{"tag_name", "v" + version}, {"draft", false}, {"prerelease", false},
            {"html_url", base + "/tag/v" + version}, {"assets", assets}}).toJson();
    }
  private slots:
    void independentNodeFixtureVerifiesInCng() {
#ifndef Q_OS_WIN
        QSKIP("CNG is Windows-only; signer and parsing tests run on every platform.");
#else
        QString error;
        QVERIFY2(updateauth::verifyProjectSignature(metadata(), "99.0.0", "EditHere-win-x64-setup.exe", hash(), &error), qPrintable(error));
#endif
    }
    void rejectsMalformedOrChangedSignedIdentity() {
        const auto good = metadata();
        QVERIFY(!good.isEmpty());
        QList<QByteArray> bad{QByteArray(), QByteArray(1025, 'x'), good + '\n', good.left(good.size() - 1)};
        auto changed = good; changed[changed.size() - 10] ^= 1; bad << changed;
        changed = good; changed.replace("99.0.0", "98.0.0"); bad << changed;
        changed = good; changed.replace("\n", "\r\n"); bad << changed;
        changed = good; changed[good.size() - 2] = '!'; bad << changed;
        changed = good; changed[good.size() - 90] = char(0x80); bad << changed;
        QFile wrongKey(QFINDTESTDATA("fixtures/update-signature-wrong-key.txt"));
        QVERIFY(wrongKey.open(QIODevice::ReadOnly)); bad << wrongKey.readAll();
        for (const auto &value : bad) {
            QString error;
            QVERIFY(!updateauth::verifyProjectSignature(value, "99.0.0", "EditHere-win-x64-setup.exe", hash(), &error));
            QVERIFY(!error.isEmpty());
        }
        for (const auto &version : {"98.0.0", "099.0.0", "99.0.0\n", "2147483648.0.0", "99.0.0-beta"})
            QVERIFY(!updateauth::verifyProjectSignature(good, version, "EditHere-win-x64-setup.exe", hash(), nullptr));
        for (const auto &name : {"other.exe", "EditHere-99.0.0-win-x64-setup.exe", "../EditHere-win-x64-setup.exe"})
            QVERIFY(!updateauth::verifyProjectSignature(good, "99.0.0", name, hash(), nullptr));
        QVERIFY(!updateauth::verifyProjectSignature(good, "99.0.0", "EditHere-win-x64-setup.exe", QString(64, '0'), nullptr));
    }
    void sameVersionPrefersWholeSignedSourceAndExactName() {
        auto github = UpdateChecker::parseRelease(release(UpdateChecker::GitHub), "1.0.0");
        auto gitee = UpdateChecker::parseRelease(release(UpdateChecker::Gitee, "99.0.0", false), "1.0.0", UpdateChecker::Gitee);
        QCOMPARE(UpdateChecker::selectRelease(github, gitee).installer.url.host(), QString("github.com"));
        gitee = UpdateChecker::parseRelease(release(UpdateChecker::Gitee), "1.0.0", UpdateChecker::Gitee);
        github.installerSignature = {};
        QCOMPARE(UpdateChecker::selectRelease(github, gitee).installer.url.host(), QString("gitee.com"));
        const auto newer = UpdateChecker::parseRelease(release(UpdateChecker::GitHub, "100.0.0", false), "1.0.0");
        QCOMPARE(UpdateChecker::selectRelease(newer, gitee).tagName, QString("v100.0.0"));
        if (!updateauth::certificateMode()) QVERIFY(!UpdateChecker::canAutoInstall(newer, true, true));
        auto object = QJsonDocument::fromJson(release(UpdateChecker::GitHub)).object();
        auto assets = object["assets"].toArray();
        const auto original = assets;
        // An incomplete versioned sibling must not hide the signed stable set.
        assets.append(QJsonObject{{"name", "EditHere-99.0.0-win-x64-setup.exe"},
            {"browser_download_url", "https://github.com/Inginnng/EditHere/releases/download/v99.0.0/EditHere-99.0.0-win-x64-setup.exe"}});
        object["assets"] = assets;
        QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "1.0.0").installer.name, QString("EditHere-win-x64-setup.exe"));
        for (const auto &url : {"https://gitee.com/InnGing/EditHere/releases/download/v99.0.0/EditHere-win-x64-setup.exe.sig",
            "https://github.com/Inginnng/EditHere/releases/download/v98.0.0/EditHere-win-x64-setup.exe.sig",
            "https://github.com/Inginnng/EditHere/releases/download/v99.0.0/other.exe.sig"}) {
            assets = original; auto sig = assets[2].toObject(); sig["browser_download_url"] = url; assets[2] = sig; object["assets"] = assets;
            QVERIFY(UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "1.0.0").installerSignature.url.isEmpty());
        }
        assets = original; assets.append(assets[2]); object["assets"] = assets;
        QVERIFY(UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "1.0.0").installerSignature.url.isEmpty());
        object["tag_name"] = "v99.0.0\n";
        QCOMPARE(UpdateChecker::parseRelease(QJsonDocument(object).toJson(), "1.0.0").status, UpdateChecker::Failed);
    }
    void realEntryAuthenticatesBeforeConfirmationAndLaunch_data() {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 8; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void realEntryAuthenticatesBeforeConfirmationAndLaunch() {
#ifndef Q_OS_WIN
        QSKIP("Windows install boundary");
#else
        QFETCH(int, mutation);
        QTemporaryDir directory;
        UpdateChecker checker;
        QSignalSpy failed(&checker, &UpdateChecker::installFailed);
        QSignalSpy preparing(&checker, &UpdateChecker::installerPreparing);
        int confirmed = 0, launched = 0;
        bool locked = false;
        checker.setInstallConfirmation([&] { ++confirmed; return true; });
        UpdateCheckerTestAccess::launcher(checker, [&](QProcess &process) {
            ++launched;
            const auto native = QDir::toNativeSeparators(process.program());
            const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            locked = handle == INVALID_HANDLE_VALUE;
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            return false; // Observe launch without running any external process.
        });
        auto sig = metadata(); auto version = QString("99.0.0"); auto name = QString("EditHere-win-x64-setup.exe"); auto digest = hash();
        if (mutation == 1) sig.clear();
        if (mutation == 2) sig[sig.size()-10] ^= 1;
        if (mutation == 3) version = "100.0.0";
        if (mutation == 4) name = "EditHere-99.0.0-win-x64-setup.exe";
        if (mutation == 5) digest = QString(64, '0');
        if (mutation == 6) version = EDITHERE_VERSION; // Old/current version replay.
        if (mutation == 7) sig = QByteArray(1025, 'x');
        UpdateCheckerTestAccess::install(checker, directory.path(), name, version, digest, sig);
        const bool accepted = mutation == 0 && !updateauth::certificateMode();
        QCOMPARE(confirmed, int(accepted)); QCOMPARE(launched, int(accepted));
        if (accepted) QVERIFY(locked);
        QCOMPARE(failed.size(), 1); QCOMPARE(preparing.size(), 0);
        QVERIFY(!checker.downloading()); QVERIFY(QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
#endif
    }
    void downloadBoundsCancelDestroyRetryAndBothDistributions() {
#ifndef Q_OS_WIN
        QSKIP("Windows download/install");
#else
        if (updateauth::certificateMode()) return; // The real entry above proves no project fallback.
        const auto result = UpdateChecker::parseRelease(release(UpdateChecker::GitHub), "1.0.0");
        const auto sig = metadata();
        for (int scenario = 0; scenario < 8; ++scenario) {
            auto checker = std::make_unique<UpdateChecker>();
            QSignalSpy failed(checker.get(), &UpdateChecker::installFailed);
            int confirmations = 0, launches = 0;
            bool correctMode = true;
            checker->setInstallConfirmation([&] { ++confirmations; return scenario != 7; });
            UpdateCheckerTestAccess::launcher(*checker, [&](QProcess &process) {
                ++launches;
                correctMode = process.arguments().contains("/PORTABLE") == (scenario == 6);
                return false;
            });
            QList<QPointer<Reply>> replies;
            bool hold = scenario == 3 || scenario == 4;
            bool oversize = scenario == 1 || scenario == 2;
            bool missing = scenario == 5;
            UpdateCheckerTestAccess::transport(*checker, [&](const QNetworkRequest &request) {
                const auto name = request.url().fileName();
                QByteArray body = name.endsWith(".sig") ? sig : name.endsWith(".sha256")
                    ? hash().toLatin1() + "  EditHere-win-x64-setup.exe\n" : QByteArray("payload");
                if (oversize && name.endsWith(scenario == 1 ? ".sha256" : ".sig")) body = QByteArray(5000, 'x');
                auto *reply = new Reply(request, body, checker.get()); replies << reply;
                if (!(hold && name.endsWith(".sig"))) QTimer::singleShot(0, reply, [reply, missing, name] { reply->deliver(missing && name.endsWith(".sig")); });
                return reply;
            });
            const auto start = [&] { checker->downloadAndInstall(result.installer, result.installerHash,
                scenario != 6, result.tagName, result.installerSignature); };
            start();
            if (hold) {
                QTRY_COMPARE(replies.size(), 3);
                const auto path = UpdateCheckerTestAccess::directory(*checker);
                if (scenario == 4) checker.reset(); else checker->cancelDownload();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QVERIFY(replies.last().isNull()); QVERIFY(!QFileInfo::exists(path));
                QCOMPARE(confirmations, 0); QCOMPARE(launches, 0);
                if (scenario == 4) continue;
            } else {
                QTRY_COMPARE(failed.size(), 1);
                QCOMPARE(confirmations, int(scenario == 0 || scenario == 6 || scenario == 7));
                QCOMPARE(launches, int(scenario == 0 || scenario == 6));
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            for (const auto &reply : replies) QVERIFY(reply.isNull());
            QVERIFY(!checker->downloading());
            QVERIFY(correctMode);
            // The same updater can retry after failure/cancel with fresh replies.
            hold = oversize = missing = false;
            const int previous = failed.size(); start(); QTRY_COMPARE(failed.size(), previous + 1);
            QVERIFY(!checker->downloading());
        }
#endif
    }
    void cancellationOrDestructionDuringConfirmationReclaimsLockedPackage() {
#ifndef Q_OS_WIN
        QSKIP("Windows file locking and install confirmation");
#else
        if (updateauth::certificateMode()) return;
        for (const bool destroy : {false, true}) {
            QTemporaryDir directory;
            auto checker = std::make_unique<UpdateChecker>();
            QSignalSpy failed(checker.get(), &UpdateChecker::installFailed);
            int launches = 0;
            QString download;
            UpdateCheckerTestAccess::launcher(*checker, [&](QProcess &) { ++launches; return false; });
            checker->setInstallConfirmation([&] {
                download = UpdateCheckerTestAccess::directory(*checker);
                if (destroy) checker.reset(); else checker->cancelDownload();
                return true;
            });
            UpdateCheckerTestAccess::install(*checker, directory.path(), "EditHere-win-x64-setup.exe", "99.0.0", hash(), metadata());
            QCOMPARE(launches, 0);
            QCOMPARE(failed.size(), destroy ? 0 : 1);
            QVERIFY(!download.isEmpty()); QVERIFY(!QFileInfo::exists(download));
        }
#endif
    }
};
QTEST_GUILESS_MAIN(SignatureTests)
#include "update_signature_test.moc"
