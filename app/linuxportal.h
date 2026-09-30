#pragma once
#include <QObject>
#include <QDBusArgument>
#include <QVariantMap>
#include <functional>
struct PortalShortcut { QString id; QVariantMap properties; };
using PortalShortcuts = QList<PortalShortcut>;
Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)
QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &value);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &value);
namespace h2d {
// Qt DBus implements the standard portal wire protocol; requests are subscribed
// before calling the method so a fast backend cannot race our subscription.
class LinuxPortalRequest final : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(uint, QVariantMap)>;
    explicit LinuxPortalRequest(QObject *parent = nullptr);
    void start(const QString &interface, const QString &method, QList<QVariant> arguments,
               QVariantMap options, Callback callback);
    ~LinuxPortalRequest() override;
  private slots:
    void response(uint code, const QVariantMap &results);
  private:
    void complete(uint code, QVariantMap results);
    QString path_;
    Callback callback_;
};
}
