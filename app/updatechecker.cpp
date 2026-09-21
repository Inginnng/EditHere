#include "updatechecker.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace h2d {
namespace {
constexpr int maximumResponse = 1024 * 1024;
UpdateChecker::Result failure(const QString &message) {
    return {UpdateChecker::Failed, message, UpdateChecker::releasesUrl()};
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
    const int order = QVersionNumber::compare(remote, local);
    if (order > 0)
        return {Available, QString("发现新版本 %1，可以前往发布页下载。").arg(tag), url};
    if (order < 0)
        return {NewerLocal, QString("当前版本高于已发布的正式版 %1。").arg(tag), url};
    return {Current, QString("已是最新正式版 %1。").arg(tag), url};
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
    emit finished(result.status, result.message, result.url);
}
} // namespace h2d
