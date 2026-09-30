#include "autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
namespace h2d {
namespace {
QString path() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("autostart/com.edithere.capture.desktop");
}
QString executable() {
    // The mounted AppImage path is transient; register the original AppImage.
    return qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
}
QString quotedExec(QString value) {
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    value.replace('`', "\\`");
    value.replace('$', "\\$");
    value.replace('%', "%%");
    // Desktop-entry string escaping is applied after Exec argument escaping.
    value.replace('\\', "\\\\");
    return '"' + value + '"';
}
}
bool launchAtLoginEnabled(QString *error) {
    if (error) error->clear();
    QFile file(path());
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    return !file.readAll().contains("Hidden=true");
}
bool setLaunchAtLoginEnabled(bool enabled, QString *error) {
    if (error) error->clear();
    if (!enabled) {
        QFile file(path());
        if (!file.exists() || file.remove()) return true;
        if (error) *error = file.errorString();
        return false;
    }
    const QString program = executable();
    if (!QFileInfo(program).isExecutable() || program.contains('\n') || program.contains('\r')) {
        if (error) *error = QCoreApplication::translate("h2d", "启动程序路径无效。");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path()).absolutePath())) {
        if (error) *error = QCoreApplication::translate("h2d", "无法创建开机启动目录。");
        return false;
    }
    QSaveFile file(path());
    const QByteArray data = ("[Desktop Entry]\nType=Application\nName=EditHere\nExec=" + quotedExec(program)
                            + " --autostart\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
QString launchAtLoginNotice() {
    if (!launchAtLoginEnabled()) return {};
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (!file.readAll().contains(("Exec=" + quotedExec(executable()) + " --autostart").toUtf8()))
        return QCoreApplication::translate("h2d", "程序位置已改变，保存开机启动设置可刷新路径。");
    return {};
}
void initializeLaunchAtLoginDetection() {}
bool wasLaunchedAtLogin() { return false; }
}
