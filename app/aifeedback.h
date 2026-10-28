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
#include <QVector>

class QNetworkReply;
namespace h2d {
struct AiFeedbackInput {
    QJsonObject feedback;
    QList<QImage> images;
    QStringList imageLabels;
};
// The feedback JSON supplies exact numbers and user quotations. Images must be
// provided explicitly; the client never follows paths or URLs from task data.
class AiFeedbackClient final : public QObject {
    Q_OBJECT
  public:
    explicit AiFeedbackClient(QObject *parent = nullptr);
    ~AiFeedbackClient() override;
    void generate(const AiConnectionSettings &, const QString &apiKey, const AiFeedbackInput &);
    void testConnection(const AiConnectionSettings &, const QString &apiKey);
    void cancel();
    bool isRunning() const { return !reply_.isNull(); }
  signals:
    void completed(const QString &text);
    void failed(const QString &message);
    void usageAvailable(qint64 inputTokens, qint64 outputTokens);
  private:
    void start(const AiConnectionSettings &, const QString &, const AiFeedbackInput &, bool test);
    void finish();
    void fail(const QString &message);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_;
    QByteArray response_;
    AiConnectionSettings connection_;
    QJsonArray facts_;
    QString context_;
    QString key_;
    QString terminalError_;
    bool testing_ = false;
};
} // namespace h2d
