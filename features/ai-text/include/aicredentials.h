#pragma once
#include <QObject>
#include <QString>

namespace h2d {
// The operating system owns the secret. No API key is serialized in AppSettings,
// project documents, feedback, diagnostics, or the provider preset resource.
class AiCredentials : public QObject {
    Q_OBJECT
  public:
    explicit AiCredentials(QObject *parent = nullptr);
    virtual void load(const QString &providerId);
    virtual void save(const QString &providerId, const QString &key);
    virtual void remove(const QString &providerId);
  signals:
    void loaded(const QString &providerId, const QString &key);
    void saved(const QString &providerId);
    void removed(const QString &providerId);
    void failed(const QString &message);
};
} // namespace h2d
