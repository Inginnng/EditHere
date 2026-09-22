#include "updatechecker.h"
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
#include <QTemporaryFile>
#include <QVersionNumber>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace h2d {
namespace {
constexpr int maximumResponse = 1024 * 1024;
UpdateChecker::Result failure(const QString &message) {
    return {UpdateChecker::Failed, message, UpdateChecker::releasesUrl(), {}, {}, {}, {}, {}};
}
bool isValidAssetUrl(const QUrl &url) {
    return url.scheme() == "https" &&
           (url.host() == "github.com" || url.host() == "objects.githubusercontent.com") &&
           url.userInfo().isEmpty() && url.port(-1) == -1;
}
} // namespace
QUrl UpdateChecker::releasesUrl() {
    return QUrl("https://github.com/Inginnng/EditHere/releases");
}
UpdateChecker::Result UpdateChecker::parseRelease(const QByteArray &bytes, const QString &currentVersion) {
    if (bytes.size() > maximumResponse)
        return failure("更新信息过大，请稍后重试。");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return failure("无法读取更新信息，请稍后重试。");
    const auto object = document.object();
    const auto tag = object.value("tag_name").toString();
    static const QRegularExpression versionPattern("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$");
    const auto match = versionPattern.match(tag);
    const auto localMatch = versionPattern.match(currentVersion);
    const QUrl url(object.value("html_url").toString());
    if (!match.hasMatch() || !localMatch.hasMatch() || !object.value("draft").isBool() ||
        !object.value("prerelease").isBool() || object.value("draft").toBool() ||
        object.value("prerelease").toBool() || url.scheme() != "https" || url.host() != "github.com" ||
        !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasQuery() || url.hasFragment() ||
        url.path() != "/Inginnng/EditHere/releases/tag/" + tag)
        return failure("发行信息不是有效的 EditHere 正式版本，请到发布页查看。");
    const auto remote = QVersionNumber::fromString(match.captured(1));
    const auto local = QVersionNumber::fromString(localMatch.captured(1));
    if (remote.segmentCount() != 3 || local.segmentCount() != 3)
        return failure("版本号无法比较，请到发布页查看。");
    const QString ver = match.captured(1);
    const QString installerName = QString("EditHere-%1-win-x64-setup.exe").arg(ver);
    const QString portableName = QString("EditHere-%1-win-x64.zip").arg(ver);
    Asset installer, installerHash, portable, portableHash;
    const auto assets = object.value("assets").toArray();
    for (const auto &item : assets) {
        const auto obj = item.toObject();
        const QString name = obj.value("name").toString();
        const QUrl assetUrl(obj.value("browser_download_url").toString());
        if (!isValidAssetUrl(assetUrl))
            continue;
        const qint64 size = obj.value("size").toVariant().toLongLong();
        if (name == installerName)
            installer = {name, assetUrl, size};
        else if (name == installerName + ".sha256")
            installerHash = {name, assetUrl, size};
        else if (name == portableName)
            portable = {name, assetUrl, size};
        else if (name == portableName + ".sha256")
            portableHash = {name, assetUrl, size};
    }
    const int order = QVersionNumber::compare(remote, local);
    if (order > 0)
        return {Available, QString("发现新版本 %1，点击立即更新自动下载安装。").arg(tag), url,
                tag, installer, installerHash, portable, portableHash};
    if (order < 0)
        return {NewerLocal, QString("当前版本高于已发布的正式版 %1。").arg(tag), url,
                tag, {}, {}, {}, {}};
    return {Current, QString("已是最新正式版 %1。").arg(tag), url,
            tag, {}, {}, {}, {}};
}
UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(15000);
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(&timeout_, &QTimer::timeout, this, [this] { finish(failure("检查超时，请检查网络后重试。")); });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (!busy_)
            return;
        output_ += process_.readAllStandardOutput();
        if (output_.size() > maximumResponse)
            finish(failure("更新信息过大，请稍后重试。"));
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && error == QProcess::FailedToStart)
            requestPublic();
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (!busy_)
                    return;
                output_ += process_.readAllStandardOutput();
                if (code == 0 && status == QProcess::NormalExit)
                    finish(parseRelease(output_, QStringLiteral(EDITHERE_VERSION)));
                else
                    requestPublic();
            });
}
UpdateChecker::~UpdateChecker() {
    busy_ = false;
    process_.disconnect(this);
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (downloadReply_) {
        downloadReply_->disconnect(this);
        downloadReply_->abort();
    }
    if (hashReply_) {
        hashReply_->disconnect(this);
        hashReply_->abort();
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}
void UpdateChecker::check() {
    if (busy_)
        return;
    if (process_.state() != QProcess::NotRunning) {
        QTimer::singleShot(100, this, &UpdateChecker::check);
        return;
    }
    busy_ = true;
    output_.clear();
    timeout_.start();
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
}
void UpdateChecker::requestPublic() {
    if (!busy_ || reply_)
        return;
    QNetworkRequest request(QUrl("https://api.github.com/repos/Inginnng/EditHere/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "EditHere/" EDITHERE_VERSION);
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    output_.clear();
    reply_ = network_.get(request);
    reply_->setReadBufferSize(maximumResponse + 1);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!busy_ || !reply_)
            return;
        output_ += reply_->readAll();
        if (output_.size() > maximumResponse)
            finish(failure("更新信息过大，请稍后重试。"));
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        if (!busy_ || !reply_)
            return;
        const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404 || status == 401 || status == 403)
            finish(failure("无法访问发行版：仓库可能为私有、尚未发布或达到访问限制。请登录有权限的 GitHub "
                           "账号查看发布页。"));
        else if (status != 200 || reply_->error() != QNetworkReply::NoError)
            finish(failure("网络连接失败，暂时无法判断是否有更新。请稍后重试。"));
        else {
            output_ += reply_->readAll();
            finish(parseRelease(output_, QStringLiteral(EDITHERE_VERSION)));
        }
    });
}
void UpdateChecker::finish(Result result) {
    if (!busy_)
        return;
    busy_ = false;
    timeout_.stop();
    if (reply_) {
        auto reply = reply_.data();
        reply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (process_.state() != QProcess::NotRunning)
        process_.kill();
    lastResult_ = result;
    emit finished(result.status, result.message, result.url);
}
void UpdateChecker::downloadAndInstall(const Asset &package, const Asset &hashAsset, bool isInstaller) {
    if (downloadReply_ || hashReply_)
        return;
    isInstallerUpdate_ = isInstaller;
    downloadFileName_ = package.name;
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    downloadPath_ = QDir(tempDir).filePath("EditHere-update-" + package.name);
    // Remove any leftover download (e.g. a previous attempt that failed or was
    // interrupted): the writer below appends, and a stale file would corrupt
    // the package and fail the SHA256 check.
    QFile::remove(downloadPath_);
    // Download the package.
    QNetworkRequest request(package.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    downloadReply_ = network_.get(request);
    connect(downloadReply_, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(downloadReply_, &QNetworkReply::readyRead, this, [this] {
        if (!downloadReply_)
            return;
        QFile f(downloadPath_);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            f.write(downloadReply_->readAll());
            f.close();
        }
    });
    connect(downloadReply_, &QNetworkReply::finished, this, [this, hashAsset] {
        if (!downloadReply_)
            return;
        const auto reply = downloadReply_.data();
        downloadReply_ = nullptr;
        reply->disconnect(this);
        if (reply->error() != QNetworkReply::NoError) {
            reply->abort();
            reply->deleteLater();
            QFile::remove(downloadPath_);
            emit installFailed("下载失败，请检查网络后重试。");
            return;
        }
        reply->deleteLater();
        // Download the hash file.
        if (!hashAsset.url.isValid()) {
            emit installFailed("未找到校验文件，无法验证安装包完整性。");
            QFile::remove(downloadPath_);
            return;
        }
        QNetworkRequest hashRequest(hashAsset.url);
        hashRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        hashReply_ = network_.get(hashRequest);
        connect(hashReply_, &QNetworkReply::finished, this, [this] {
            if (!hashReply_)
                return;
            const auto reply = hashReply_.data();
            hashReply_ = nullptr;
            if (reply->error() != QNetworkReply::NoError) {
                reply->abort();
                reply->deleteLater();
                QFile::remove(downloadPath_);
                emit installFailed("无法下载校验文件，请检查网络后重试。");
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
void UpdateChecker::verifyAndInstall(const QString &filePath, const QString &expectedHash, bool isInstaller) {
    // Compute SHA256 of the downloaded file.
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit installFailed("无法读取已下载的文件。");
        QFile::remove(filePath);
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        emit installFailed("校验文件失败。");
        QFile::remove(filePath);
        return;
    }
    f.close();
    const QString actualHash = QString::fromLatin1(hash.result().toHex());
    if (!expectedHash.isEmpty() && actualHash.compare(expectedHash, Qt::CaseInsensitive) != 0) {
        emit installFailed("校验失败：安装包已损坏或不完整。");
        QFile::remove(filePath);
        return;
    }
    if (isInstaller) {
        // Launch NSIS installer silently: /S = silent, /UPDATE = skip pages, /RESTART = restart app.
        QProcess::startDetached(filePath, {"/S", "/UPDATE", "/RESTART"});
        emit installStarted();
    } else {
        // Portable ZIP update: generate a batch script to replace files after the app exits.
        const QString appDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
        const QString batPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                    .filePath("_edithere_update.bat");
        QFile bat(batPath);
        if (!bat.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit installFailed("无法创建更新脚本。");
            QFile::remove(filePath);
            return;
        }
        // The batch waits for EditHere to exit, extracts the ZIP, restarts, and self-deletes.
        // Using tar (built into Windows 10+) to extract ZIP.
        const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        bat.write(QString(
            "@echo off\r\n"
            "setlocal\r\n"
            "set APPDIR=%1\r\n"
            "set ZIPPATH=%2\r\n"
            "set EXE=%3\r\n"
            ":: Wait for EditHere to exit\r\n"
            ":wait\r\n"
            "tasklist /fi \"imagename eq %EXE%\" | find /i \"%EXE%\" >nul\r\n"
            "if not errorlevel 1 (\r\n"
            "  timeout /t 1 /nobreak >nul\r\n"
            "  goto wait\r\n"
            ")\r\n"
            ":: Extract ZIP over existing files\r\n"
            "tar -xf \"%ZIPPATH%\" -C \"%APPDIR%\"\r\n"
            ":: Restart EditHere\r\n"
            "start \"\" \"%APPDIR%\\%EXE%\"\r\n"
            ":: Self-delete\r\n"
            "del \"%~f0\"\r\n"
        ).arg(appDir, filePath, exeName).toUtf8());
        bat.close();
        QProcess::startDetached("cmd", {"/c", batPath});
        emit installStarted();
    }
}
} // namespace h2d
