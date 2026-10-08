#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QSaveFile>
#include <QTemporaryDir>
#include <functional>
#include <memory>
#include <optional>
namespace h2d {
class UpdateCheckerTestAccess;
class UpdateChecker final : public QObject {
    Q_OBJECT
  public:
    enum Status { Current, Available, NewerLocal, Failed };
    enum Source { GitHub, Gitee };
    struct Asset {
        QString name;
        QUrl url;
        qint64 size = 0;
    };
    struct Result {
        Status status;
        QString message;
        QUrl url;
        QString tagName;
        Asset installer;
        Asset installerHash;
        Asset portable;
        Asset portableHash;
        bool portableInstallerSupported = false;
    };
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    static QUrl releasesUrl(Source source = GitHub);
    static Result parseRelease(const QByteArray &bytes, const QString &currentVersion,
                               Source source = GitHub);
    static Result parseReleases(const QByteArray &bytes, const QString &currentVersion,
                                Source source = Gitee);
    static Result selectRelease(const Result &github, const Result &gitee);
    // Resolves the temporary path a package is downloaded to and drops whatever
    // is already there. The writer appends, so a leftover file from a previous
    // attempt would be concatenated with the new package and fail the SHA256
    // check with "安装包已损坏或不完整".
    static QString prepareDownloadTarget(const QString &directory, const QString &packageName);
    static bool verifyPackage(const QString &filePath, const QString &expectedHash, QString *error);
    static bool canAutoInstall(const Result &result, bool installed, bool windows);
    void setInstallConfirmation(std::function<bool()> confirmation) { confirmInstall_ = std::move(confirmation); }
    void check();
    bool busy() const {
        return busy_;
    }
    bool downloading() const {
        return installing_;
    }
    const Result & lastResult() const {
        return lastResult_;
    }
    void downloadAndInstall(const Asset &package, const Asset &hashAsset, bool isInstaller);
  signals:
    void finished(h2d::UpdateChecker::Status status, QString message, QUrl url);
    void downloadProgress(qint64 received, qint64 total);
    void installStarted();
    void installFailed(QString message);

  private:
    friend class UpdateCheckerTestAccess;
    void requestPublic();
    void requestGitee();
    void requestSource(Source source);
    void collectSource(Source source, Result result);
    void completeCheck();
    void finish(Result result);
    void verifyAndInstall(const QString &filePath, const QString &expectedHash, bool isInstaller);
    void failInstall(const QString &message);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QPointer<QNetworkReply> giteeReply_;
    QPointer<QNetworkReply> downloadReply_;
    QPointer<QNetworkReply> hashReply_;
    QProcess process_;
    QTimer timeout_;
    QTimer cliTimeout_;
    QByteArray output_;
    QByteArray giteeOutput_;
    std::optional<Result> githubResult_, giteeResult_;
    std::optional<Result> giteeCandidate_;
    int giteePage_ = 1;
    QString downloadPath_;
    QString downloadFileName_;
    bool busy_ = false;
    bool isInstallerUpdate_ = false;
    bool installing_ = false;
    bool publicRequested_ = false;
    std::unique_ptr<QTemporaryDir> downloadDirectory_;
    std::unique_ptr<QSaveFile> downloadFile_;
    std::function<bool()> confirmInstall_;
    Result lastResult_;
};
} // namespace h2d
Q_DECLARE_METATYPE(h2d::UpdateChecker::Status)
