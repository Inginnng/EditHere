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
} // namespace
#endif

QString launchAgentDesktop(const QString &executable, qint64 *pid) {
    if (pid) *pid = 0;
#ifdef Q_OS_WIN
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob))
        return windowsError("Cannot inspect the Agent process lifetime", GetLastError(), true);
    DWORD flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    if (inJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
            return windowsError("Cannot inspect the Agent job's launch permissions", GetLastError(), true);
        const auto jobFlags = limits.BasicLimitInformation.LimitFlags;
        if (!(jobFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) {
            if (!(jobFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
                return windowsError("The Agent job does not permit an independent desktop process", ERROR_ACCESS_DENIED, true);
            flags |= CREATE_BREAKAWAY_FROM_JOB;
        }
    }
    const auto path = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    // Pass the executable explicitly; quoting keeps spaces in portable paths
    // from being interpreted as an executable prefix. There are no user args.
    auto command = QStringLiteral("\"") + path + "\" --agent-start";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(reinterpret_cast<const wchar_t *>(path.utf16()),
                        reinterpret_cast<wchar_t *>(command.data()), nullptr, nullptr, FALSE,
                        flags, nullptr, nullptr, &startup, &child))
        return windowsError("The OS could not start EditHere independently", GetLastError(), inJob);

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
    BOOL childInJob = FALSE;
    if (!IsProcessInJob(child.hProcess, nullptr, &childInJob))
        return discard(windowsError("Cannot verify the desktop process lifetime", GetLastError(), true));
    // With nested jobs, a permitted breakaway can remove only an inner job.
    if (childInJob)
        return discard(windowsError("The desktop process is still attached to an Agent job", ERROR_ACCESS_DENIED, true));
    if (ResumeThread(child.hThread) == DWORD(-1))
        return discard(windowsError("Cannot start the suspended desktop process", GetLastError(), true));
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
