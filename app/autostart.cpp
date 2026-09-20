#include "autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QtEndian>
#include <QStringList>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#endif

namespace h2d {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
void clearError(QString *error) {
    if (error)
        error->clear();
}
QString windowsPath(QString path) {
    return path.replace('/', '\\');
}
QString formattedCommand(const QString &path) {
    return QString("\"%1\" --autostart").arg(windowsPath(path));
}

#ifdef Q_OS_WIN
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"HelpDesign";
constexpr wchar_t StartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
bool registryFailure(QString *error, const QString &action, LSTATUS status) {
    return fail(error, QString("无法%1当前用户的开机自启项（Windows 错误 %2）。请检查用户权限或系统策略。")
                           .arg(action).arg(status));
}
class WindowsRunValueStore final : public autostart_detail::RunValueStore {
  public:
    autostart_detail::StartupApproval approval(QString *error) {
        using autostart_detail::StartupApproval;
        HKEY key = nullptr;
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, StartupApprovedKey, 0, KEY_QUERY_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND)
            return StartupApproval::NotRecorded;
        if (opened != ERROR_SUCCESS) {
            registryFailure(error, "读取启动许可状态", opened);
            return StartupApproval::Unknown;
        }
        BYTE data[12] = {};
        DWORD bytes = sizeof(data);
        const auto status = RegGetValueW(key, nullptr, RunValue, RRF_RT_REG_BINARY, nullptr, data, &bytes);
        RegCloseKey(key);
        if (status == ERROR_FILE_NOT_FOUND)
            return StartupApproval::NotRecorded;
        // This is Windows-owned state. Never reset it to bypass a user's Task Manager choice.
        if (status == ERROR_UNSUPPORTED_TYPE || status == ERROR_MORE_DATA)
            return StartupApproval::Unknown;
        if (status != ERROR_SUCCESS) {
            registryFailure(error, "读取启动许可状态", status);
            return StartupApproval::Unknown;
        }
        return autostart_detail::windowsStartupApproval(
            QByteArray(reinterpret_cast<const char *>(data), qsizetype(bytes)));
    }
    bool read(QString *command, QString *error) override {
        command->clear();
        HKEY key = nullptr;
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_QUERY_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND)
            return true;
        if (opened != ERROR_SUCCESS)
            return registryFailure(error, "读取", opened);
        DWORD bytes = 0;
        auto status = RegGetValueW(key, nullptr, RunValue, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
        if (status == ERROR_FILE_NOT_FOUND) {
            RegCloseKey(key);
            return true;
        }
        // A value of another type is not our valid registration, but can still be replaced/removed.
        if (status == ERROR_UNSUPPORTED_TYPE) {
            RegCloseKey(key);
            return true;
        }
        if (status != ERROR_SUCCESS) {
            RegCloseKey(key);
            return registryFailure(error, "读取", status);
        }
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
        status = RegGetValueW(key, nullptr, RunValue, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS)
            return registryFailure(error, "读取", status);
        *command = QString::fromWCharArray(buffer.data());
        return true;
    }
    bool write(const QString &command, QString *error) override {
        HKEY key = nullptr;
        const auto opened = RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE,
                                           nullptr, &key, nullptr);
        if (opened != ERROR_SUCCESS)
            return registryFailure(error, "写入", opened);
        const auto status = RegSetValueExW(key, RunValue, 0, REG_SZ,
                                          reinterpret_cast<const BYTE *>(command.utf16()),
                                          DWORD((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return status == ERROR_SUCCESS || registryFailure(error, "写入", status);
    }
    bool remove(QString *error) override {
        HKEY key = nullptr;
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_SET_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND)
            return true;
        if (opened != ERROR_SUCCESS)
            return registryFailure(error, "移除", opened);
        // Delete only our named value, never the Run key or a similarly named subkey.
        const auto status = RegDeleteValueW(key, RunValue);
        RegCloseKey(key);
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ||
               registryFailure(error, "移除", status);
    }
};
#endif
} // namespace

