#include "aicredentials.h"
#include <qtkeychain/keychain.h>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QTimer>

namespace h2d {
namespace {
bool validProviderId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
    return pattern.match(id).hasMatch();
}
template<class Job> Job *jobFor(const QString &providerId, QObject *parent) {
    auto *job = new Job(QStringLiteral("EditHere.AI"), parent);
    // Windows' Credential Store backend uses key() as the global target name;
    // unlike the other backends it does not namespace that name by service().
    job->setKey(QStringLiteral("EditHere.AI/provider/") + providerId);
    // A missing system keyring must be reported; never fall back to plaintext.
    job->setInsecureFallback(false);
    return job;
}
}
AiCredentials::AiCredentials(QObject *parent) : QObject(parent) {}
void AiCredentials::load(const QString &providerId) {
    if (!validProviderId(providerId)) {
        QTimer::singleShot(0, this, [this] { emit failed(tr("无法读取密钥：API 提供商标识无效。")); });
        return;
    }
    auto *job = jobFor<QKeychain::ReadPasswordJob>(providerId, this);
    connect(job, &QKeychain::Job::finished, this, [this, job, providerId] {
        if (job->error() == QKeychain::NoError)
            emit loaded(providerId, job->textData());
        else if (job->error() == QKeychain::EntryNotFound)
            emit loaded(providerId, {});
        else
            emit failed(tr("无法读取系统凭据库中的 API Key（错误 %1）。").arg(static_cast<int>(job->error())));
    });
    job->start();
}
void AiCredentials::save(const QString &providerId, const QString &key) {
    if (!validProviderId(providerId) || key.trimmed().isEmpty() || key.size() > 16384 ||
        key.contains(QChar::Null) || key.contains('\r') || key.contains('\n')) {
        QTimer::singleShot(0, this, [this] { emit failed(tr("请输入有效的 API Key 后再保存。")); });
        return;
    }
    // Clicking Save is an explicit write, independent from accepting/cancelling
    // the settings dialog. Keep that job alive if its UI closes before completion.
    auto *job = jobFor<QKeychain::WritePasswordJob>(providerId, QCoreApplication::instance());
    job->setTextData(key.trimmed());
    connect(job, &QKeychain::Job::finished, this, [this, job, providerId] {
        if (job->error() == QKeychain::NoError)
            emit saved(providerId);
        else
            emit failed(tr("API Key 未保存：无法写入系统凭据库（错误 %1）。").arg(static_cast<int>(job->error())));
    });
    job->start();
}
void AiCredentials::remove(const QString &providerId) {
    if (!validProviderId(providerId)) {
        QTimer::singleShot(0, this, [this] { emit failed(tr("无法删除密钥：API 提供商标识无效。")); });
        return;
    }
    auto *job = jobFor<QKeychain::DeletePasswordJob>(providerId, QCoreApplication::instance());
    connect(job, &QKeychain::Job::finished, this, [this, job, providerId] {
        if (job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound)
            emit removed(providerId);
        else
            emit failed(tr("API Key 未删除：无法访问系统凭据库（错误 %1）。").arg(static_cast<int>(job->error())));
    });
    job->start();
}
} // namespace h2d
