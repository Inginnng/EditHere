#include "updatechecker.h"
#include "updateauth.h"
#include "diagnostics.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QVersionNumber>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace h2d {
namespace {
constexpr int maximumResponse = 1024 * 1024;
UpdateChecker::Result failure(const QString &message, UpdateChecker::Source source = UpdateChecker::GitHub) {
    return {UpdateChecker::Failed, message, UpdateChecker::releasesUrl(source), {}, {}, {}, {}, {}};
}
bool isSourceAssetUrl(const QUrl &url, UpdateChecker::Source source) {
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasFragment() || url.hasQuery())
        return false;
    if (source == UpdateChecker::Gitee)
        return url.host() == "gitee.com" && url.path().startsWith("/InnGing/EditHere/releases/download/");
    return url.host() == "github.com" && url.path().startsWith("/Inginnng/EditHere/releases/download/");
}
bool matchesReleaseAsset(const UpdateChecker::Asset &asset, const QString &tag) {
    for (const auto source : {UpdateChecker::GitHub, UpdateChecker::Gitee}) {
        const QString repo = source == UpdateChecker::GitHub ? "/Inginnng/EditHere" : "/InnGing/EditHere";
        if (isSourceAssetUrl(asset.url, source) &&
            asset.url.path() == repo + "/releases/download/" + tag + "/" + asset.name)
            return true;
    }
    return false;
}
bool compatibleInstallerAssets(const UpdateChecker::Result &result, bool installed, bool windows) {
    return windows && result.status == UpdateChecker::Available && (installed || result.portableInstallerSupported) &&
           matchesReleaseAsset(result.installer, result.tagName) && matchesReleaseAsset(result.installerHash, result.tagName) &&
           result.installer.url.host() == result.installerHash.url.host() &&
           result.installerHash.name == result.installer.name + ".sha256";
}
bool signedInstallerAssets(const UpdateChecker::Result &result) {
    return compatibleInstallerAssets(result, true, true) &&
           matchesReleaseAsset(result.installerSignature, result.tagName) &&
           result.installerSignature.url.host() == result.installer.url.host() &&
           result.installerSignature.name == result.installer.name + ".sig";
}
int completeness(const UpdateChecker::Result &result) {
    return signedInstallerAssets(result) ? 2 : compatibleInstallerAssets(result, true, true) ? 1 : 0;
}
QVersionNumber releaseVersion(const UpdateChecker::Result &result) {
    return QVersionNumber::fromString(result.tagName.startsWith('v') ? result.tagName.mid(1) : result.tagName);
}
} // namespace
QUrl UpdateChecker::releasesUrl(Source source) {
    return source == Gitee ? QUrl("https://gitee.com/InnGing/EditHere/releases")
                           : QUrl("https://github.com/Inginnng/EditHere/releases");
}
UpdateChecker::Result UpdateChecker::parseRelease(const QByteArray &bytes, const QString &currentVersion,
                                                 Source source) {
    if (bytes.size() > maximumResponse)
        return failure(tr("更新信息过大，请稍后重试。"), source);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return failure(tr("无法读取更新信息，请稍后重试。"), source);
    const auto object = document.object();
    const auto tag = object.value("tag_name").toString();
    static const QRegularExpression versionPattern("\\Av?([0-9]+\\.[0-9]+\\.[0-9]+)\\z");
    const auto match = versionPattern.match(tag);
    const auto localMatch = versionPattern.match(currentVersion);
    // Gitee's latest-release API omits draft and html_url. Build its public
    // release page from the fixed repository and the validated version tag.
    const QUrl expectedUrl(releasesUrl(source).toString() + "/tag/" + tag);
    const QUrl url(source == Gitee && !object.contains("html_url")
                       ? expectedUrl : QUrl(object.value("html_url").toString()));
    const bool invalidDraft = source == GitHub ? !object.value("draft").isBool() || object.value("draft").toBool()
                                              : object.contains("draft") &&
                                                    (!object.value("draft").isBool() || object.value("draft").toBool());
    if (!match.hasMatch() || !localMatch.hasMatch() ||
        !updateauth::canonicalVersion(match.captured(1)) || !updateauth::canonicalVersion(localMatch.captured(1)) || invalidDraft ||
        !object.value("prerelease").isBool() || object.value("prerelease").toBool() ||
        url.scheme() != "https" || url.host() != expectedUrl.host() ||
        !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasQuery() || url.hasFragment() ||
        url.path() != expectedUrl.path())
        return failure(tr("发行信息不是有效的 EditHere 正式版本，请到发布页查看。"), source);
    const auto remote = QVersionNumber::fromString(match.captured(1));
    const auto local = QVersionNumber::fromString(localMatch.captured(1));
    if (remote.segmentCount() != 3 || local.segmentCount() != 3)
        return failure(tr("版本号无法比较，请到发布页查看。"), source);
    const QString ver = match.captured(1);
    const auto assets = object.value("assets").toArray();
    // Releases publish version-less asset names so documentation links stay valid.
    // The historical versioned names are still accepted: pick whichever exists,
    // preferring the versioned name when a release carries both.
    const auto pickAsset = [&assets, source, &tag](const QStringList &candidates) {
        Asset found;
        for (const QString &candidate : candidates) {
            for (const auto &item : assets) {
                const auto obj = item.toObject();
                if (obj.value("name").toString() != candidate)
                    continue;
                const QUrl assetUrl(obj.value("browser_download_url").toString());
                const Asset value{candidate, assetUrl, obj.value("size").toVariant().toLongLong()};
                if (!isSourceAssetUrl(assetUrl, source) || !matchesReleaseAsset(value, tag))
                    continue;
                // Duplicate exact names are ambiguous; do not silently pick one.
                if (!found.name.isEmpty()) return Asset{};
                found = value;
            }
            if (!found.name.isEmpty())
                return found;
        }
        return found;
    };
    const QStringList installerNames{QString("EditHere-%1-win-x64-setup.exe").arg(ver),
                                     QStringLiteral("EditHere-win-x64-setup.exe")};
    const QStringList portableNames{QString("EditHere-%1-win-x64.zip").arg(ver),
                                    QStringLiteral("EditHere-win-x64.zip")};
    // Prefer a complete signed bundle even when a release also carries an
    // incomplete historical versioned name. Never borrow a sibling's metadata.
    Asset installer;
    int bestBundle = -1;
    for (const auto &name : installerNames) {
        const auto package = pickAsset({name});
        if (package.name.isEmpty()) continue;
        const bool hash = !pickAsset({name + ".sha256"}).name.isEmpty();
        const bool signature = !pickAsset({name + ".sig"}).name.isEmpty();
        const int score = hash && signature ? 2 : hash ? 1 : 0;
        if (score > bestBundle) { installer = package; bestBundle = score; }
    }
    const Asset installerHash = installer.name.isEmpty() ? Asset{} : pickAsset({installer.name + ".sha256"});
    const Asset installerSignature = installer.name.isEmpty() ? Asset{} : pickAsset({installer.name + ".sig"});
    const Asset portable = pickAsset(portableNames);
    const Asset portableHash = portable.name.isEmpty() ? Asset{} : pickAsset({portable.name + ".sha256"});
    const int order = QVersionNumber::compare(remote, local);
    if (order > 0)
        return {Available, tr("发现新版本 %1。").arg(tag), url,
                tag, installer, installerHash, portable, portableHash,
                QVersionNumber::compare(remote, QVersionNumber(0, 9, 9)) >= 0, installerSignature};
    if (order < 0)
        return {NewerLocal, tr("当前版本高于已发布的正式版 %1。").arg(tag), url,
                tag, {}, {}, {}, {}};
    return {Current, tr("已是最新正式版 %1。").arg(tag), url,
            tag, {}, {}, {}, {}};
}
UpdateChecker::Result UpdateChecker::parseReleases(const QByteArray &bytes, const QString &currentVersion,
                                                  Source source) {
    if (bytes.size() > maximumResponse)
        return failure(tr("更新信息过大，请稍后重试。"), source);
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isArray())
        return failure(tr("无法读取更新信息，请稍后重试。"), source);
    std::optional<Result> newest;
    for (const auto &entry : document.array()) {
        const auto result = parseRelease(QJsonDocument(entry.toObject()).toJson(), currentVersion, source);
        if (result.status == Failed)
            continue;
        if (!newest || QVersionNumber::compare(releaseVersion(result), releaseVersion(*newest)) > 0 ||
            (releaseVersion(result) == releaseVersion(*newest) &&
             completeness(result) > completeness(*newest)))
            newest = result;
    }
    return newest.value_or(failure(tr("发行信息不是有效的 EditHere 正式版本，请到发布页查看。"), source));
}
UpdateChecker::Result UpdateChecker::selectRelease(const Result &github, const Result &gitee) {
    const bool githubOk = github.status != Failed;
    const bool giteeOk = gitee.status != Failed;
    if (!githubOk && !giteeOk)
        return failure(tr("GitHub 和 Gitee 均无法检查更新，请检查网络后重试。"));
    if (!githubOk || !giteeOk) {
        Result result = githubOk ? github : gitee;
        const QString checked = githubOk ? QStringLiteral("GitHub") : QStringLiteral("Gitee");
        const QString unavailable = githubOk ? QStringLiteral("Gitee") : QStringLiteral("GitHub");
        if (result.status == Available)
            result.message += '\n' + tr("%1 未能完成检查，以上结果来自 %2。").arg(unavailable, checked);
        else
            result.message = tr("已检查 %1，正式版为 %2；%3 未能完成检查，暂时无法确认最新版本。")
                                 .arg(checked, result.tagName, unavailable);
        return result;
    }
    const int order = QVersionNumber::compare(releaseVersion(github), releaseVersion(gitee));
    if (order != 0)
        return order > 0 ? github : gitee;
    // Select one whole release: a mirror's checksum must never be paired with
    // the other mirror's binary. Prefer complete packages, then the Gitee mirror.
    return completeness(github) > completeness(gitee) ? github : gitee;
}
UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {
    handoffTimer_.setInterval(250);
    connect(&handoffTimer_, &QTimer::timeout, this, &UpdateChecker::pollHandoff);
    timeout_.setSingleShot(true);
    timeout_.setInterval(15000);
    cliTimeout_.setSingleShot(true);
    cliTimeout_.setInterval(3000);
    connect(&cliTimeout_, &QTimer::timeout, this, [this] {
        if (!busy_) return;
        publicRequested_ = true;
        process_.kill();
        requestPublic();
    });
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(&timeout_, &QTimer::timeout, this, &UpdateChecker::completeCheck);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (!busy_ || publicRequested_ || githubResult_)
            return;
        output_ += process_.readAllStandardOutput();
        if (output_.size() > maximumResponse) {
            requestPublic();
            process_.kill();
        }
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && error == QProcess::FailedToStart)
            requestPublic();
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (!busy_ || githubResult_)
                    return;
                cliTimeout_.stop();
                if (publicRequested_) return;
                output_ += process_.readAllStandardOutput();
                const auto result = parseRelease(output_, QStringLiteral(EDITHERE_VERSION));
                if (code == 0 && status == QProcess::NormalExit && result.status != Failed)
                    collectSource(GitHub, result);
                else
                    requestPublic();
            });
}
UpdateChecker::~UpdateChecker() {
    cancelHandoff();
    busy_ = false;
    process_.disconnect(this);
    for (auto *pointer : {&reply_, &giteeReply_, &downloadReply_, &hashReply_}) {
        if (*pointer) {
            auto *reply = pointer->data();
            *pointer = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}
void UpdateChecker::check() {
    if (busy_ || installing_)
        return;
    if (process_.state() != QProcess::NotRunning) {
        QTimer::singleShot(100, this, &UpdateChecker::check);
        return;
    }
    busy_ = true;
    publicRequested_ = false;
    output_.clear();
    giteeOutput_.clear();
    githubResult_.reset();
    giteeResult_.reset();
    giteeCandidate_.reset();
    giteePage_ = 1;
    timeout_.start();
    requestGitee();
    auto gh = QStandardPaths::findExecutable("gh");
#ifdef Q_OS_WIN
    if (gh.isEmpty()) {
        const auto installed = qEnvironmentVariable("ProgramFiles") + "/GitHub CLI/gh.exe";
        if (QFileInfo::exists(installed))
            gh = installed;
    }
#else
    if (gh.isEmpty()) {
        for (const auto &path : {"/opt/homebrew/bin/gh", "/usr/local/bin/gh"})
            if (QFileInfo(path).isExecutable()) {
                gh = path;
                break;
            }
    }
#endif
    if (gh.isEmpty()) {
        requestPublic();
        return;
    }
    process_.start(gh, {"api", "repos/Inginnng/EditHere/releases/latest", "--hostname", "github.com"});
    if (!publicRequested_) cliTimeout_.start();
}
void UpdateChecker::requestPublic() {
    if (!busy_ || reply_ || githubResult_)
        return;
    publicRequested_ = true;
    cliTimeout_.stop();
    requestSource(GitHub);
}
void UpdateChecker::requestGitee() {
    requestSource(Gitee);
}
void UpdateChecker::requestSource(Source source) {
    auto &pointer = source == GitHub ? reply_ : giteeReply_;
    const auto &result = source == GitHub ? githubResult_ : giteeResult_;
    if (!busy_ || pointer || result)
        return;
    const QUrl url = source == GitHub
                         ? QUrl("https://api.github.com/repos/Inginnng/EditHere/releases/latest")
                         : QUrl(QString("https://gitee.com/api/v5/repos/InnGing/EditHere/releases?direction=desc&per_page=100&page=%1")
                                    .arg(giteePage_));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", source == GitHub ? "application/vnd.github+json" : "application/json");
    request.setRawHeader("User-Agent", "EditHere/" EDITHERE_VERSION);
    if (source == GitHub)
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto &buffer = source == GitHub ? output_ : giteeOutput_;
    buffer.clear();
    auto *reply = network_.get(request);
    pointer = reply;
    reply->setReadBufferSize(maximumResponse + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, source, reply] {
        const auto &pointer = source == GitHub ? reply_ : giteeReply_;
        if (!busy_ || pointer != reply)
            return;
        auto &buffer = source == GitHub ? output_ : giteeOutput_;
        buffer += reply->readAll();
        if (buffer.size() > maximumResponse)
            collectSource(source, failure(tr("更新信息过大，请稍后重试。"), source));
    });
    connect(reply, &QNetworkReply::finished, this, [this, source, reply] {
        auto &pointer = source == GitHub ? reply_ : giteeReply_;
        if (!busy_ || pointer != reply)
            return;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 || reply->error() != QNetworkReply::NoError) {
            collectSource(source, failure(tr("网络连接失败，暂时无法判断是否有更新。请稍后重试。"), source));
            return;
        }
        auto &buffer = source == GitHub ? output_ : giteeOutput_;
        buffer += reply->readAll();
        if (source == GitHub) {
            collectSource(source, parseRelease(buffer, QStringLiteral(EDITHERE_VERSION), source));
            return;
        }
        const auto document = QJsonDocument::fromJson(buffer);
        if (buffer.size() > maximumResponse || !document.isArray()) {
            collectSource(source, failure(tr("无法读取更新信息，请稍后重试。"), source));
            return;
        }
        const auto page = parseReleases(buffer, QStringLiteral(EDITHERE_VERSION), source);
        if (page.status != Failed &&
            (!giteeCandidate_ || QVersionNumber::compare(releaseVersion(page), releaseVersion(*giteeCandidate_)) > 0 ||
             (releaseVersion(page) == releaseVersion(*giteeCandidate_) &&
              completeness(page) > completeness(*giteeCandidate_))))
            giteeCandidate_ = page;
        // Gitee's "latest" means last edited and can be a preview. Read every
        // page of releases instead of relying on its ordering or that endpoint.
        if (document.array().size() == 100) {
            pointer = nullptr;
            reply->disconnect(this);
            reply->deleteLater();
            ++giteePage_;
            requestGitee();
            return;
        }
        collectSource(source, giteeCandidate_.value_or(page));
    });
}
void UpdateChecker::collectSource(Source source, Result result) {
    auto &stored = source == GitHub ? githubResult_ : giteeResult_;
    if (!busy_ || stored)
        return;
    stored = std::move(result);
    auto &pointer = source == GitHub ? reply_ : giteeReply_;
    if (pointer) {
        auto *reply = pointer.data();
        pointer = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (source == GitHub) {
        cliTimeout_.stop();
        publicRequested_ = true;
        if (process_.state() != QProcess::NotRunning)
            process_.kill();
    }
    if (githubResult_ && giteeResult_)
        completeCheck();
}
void UpdateChecker::completeCheck() {
    if (!busy_)
        return;
    const auto timeout = failure(tr("检查超时，请检查网络后重试。"));
    finish(selectRelease(githubResult_.value_or(timeout), giteeResult_.value_or(timeout)));
}
void UpdateChecker::finish(Result result) {
    if (!busy_)
        return;
    if (result.status == Failed)
        diagnostics::write(diagnostics::Level::Warning, "updates.check", result.message);
    busy_ = false;
    timeout_.stop();
    cliTimeout_.stop();
    for (auto *pointer : {&reply_, &giteeReply_}) {
        if (*pointer) {
            auto *reply = pointer->data();
            *pointer = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    if (process_.state() != QProcess::NotRunning)
        process_.kill();
    lastResult_ = result;
    emit finished(result.status, result.message, result.url);
}
QString UpdateChecker::prepareDownloadTarget(const QString &directory, const QString &packageName) {
    const QString path = QDir(directory).filePath("EditHere-update-" + packageName);
    QFile::remove(path);
    return path;
}
void UpdateChecker::downloadAndInstall(const Asset &package, const Asset &hashAsset, bool isInstaller,
                                       const QString &expectedVersion) {
    downloadAndInstall(package, hashAsset, isInstaller, expectedVersion, Asset{});
}
void UpdateChecker::downloadAndInstall(const Asset &package, const Asset &hashAsset, bool isInstaller,
                                       const QString &expectedVersion, const Asset &signatureAsset) {
    if (busy_ || installing_)
        return;
#ifndef Q_OS_WIN
    emit installFailed(tr("未找到适合当前系统的更新包，请前往发布页手动下载。"));
    return;
#endif
    if (!updateauth::installationConfigured()) {
        emit installFailed(tr("未配置可信发布者，程序内安装已停用。请前往发布页手动下载。"));
        return;
    }
    const auto version = expectedVersion.startsWith('v') ? expectedVersion.mid(1) : expectedVersion;
    Result candidate{Available, {}, {}, expectedVersion, package, hashAsset, {}, {}, true, signatureAsset};
    if (!updateauth::canonicalVersion(version) ||
        QVersionNumber::compare(QVersionNumber::fromString(version), QVersionNumber::fromString(EDITHERE_VERSION)) <= 0 ||
        (package.name != "EditHere-win-x64-setup.exe" && package.name != "EditHere-" + version + "-win-x64-setup.exe") ||
        !compatibleInstallerAssets(candidate, isInstaller, true) ||
        (!updateauth::certificateMode() && !signedInstallerAssets(candidate))) {
        emit installFailed(tr("未找到校验文件，无法验证安装包完整性。"));
        return;
    }
    installing_ = true;
    signatureMetadata_.clear();
    isInstallerUpdate_ = isInstaller;
    downloadFileName_ = package.name;
    expectedInstallVersion_ = expectedVersion.startsWith('v') ? expectedVersion.mid(1) : expectedVersion;
    // Qt supplies unique paths and atomic writes; retries cannot concatenate an
    // earlier download or overwrite another running installer's payload.
    downloadDirectory_ = std::make_shared<QTemporaryDir>(
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("EditHere-update-XXXXXX"));
    downloadPath_ = downloadDirectory_->filePath(package.name);
    downloadFile_ = std::make_unique<QSaveFile>(downloadPath_);
    if (!downloadDirectory_->isValid() || !downloadFile_->open(QIODevice::WriteOnly)) {
        failInstall(tr("无法写入更新文件：%1").arg(downloadFile_->errorString()));
        return;
    }
    // Download the package.
    QNetworkRequest request(package.url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    downloadReply_ = getDownloadReply(request);
    connect(downloadReply_, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(downloadReply_, &QNetworkReply::readyRead, this, [this] {
        if (!downloadReply_)
            return;
        const auto bytes = downloadReply_->readAll();
        if (downloadFile_->write(bytes) != bytes.size())
            failInstall(tr("无法写入更新文件：%1").arg(downloadFile_->errorString()));
    });
    connect(downloadReply_, &QNetworkReply::finished, this, [this, hashAsset, signatureAsset] {
        if (!downloadReply_)
            return;
        const auto reply = downloadReply_.data();
        downloadReply_ = nullptr;
        reply->disconnect(this);
        if (reply->error() != QNetworkReply::NoError) {
            reply->deleteLater();
            failInstall(tr("下载失败，请检查网络后重试。"));
            return;
        }
        const auto remaining = reply->readAll();
        reply->deleteLater();
        if (downloadFile_->write(remaining) != remaining.size() || !downloadFile_->commit()) {
            failInstall(tr("无法写入更新文件：%1").arg(downloadFile_->errorString()));
            return;
        }
        downloadFile_.reset();
        downloadMetadata(hashAsset, 4096, [this, signatureAsset](QByteArray hashData) {
            // Require the exact checksum filename too. The independent signature
            // binds the same lowercase hash; retaining this check detects damage.
            const auto line = QString::fromLatin1(hashData);
            const QRegularExpression pattern("\\A([0-9a-fA-F]{64}) [ *]" +
                QRegularExpression::escape(downloadFileName_) + "\\r?\\n?\\z");
            const auto match = pattern.match(line);
            if (!match.hasMatch()) {
                failInstall(tr("校验失败：安装包已损坏或不完整。"));
                return;
            }
            const auto hash = match.captured(1).toLower();
            if (updateauth::certificateMode()) {
                verifyAndInstall(downloadPath_, hash, isInstallerUpdate_);
                return;
            }
            downloadMetadata(signatureAsset, 1024, [this, hash](QByteArray metadata) {
                signatureMetadata_ = std::move(metadata);
                verifyAndInstall(downloadPath_, hash, isInstallerUpdate_);
            });
        });
    });
}
QNetworkReply *UpdateChecker::getDownloadReply(const QNetworkRequest &request) {
#ifdef EDITHERE_UPDATE_TEST_TRUST
    if (testGet_) return testGet_(request);
#endif
    return network_.get(request);
}
void UpdateChecker::downloadMetadata(const Asset &asset, int limit, std::function<void(QByteArray)> completed) {
    QNetworkRequest request(asset.url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = getDownloadReply(request);
    hashReply_ = reply;
    reply->setReadBufferSize(limit + 1);
    auto buffer = std::make_shared<QByteArray>();
    const auto read = [this, reply, buffer, limit] {
        if (hashReply_ != reply) return false;
        buffer->append(reply->read(limit + 1 - buffer->size()));
        if (buffer->size() > limit || reply->bytesAvailable() > 0) {
            failInstall(tr("更新签名或校验文件过大，已停止更新。"));
            return false;
        }
        return true;
    };
    connect(reply, &QNetworkReply::readyRead, this, [read] { read(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, read, completed] {
        if (hashReply_ != reply || !read()) return;
        hashReply_ = nullptr;
        reply->disconnect(this);
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            failInstall(tr("无法下载更新签名或校验文件，请检查网络后重试。"));
            return;
        }
        completed(*buffer);
    });
}
void UpdateChecker::failInstall(const QString &message) {
    cancelHandoff();
    diagnostics::write(diagnostics::Level::Error, "updates.install", message);
    for (auto *pointer : {&downloadReply_, &hashReply_}) {
        if (*pointer) {
            auto reply = pointer->data();
            *pointer = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    downloadFile_.reset();
    downloadDirectory_.reset();
    installing_ = false;
    emit installFailed(message);
}
void UpdateChecker::cancelDownload() {
    if (installing_)
        failInstall(tr("已取消更新，当前工作保持不变。"));
}
bool UpdateChecker::prepareHandoff(const QString &filePath, const QString &expectedHash, QString *error) {
    static const QRegularExpression idPattern("^EditHere-update-[A-Za-z0-9]+$");
    static const QRegularExpression versionPattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");
    handoffTarget_ = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    handoffId_ = downloadDirectory_ ? QFileInfo(downloadDirectory_->path()).fileName() : QString();
    if (!downloadDirectory_ || !downloadDirectory_->isValid() || !idPattern.match(handoffId_).hasMatch() ||
        !versionPattern.match(expectedInstallVersion_).hasMatch()) {
        if (error) *error = tr("无法准备更新请求：安装目录或版本信息无效。");
        return false;
    }
    handoffToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject request{{"id", handoffId_}, {"target", handoffTarget_},
                              {"version", expectedInstallVersion_}, {"token", handoffToken_},
                              {"processId", QCoreApplication::applicationPid()},
                              {"package", QFileInfo(filePath).fileName()}, {"sha256", expectedHash}};
    QSaveFile file(downloadDirectory_->filePath("request.json"));
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        if (error) *error = tr("无法写入更新请求：%1").arg(file.errorString());
        return false;
    }
    return true;
}
bool UpdateChecker::writeHandoffAction(const QString &action) {
    if (!downloadDirectory_)
        return false;
    QSaveFile file(downloadDirectory_->filePath("ack.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"action", action}, {"token", handoffToken_}})
                           .toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void UpdateChecker::cancelHandoff() {
    handoffTimer_.stop();
    if (!awaitingHandoff_)
        return;
    // Keep the cancellation marker until the worker observes it. The worker owns
    // cleanup after it acknowledges cancellation, including when this dialog dies.
    writeHandoffAction("cancel");
    awaitingHandoff_ = false;
}
void UpdateChecker::pollHandoff() {
    if (!awaitingHandoff_ || !downloadDirectory_)
        return;
    if (handoffDeadline_.elapsed() >= handoffTimeoutMs_) {
        failInstall(tr("安装器准备超时，更新已取消，当前工作保持不变。"));
        return;
    }
    QFile file(downloadDirectory_->filePath("status.json"));
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        failInstall(tr("无法读取安装器状态，更新已取消。"));
        return;
    }
    QJsonParseError error;
    const auto bytes = file.readAll();
    file.close(); // Release the Windows handle before reclaiming a failed handoff.
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        failInstall(tr("安装器状态无效，更新已取消。"));
        return;
    }
    const auto status = document.object();
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(status.value("target").toString()));
    const QString expectedTarget = QDir::cleanPath(QDir::fromNativeSeparators(handoffTarget_));
    if (status.value("token").toString() != handoffToken_ ||
        status.value("version").toString() != expectedInstallVersion_ ||
        target.compare(expectedTarget, Qt::CaseInsensitive) != 0) {
        failInstall(tr("安装器状态与本次更新不一致，更新已取消。"));
        return;
    }
    const auto state = status.value("status").toString();
    if (state == "failed" || state == "cancelled") {
        // The worker is terminal before ACK, so Qt can reclaim the request and package.
        awaitingHandoff_ = false;
        handoffTimer_.stop();
        downloadDirectory_->setAutoRemove(true);
        failInstall(status.value("message").toString().isEmpty()
                        ? tr("安装器准备失败，当前工作保持不变。") : status.value("message").toString());
        return;
    }
    if (state != "ready") {
        failInstall(tr("安装器状态无效，更新已取消。"));
        return;
    }
    if (!writeHandoffAction("ack")) {
        failInstall(tr("无法确认安装器已就绪，更新已取消。"));
        return;
    }
    // READY means validation and preparation succeeded. ACK permits the worker
    // to wait for this application to exit before it touches the installation.
    awaitingHandoff_ = false;
    handoffTimer_.stop();
    installing_ = false;
    emit installStarted();
}
QString UpdateChecker::installationResultPath() {
#ifdef Q_OS_WIN
    const auto local = qEnvironmentVariable("LOCALAPPDATA");
    return local.isEmpty() ? QString() : QDir(local).filePath("EditHere/Installer/Results/last.json");
#else
    return {};
#endif
}
QString UpdateChecker::installationResultMessage(const QJsonObject &receipt, const QString &target,
                                                  const QString &currentVersion) {
    const auto receiptTarget = QDir::cleanPath(QDir::fromNativeSeparators(receipt.value("target").toString()));
    const auto expectedTarget = QDir::cleanPath(QDir::fromNativeSeparators(target));
    static const QRegularExpression versionPattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");
    const auto version = receipt.value("version").toString();
    if (receipt.value("id").toString().isEmpty() || !versionPattern.match(version).hasMatch() ||
        receiptTarget.compare(expectedTarget, Qt::CaseInsensitive) != 0)
        return {};
    const auto status = receipt.value("status").toString();
    if (status == "failed")
        return receipt.value("message").toString().isEmpty()
                   ? tr("上次安装未完成。请检查安装日志和恢复信息。") : receipt.value("message").toString();
    if (status == "committed" && version != currentVersion)
        return tr("安装文件已切换，但当前运行版本与预期版本 %1 不一致。请检查启动路径。").arg(version);
    return {};
}
bool UpdateChecker::canAutoInstall(const Result &result, bool installed, bool windows) {
    return updateauth::installationConfigured() && compatibleInstallerAssets(result, installed, windows) &&
           (updateauth::certificateMode() || signedInstallerAssets(result));
}
bool UpdateChecker::verifyPackage(const QString &filePath, const QString &expectedHash, QString *error) {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    static const QRegularExpression sha256("\\A[0-9a-fA-F]{64}\\z");
    if (!sha256.match(expectedHash).hasMatch())
        return fail(tr("校验失败：安装包已损坏或不完整。"));
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return fail(tr("无法读取已下载的文件。"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return fail(tr("校验文件失败。"));
    const QString actualHash = QString::fromLatin1(hash.result().toHex());
    if (actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0)
        return fail(tr("校验失败：安装包已损坏或不完整。"));
    if (error) error->clear();
    return true;
}
void UpdateChecker::verifyAndInstall(const QString &filePath, const QString &expectedHash, bool isInstaller) {
    QString error;
    if (!updateauth::installationConfigured()) {
        failInstall(tr("未配置可信发布者，程序内安装已停用。请前往发布页手动下载。"));
        return;
    }
    // The confirmation runs a nested event loop. Keep cleanup ownership until
    // after the Windows lock closes, even if cancellation destroys the checker.
    const auto directory = downloadDirectory_;
    const QPointer<UpdateChecker> self(this);
#ifdef Q_OS_WIN
    // Keep the verified executable immutable until CreateProcess has mapped it.
    // Sharing only reads prevents replacing it during the confirmation dialog.
    const auto nativePath = QDir::toNativeSeparators(filePath);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        failInstall(tr("无法读取并锁定已下载的安装包，已停止更新。"));
        return;
    }
    std::unique_ptr<void, decltype(&CloseHandle)> packageLock(handle, &CloseHandle);
#endif
    const auto reject = [self, directory
#ifdef Q_OS_WIN
                         , &packageLock
#endif
                        ](const QString &message) {
#ifdef Q_OS_WIN
        packageLock.reset();
#endif
        if (self && self->installing_ && self->downloadDirectory_ == directory)
            self->failInstall(message);
    };
    if (!updateauth::canonicalVersion(expectedInstallVersion_) ||
        QVersionNumber::compare(QVersionNumber::fromString(expectedInstallVersion_),
                                QVersionNumber::fromString(EDITHERE_VERSION)) <= 0) {
        reject(tr("更新签名无效或与本次安装包不一致，已停止更新。请前往发布页手动下载。"));
        return;
    }
    if (!verifyPackage(filePath, expectedHash, &error)) {
        reject(error);
        return;
    }
    const bool authenticated = updateauth::certificateMode()
        ? updateauth::verifyPublisherSignature(filePath, updateauth::publisherCertificateSha256(), &error)
        : updateauth::verifyProjectSignature(signatureMetadata_, expectedInstallVersion_,
                                             QFileInfo(filePath).fileName(), expectedHash.toLower(), &error);
    if (!authenticated) {
        reject(error);
        return;
    }
    if (!startVerifiedInstaller(filePath, expectedHash, isInstaller, &error))
        reject(error);
}
bool UpdateChecker::startVerifiedInstaller(const QString &filePath, const QString &expectedHash, bool isInstaller,
                                           QString *error) {
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    // Confirmation comes after downloading, before ANY external installer runs.
    const auto directory = downloadDirectory_;
    const QPointer<UpdateChecker> self(this);
    const auto confirmation = confirmInstall_;
    const bool confirmed = confirmation && confirmation();
    if (!self || !installing_ || downloadDirectory_ != directory)
        return false;
    if (!confirmed) {
        return fail(tr("已取消更新，当前工作保持不变。"));
    }
#ifdef Q_OS_WIN
    if (!prepareHandoff(filePath, expectedHash, error))
        return false;
    QProcess installer;
    installer.setProgram(filePath);
    installer.setArguments(isInstaller ? QStringList{"/S", "/UPDATE"}
                                       : QStringList{"/S", "/UPDATE", "/PORTABLE"});
    auto arguments = installer.arguments();
    arguments.append("/HANDOFF=" + handoffId_);
    installer.setArguments(arguments);
    // NSIS requires /D= last and unquoted even with spaces. No command shell is
    // involved, so &, %, Unicode, and parentheses are ordinary path characters.
    installer.setNativeArguments("/D=" + QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    installer.setWorkingDirectory(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    const auto launcher = launchInstaller_;
    const bool launched = launcher ? launcher(installer) : installer.startDetached();
    if (!self || !installing_ || downloadDirectory_ != directory)
        return false;
    if (!launched) {
        return fail(tr("无法启动更新安装器。") + "\n" + installer.errorString());
    }
    // The detached NSIS process still needs this file after our QObject dies.
    downloadDirectory_->setAutoRemove(false);
    awaitingHandoff_ = true;
    handoffDeadline_.start();
    handoffTimer_.start();
    emit installerPreparing();
    if (error) error->clear();
    return true;
#else
    return fail(tr("未找到适合当前系统的更新包，请前往发布页手动下载。"));
#endif
}
} // namespace h2d
