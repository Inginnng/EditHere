#include "updatechecker.h"
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
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasFragment())
        return false;
    if (source == UpdateChecker::Gitee)
        return url.host() == "gitee.com" && url.path().startsWith("/InnGing/EditHere/releases/download/");
    return url.host() == "github.com" || url.host() == "objects.githubusercontent.com";
}
bool isValidAssetUrl(const QUrl &url) {
    return isSourceAssetUrl(url, UpdateChecker::GitHub) || isSourceAssetUrl(url, UpdateChecker::Gitee);
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
    static const QRegularExpression versionPattern("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$");
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
    if (!match.hasMatch() || !localMatch.hasMatch() || invalidDraft ||
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
    const auto pickAsset = [&assets, source](const QStringList &candidates) {
        Asset found;
        for (const QString &candidate : candidates) {
            for (const auto &item : assets) {
                const auto obj = item.toObject();
                if (obj.value("name").toString() != candidate)
                    continue;
                const QUrl assetUrl(obj.value("browser_download_url").toString());
                if (!isSourceAssetUrl(assetUrl, source))
                    continue;
                found = {candidate, assetUrl, obj.value("size").toVariant().toLongLong()};
                return found;
            }
        }
        return found;
    };
    const QStringList installerNames{QString("EditHere-%1-win-x64-setup.exe").arg(ver),
                                     QStringLiteral("EditHere-win-x64-setup.exe")};
    const QStringList portableNames{QString("EditHere-%1-win-x64.zip").arg(ver),
                                    QStringLiteral("EditHere-win-x64.zip")};
    const Asset installer = pickAsset(installerNames);
    const Asset installerHash = installer.name.isEmpty() ? Asset{} : pickAsset({installer.name + ".sha256"});
    const Asset portable = pickAsset(portableNames);
    const Asset portableHash = portable.name.isEmpty() ? Asset{} : pickAsset({portable.name + ".sha256"});
    const int order = QVersionNumber::compare(remote, local);
    if (order > 0)
        return {Available, tr("发现新版本 %1。").arg(tag), url,
                tag, installer, installerHash, portable, portableHash,
                QVersionNumber::compare(remote, QVersionNumber(0, 9, 9)) >= 0};
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
             canAutoInstall(result, true, true) && !canAutoInstall(*newest, true, true)))
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
    const bool githubComplete = canAutoInstall(github, true, true);
    const bool giteeComplete = canAutoInstall(gitee, true, true);
    return githubComplete && !giteeComplete ? github : gitee;
}
UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {
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
    busy_ = false;
    process_.disconnect(this);
    for (auto *pointer : {&reply_, &giteeReply_, &downloadReply_, &hashReply_}) {
        if (*pointer) {
            auto *reply = pointer->data();
            *pointer = nullptr;
            reply->disconnect(this);
            reply->abort();
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
              canAutoInstall(page, true, true) && !canAutoInstall(*giteeCandidate_, true, true))))
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
void UpdateChecker::downloadAndInstall(const Asset &package, const Asset &hashAsset, bool isInstaller) {
    if (busy_ || installing_)
        return;
#ifndef Q_OS_WIN
    emit installFailed(tr("未找到适合当前系统的更新包，请前往发布页手动下载。"));
    return;
#endif
    if (!isValidAssetUrl(package.url) || !isValidAssetUrl(hashAsset.url) ||
        !package.name.endsWith("-win-x64-setup.exe") ||
        QFileInfo(package.name).fileName() != package.name ||
        hashAsset.name != package.name + ".sha256") {
        emit installFailed(tr("未找到校验文件，无法验证安装包完整性。"));
        return;
    }
    installing_ = true;
    isInstallerUpdate_ = isInstaller;
    downloadFileName_ = package.name;
    // Qt supplies unique paths and atomic writes; retries cannot concatenate an
    // earlier download or overwrite another running installer's payload.
    downloadDirectory_ = std::make_unique<QTemporaryDir>(
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
    downloadReply_ = network_.get(request);
    connect(downloadReply_, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(downloadReply_, &QNetworkReply::readyRead, this, [this] {
        if (!downloadReply_)
            return;
        const auto bytes = downloadReply_->readAll();
        if (downloadFile_->write(bytes) != bytes.size())
            failInstall(tr("无法写入更新文件：%1").arg(downloadFile_->errorString()));
    });
    connect(downloadReply_, &QNetworkReply::finished, this, [this, hashAsset] {
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
        QNetworkRequest hashRequest(hashAsset.url);
        hashRequest.setTransferTimeout(15000);
        hashRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        hashReply_ = network_.get(hashRequest);
        hashReply_->setReadBufferSize(4097);
        connect(hashReply_, &QNetworkReply::readyRead, this, [this] {
            if (hashReply_ && hashReply_->bytesAvailable() > 4096)
                failInstall(tr("校验失败：安装包已损坏或不完整。"));
        });
        connect(hashReply_, &QNetworkReply::finished, this, [this] {
            if (!hashReply_)
                return;
            const auto reply = hashReply_.data();
            hashReply_ = nullptr;
            if (reply->error() != QNetworkReply::NoError) {
                reply->deleteLater();
                failInstall(tr("无法下载校验文件，请检查网络后重试。"));
                return;
            }
            const QByteArray hashData = reply->readAll();
            reply->deleteLater();
            // Parse the hash: first whitespace-delimited token.
            const QString expectedHash = QString::fromUtf8(hashData).trimmed().section(QRegularExpression("\\s+"), 0, 0);
            verifyAndInstall(downloadPath_, expectedHash, isInstallerUpdate_);
        });
    });
}
void UpdateChecker::failInstall(const QString &message) {
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
bool UpdateChecker::canAutoInstall(const Result &result, bool installed, bool windows) {
    return windows && result.status == Available && (installed || result.portableInstallerSupported) &&
           isValidAssetUrl(result.installer.url) && isValidAssetUrl(result.installerHash.url) &&
           result.installerHash.name == result.installer.name + ".sha256";
}
bool UpdateChecker::verifyPackage(const QString &filePath, const QString &expectedHash, QString *error) {
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    static const QRegularExpression sha256("^[0-9a-fA-F]{64}$");
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
    if (!verifyPackage(filePath, expectedHash, &error)) {
        failInstall(error);
        return;
    }
    // Confirmation comes after downloading, before ANY external installer runs.
    if (!confirmInstall_ || !confirmInstall_()) {
        failInstall(tr("已取消更新，当前工作保持不变。"));
        return;
    }
#ifdef Q_OS_WIN
    QProcess installer;
    installer.setProgram(filePath);
    installer.setArguments(isInstaller ? QStringList{"/S", "/UPDATE"}
                                       : QStringList{"/S", "/UPDATE", "/PORTABLE"});
    // NSIS requires /D= last and unquoted even with spaces. No command shell is
    // involved, so &, %, Unicode, and parentheses are ordinary path characters.
    installer.setNativeArguments("/D=" + QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    installer.setWorkingDirectory(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    if (!installer.startDetached()) {
        failInstall(tr("无法启动更新安装器。") + "\n" + installer.errorString());
        return;
    }
    // The detached NSIS process still needs this file after our QObject dies.
    downloadDirectory_->setAutoRemove(false);
    emit installStarted();
#else
    failInstall(tr("未找到适合当前系统的更新包，请前往发布页手动下载。"));
#endif
}
} // namespace h2d