namespace autostart_detail {
StartupApproval windowsStartupApproval(const QByteArray &value) {
    // StartupApproved is not a public Windows API. Recognize only known 12-byte records
    // and report other layouts/states as unknown instead of assuming launch is allowed.
    if (value.size() != 12)
        return StartupApproval::Unknown;
    switch (qFromLittleEndian<quint32>(value.constData())) {
    case 2:
    case 6:
        return StartupApproval::Enabled;
    case 3:
    case 7:
        return StartupApproval::Disabled;
    default:
        return StartupApproval::Unknown;
    }
}

QString windowsLaunchAtLoginNotice(const QString &command, const QString &executablePath,
                                  bool registeredExecutableExists, StartupApproval approval) {
    if (command.isEmpty())
        return {};
    QStringList notices;
    if (!registeredExecutableExists)
        notices << "开机自启记录中的程序已不存在或路径无效。保持勾选并保存，即可修复为当前程序位置。";
    else if (command.compare(formattedCommand(executablePath), Qt::CaseInsensitive) != 0)
        notices << "开机自启项指向其他位置或使用旧的启动参数。保持勾选并保存，即可更新为当前程序。";
    if (approval == StartupApproval::Disabled)
        notices << "Windows 已禁用此启动项。请在 Windows 设置 → 应用 → 启动中启用 EditHere（旧版可能显示 HelpDesign）；仅在此处保存不会解除系统禁用。";
    else if (approval == StartupApproval::Unknown)
        notices << "无法确认 Windows 启动项的许可状态，请在 Windows 设置 → 应用 → 启动中检查 EditHere（旧版可能显示 HelpDesign）。";
    return notices.join('\n');
}

QString windowsLaunchCommand(const QString &executablePath, QString *error) {
    clearError(error);
    const auto path = windowsPath(executablePath);
    const bool drive = path.size() >= 4 &&
                       ((path[0] >= u'A' && path[0] <= u'Z') || (path[0] >= u'a' && path[0] <= u'z')) &&
                       path[1] == u':' && path[2] == u'\\';
    const auto components = path.mid(2).split('\\', Qt::SkipEmptyParts);
    const bool unc = path.startsWith("\\\\") && components.size() >= 3;
    const bool invalid = path.contains(u'"') || path.contains(QChar::Null) || path.contains(u'\r') ||
                         path.contains(u'\n') || path.endsWith(u'\\') || path.startsWith("\\\\?\\") ||
                         path.startsWith("\\\\.\\");
    if ((!drive && !unc) || invalid) {
        fail(error, "无法设置开机自启：应用路径必须是有效的完整 Windows 可执行文件路径。");
        return {};
    }
    const auto command = formattedCommand(path);
    // Microsoft documents a 260-character limit for each Run command, including arguments.
    if (command.size() > 260) {
        fail(error, "无法设置开机自启：应用路径过长，包含启动参数的命令不能超过 260 个字符。请将应用移到较短的路径。");
        return {};
    }
    return command;
}

bool windowsLaunchAtLoginEnabled(RunValueStore &store, const QString &executablePath, QString *error) {
    clearError(error);
    QString command;
    if (!store.read(&command, error))
        return false;
    // A moved executable must not hide an existing registration: users still need to disable it.
    Q_UNUSED(executablePath);
    return !command.isEmpty();
}

bool setWindowsLaunchAtLoginEnabled(RunValueStore &store, const QString &executablePath, bool enabled,
                                   QString *error) {
    clearError(error);
    if (!enabled)
        return store.remove(error);
    const auto command = windowsLaunchCommand(executablePath, error);
    if (command.isEmpty())
        return false;
    QString previous;
    if (!store.read(&previous, error))
        return false;
    if (previous == command)
        return true;
    return store.write(command, error);
}
} // namespace autostart_detail

#ifdef Q_OS_WIN
bool launchAtLoginEnabled(QString *error) {
    WindowsRunValueStore store;
    return autostart_detail::windowsLaunchAtLoginEnabled(store, QCoreApplication::applicationFilePath(), error);
}
bool setLaunchAtLoginEnabled(bool enabled, QString *error) {
    WindowsRunValueStore store;
    return autostart_detail::setWindowsLaunchAtLoginEnabled(store, QCoreApplication::applicationFilePath(), enabled,
                                                         error);
}
QString launchAtLoginNotice() {
    WindowsRunValueStore store;
    QString command, error;
    if (!store.read(&command, &error))
        return error;
    if (command.isEmpty())
        return {};
    const auto parts = QProcess::splitCommand(command);
    const bool executableExists = !parts.isEmpty() && QFileInfo(parts.front()).isFile();
    const auto approval = store.approval(&error);
    auto notice = autostart_detail::windowsLaunchAtLoginNotice(
        command, QCoreApplication::applicationFilePath(), executableExists, approval);
    if (!error.isEmpty())
        notice += (notice.isEmpty() ? QString() : QString("\n")) + error;
    return notice;
}
void initializeLaunchAtLoginDetection() {}
bool wasLaunchedAtLogin() {
    return false;
}
#endif
} // namespace h2d
