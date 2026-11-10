#pragma once
#include "aiconfig.h"
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

class QNetworkReply;
namespace h2d {
struct AiFeedbackInput {
    QJsonObject feedback;
    QList<QImage> images;
    QStringList imageLabels;
};
// Screenshots and geometric edits are evidence for the interpreter. The returned
// requirements must identify the objects and desired relationships using text alone.
class AiFeedbackClient final : public QObject {
    Q_OBJECT
  public:
    explicit AiFeedbackClient(QObject *parent = nullptr);
    ~AiFeedbackClient() override;
    void generate(const AiConnectionSettings &, const QString &apiKey, const AiFeedbackInput &);
    void testConnection(const AiConnectionSettings &, const QString &apiKey);
    void cancel();
    bool isRunning() const { return !reply_.isNull(); }
    // A local development host can replay responses through its own endpoint.
    void setEndpointOverride(const QUrl &url) { if (!isRunning()) endpointOverride_ = url; }
  signals:
    void completed(const QString &text);
    void failed(const QString &message);
    void cancelled();
    void usageAvailable(qint64 inputTokens, qint64 outputTokens);
  private:
    void start(const AiConnectionSettings &, const QString &, const AiFeedbackInput &, bool test);
    void finish();
    void fail(const QString &message);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_;
    QUrl endpointOverride_;
    QByteArray response_;
    AiConnectionSettings connection_;
    QJsonArray facts_;
    QString context_;
    QString key_;
    QString terminalError_;
    bool testing_ = false;
    bool cancelled_ = false;
};
} // namespace h2d
