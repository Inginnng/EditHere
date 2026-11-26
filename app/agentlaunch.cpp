#include "agentlaunch.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace h2d {
#ifdef Q_OS_WIN
namespace {
QString windowsError(const QString &action, DWORD code, bool callerJob = false) {
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const auto detail = buffer ? QString::fromWCharArray(buffer).trimmed() : QString();
    if (buffer) LocalFree(buffer);
    auto message = action + QString(" (Windows error %1)").arg(code);
    if (!detail.isEmpty()) message += ": " + detail;
    if (callerJob)
        message += " Open EditHere manually, then retry the Agent request. The desktop must outlive the Agent process.";
    return message;
}

bool createDesktopProcess(const QString &path, DWORD flags, STARTUPINFOW &startup, PROCESS_INFORMATION &child) {
    auto command = QStringLiteral("\"") + path + "\" --agent-start";
    return CreateProcessW(reinterpret_cast<const wchar_t *>(path.utf16()),
                          reinterpret_cast<wchar_t *>(command.data()), nullptr, nullptr, FALSE,
                          flags | CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT,
                          nullptr, nullptr, &startup, &child);
}

bool createFromExplorer(const QString &path, PROCESS_INFORMATION &child) {
    const auto shell = GetShellWindow();
    DWORD shellPid = 0;
    if (!shell || !GetWindowThreadProcessId(shell, &shellPid))
        return false;
    auto parent = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, shellPid);
    if (!parent)
        return false;

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    QByteArray storage(qsizetype(size), Qt::Uninitialized);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool created = false;
    if (InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) {
        if (UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                                      &parent, sizeof(parent), nullptr, nullptr))
            created = createDesktopProcess(path, EXTENDED_STARTUPINFO_PRESENT, startup.StartupInfo, child);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }
    CloseHandle(parent);
    return created;
}
} // namespace
#endif

QString launchAgentDesktop(const QString &executable, qint64 *pid) {
    if (pid) *pid = 0;
#ifdef Q_OS_WIN
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob))
        return windowsError("Cannot inspect the Agent process lifetime", GetLastError(), true);
    DWORD flags = 0;
    QString error;
    if (inJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
            return windowsError("Cannot inspect the Agent job's launch permissions", GetLastError(), true);
        const auto jobFlags = limits.BasicLimitInformation.LimitFlags;
        if (!(jobFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) {
            if (!(jobFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
                error = windowsError("The Agent job does not permit an independent desktop process", ERROR_ACCESS_DENIED, true);
            else
                flags |= CREATE_BREAKAWAY_FROM_JOB;
        }
    }
    const auto path = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    PROCESS_INFORMATION child{};
    auto discard = [&child](QString error) {
        // Only the newly created, still suspended process belongs to this
        // cleanup. Never let its GUI execute while attached to a caller job.
        if (!TerminateProcess(child.hProcess, ERROR_PROCESS_ABORTED))
            error += " " + windowsError("Cannot stop the suspended desktop process", GetLastError());
        else
            WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        return error;
    };
    const auto verify = [&child] {
        BOOL childInJob = FALSE;
        if (!IsProcessInJob(child.hProcess, nullptr, &childInJob))
            return windowsError("Cannot verify the desktop process lifetime", GetLastError(), true);
        if (childInJob)
            return windowsError("The desktop process is still attached to an Agent job", ERROR_ACCESS_DENIED, true);
        return QString();
    };
    if (error.isEmpty()) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        if (createDesktopProcess(path, flags, startup, child)) {
            error = verify();
            if (!error.isEmpty())
                error = discard(error);
        } else {
            error = windowsError("The OS could not start EditHere independently", GetLastError(), inJob);
        }
    }
    // A nested job can deny breakaway or leave the child attached to an outer job.
    if (!error.isEmpty()) {
        if (!inJob || !createFromExplorer(path, child))
            return error;
        if (!verify().isEmpty())
            return discard(error);
    }
    if (ResumeThread(child.hThread) == DWORD(-1))
        return discard(error.isEmpty() ? windowsError("Cannot start the suspended desktop process", GetLastError(), true)
                                       : error);
    if (pid) *pid = child.dwProcessId;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return {};
#else
    QProcess process;
    process.setProgram(executable);
    process.setArguments({"--agent-start"});
    if (!process.startDetached(pid))
        return "The OS could not start EditHere: " + process.errorString();
    return {};
#endif
}
} // namespace h2d
