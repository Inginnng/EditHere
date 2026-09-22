#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
namespace h2d {
class UpdateChecker final : public QObject {
    Q_OBJECT
  public:
    enum Status { Current, Available, NewerLocal, Failed };
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
    };
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    static QUrl releasesUrl();
    static Result parseRelease(const QByteArray &bytes, const QString &currentVersion);
    void check();
    bool busy() const {
        return busy_;
    }
    bool downloading() const {
        return downloadReply_ != nullptr;
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
    void requestPublic();
    void finish(Result result);
    void verifyAndInstall(const QString &filePath, const QString &expectedHash, bool isInstaller);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QPointer<QNetworkReply> downloadReply_;
    QPointer<QNetworkReply> hashReply_;
    QProcess process_;
    QTimer timeout_;
    QByteArray output_;
    QString downloadPath_;
    QString downloadFileName_;
    bool busy_ = false;
    bool isInstallerUpdate_ = false;
    Result lastResult_;
};
} // namespace h2d
Q_DECLARE_METATYPE(h2d::UpdateChecker::Status)
