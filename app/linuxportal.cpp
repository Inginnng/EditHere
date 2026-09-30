#include "linuxportal.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>
#include <QUuid>
QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &value) {
    arg.beginStructure(); arg << value.id << value.properties; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &value) {
    arg.beginStructure(); arg >> value.id >> value.properties; arg.endStructure(); return arg;
}
namespace h2d {
static constexpr auto service = "org.freedesktop.portal.Desktop";
LinuxPortalRequest::LinuxPortalRequest(QObject *parent) : QObject(parent) {}
LinuxPortalRequest::~LinuxPortalRequest() {
    if (!path_.isEmpty()) {
        auto close = QDBusMessage::createMethodCall(service, path_, "org.freedesktop.portal.Request", "Close");
        QDBusConnection::sessionBus().asyncCall(close);
    }
}
void LinuxPortalRequest::start(const QString &interface, const QString &method, QList<QVariant> arguments,
                               QVariantMap options, Callback callback) {
    callback_ = std::move(callback);
    auto bus = QDBusConnection::sessionBus();
    const QString token = "edithere_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    path_ = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    if (!bus.isConnected() || !bus.connect(service, path_, "org.freedesktop.portal.Request", "Response",
                                           this, SLOT(response(uint,QVariantMap)))) {
        QTimer::singleShot(0, this, [this] { complete(2, {{"error", "Session bus unavailable"}}); });
        return;
    }
    options.insert("handle_token", token);
    arguments.append(options);
    auto call = QDBusMessage::createMethodCall(service, "/org/freedesktop/portal/desktop", interface, method);
    call.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (!callback_) return;
        if (reply.isError()) complete(2, {{"error", reply.error().message()}});
        else if (reply.value().path() != path_) complete(2, {{"error", "Unexpected portal request path"}});
    });
    QTimer::singleShot(120000, this, [this] { complete(2, {{"error", "Portal request timed out"}}); });
}
void LinuxPortalRequest::response(uint code, const QVariantMap &results) { complete(code, results); }
void LinuxPortalRequest::complete(uint code, QVariantMap results) {
    if (!callback_) return;
    QDBusConnection::sessionBus().disconnect(service, path_, "org.freedesktop.portal.Request", "Response",
                                            this, SLOT(response(uint,QVariantMap)));
    auto callback = std::move(callback_);
    callback_ = {};
    callback(code, std::move(results));
    deleteLater();
}
}
