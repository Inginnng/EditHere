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
    struct Result {
        Status status;
        QString message;
        QUrl url;
    };
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    static QUrl releasesUrl();
    static Result parseRelease(const QByteArray &bytes, const QString &currentVersion);
    void check();
    bool busy() const {
        return busy_;
    }
  signals:
    void finished(h2d::UpdateChecker::Status status, QString message, QUrl url);

  private:
    void requestPublic();
    void finish(Result result);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QProcess process_;
    QTimer timeout_;
    QByteArray output_;
    bool busy_ = false;
};
} // namespace h2d
Q_DECLARE_METATYPE(h2d::UpdateChecker::Status)
