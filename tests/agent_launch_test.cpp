#include "agentlaunch.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace h2d;
namespace {
class OwnedHandle {
  public:
    OwnedHandle() = default;
    explicit OwnedHandle(HANDLE handle) : value(handle) {}
    OwnedHandle(const OwnedHandle &) = delete;
    OwnedHandle &operator=(const OwnedHandle &) = delete;
    ~OwnedHandle() { reset(); }
    void reset(HANDLE handle = nullptr) {
        if (value) CloseHandle(value);
        value = handle;
    }
    HANDLE value = nullptr;
};
class OwnedProcess : public OwnedHandle {
  public:
    ~OwnedProcess() {
        if (value && WaitForSingleObject(value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(value, 0);
            WaitForSingleObject(value, 5000);
        }
    }
};
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString fixtureError(const char *action) {
    return QString("%1 (Windows error %2)").arg(action).arg(GetLastError());
}
bool inJob(HANDLE process) {
    BOOL contained = FALSE;
    return IsProcessInJob(process, nullptr, &contained) && contained;
}

// Positive lifetime tests need a host outside test runner jobs. Follow their
// permissions and skip those rows when isolation is unavailable. Negative
// controls can inherit runner jobs and add our own nested fixture jobs safely.
QString createHost(const QString &directory, bool legacy, bool independent, PROCESS_INFORMATION &process) {
    DWORD flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    BOOL contained = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &contained)) return fixtureError("Inspect test runner job");
    if (independent && contained) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
            return fixtureError("Inspect test runner job permissions");
        if (!(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) {
            if (!(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK))
                return "The test runner job prohibits independent fixture processes.";
            flags |= CREATE_BREAKAWAY_FROM_JOB;
        }
    }
    const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    auto command = QStringLiteral("\"") + executable + "\" --launch-host \"" + directory + "\"";
    if (legacy) command += " --legacy-launch";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(reinterpret_cast<const wchar_t *>(executable.utf16()),
                        reinterpret_cast<wchar_t *>(command.data()), nullptr, nullptr, FALSE,
                        flags, nullptr, nullptr, &startup, &process))
        return fixtureError("Create test host");
    BOOL hostContained = FALSE;
    if (independent && (!IsProcessInJob(process.hProcess, nullptr, &hostContained) || hostContained)) {
        TerminateProcess(process.hProcess, 0);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        process = {};
        return "The test host cannot leave the enclosing test runner jobs.";
    }
    return {};
}
} // namespace

class AgentLaunchTests : public QObject {
    Q_OBJECT
    QString independentHostError_;
  private slots:
    void initTestCase() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PROCESS_INFORMATION host{};
        independentHostError_ = createHost(directory.path(), false, true, host);
        if (!independentHostError_.isEmpty()) return;
        OwnedProcess process;
        process.reset(host.hProcess);
        OwnedHandle thread(host.hThread);
        // The probe is never resumed and cannot launch another process.
    }
    void desktopOutlivesCallerJob_data() {
        QTest::addColumn<bool>("useJob");
        QTest::addColumn<quint32>("jobFlags");
        QTest::addColumn<bool>("nested");
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<bool>("success");
        QTest::addColumn<bool>("survives");
        QTest::newRow("ordinary-no-job") << false << quint32(0) << false << false << true << true;
        QTest::newRow("breakaway-permitted") << true << quint32(JOB_OBJECT_LIMIT_BREAKAWAY_OK) << false << false << true << true;
        QTest::newRow("silent-breakaway-permitted") << true << quint32(JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK) << false << false << true << true;
        QTest::newRow("restrictive-job-refuses-before-start") << true << quint32(0) << false << false << false << false;
        QTest::newRow("nested-outer-job-refuses-before-start") << true << quint32(JOB_OBJECT_LIMIT_BREAKAWAY_OK) << true << false << false << false;
        // A negative control runs the old production launch: detached alone is
        // not independent, and closing the caller's job kills its GUI child.
        QTest::newRow("old-detached-launch-is-killed") << true << quint32(JOB_OBJECT_LIMIT_BREAKAWAY_OK) << false << true << true << false;
    }
    void desktopOutlivesCallerJob() {
        QFETCH(bool, useJob);
        QFETCH(quint32, jobFlags);
        QFETCH(bool, nested);
        QFETCH(bool, legacy);
        QFETCH(bool, success);
        QFETCH(bool, survives);
        const bool independent = success && !legacy;
        if (independent && !independentHostError_.isEmpty()) QSKIP(qPrintable(independentHostError_));
        QTemporaryDir directory(QDir::tempPath() + "/EditHere launch fixture-XXXXXX");
        QVERIFY(directory.isValid());
        OwnedHandle job;
        OwnedHandle outerJob;
        if (useJob) {
            job.reset(CreateJobObjectW(nullptr, nullptr));
            QVERIFY2(job.value, qPrintable(fixtureError("Create fixture job")));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | jobFlags;
            QVERIFY2(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                     qPrintable(fixtureError("Configure fixture job")));
            if (nested) {
                outerJob.reset(CreateJobObjectW(nullptr, nullptr));
                QVERIFY2(outerJob.value, qPrintable(fixtureError("Create outer fixture job")));
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                QVERIFY2(SetInformationJobObject(outerJob.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                         qPrintable(fixtureError("Configure outer fixture job")));
            }
        }
        PROCESS_INFORMATION created{};
        const auto hostError = createHost(directory.path(), legacy, independent, created);
        QVERIFY2(hostError.isEmpty(), qPrintable(hostError));
        OwnedProcess host;
        host.reset(created.hProcess);
        OwnedHandle thread(created.hThread);
        if (nested)
            QVERIFY2(AssignProcessToJobObject(outerJob.value, host.value), qPrintable(fixtureError("Assign outer fixture job")));
        if (useJob)
            QVERIFY2(AssignProcessToJobObject(job.value, host.value), qPrintable(fixtureError("Assign fixture host")));
        QCOMPARE(inJob(host.value), useJob);
        QVERIFY2(ResumeThread(thread.value) != DWORD(-1), qPrintable(fixtureError("Start fixture host")));
        thread.reset();
        const auto resultPath = directory.filePath("launch.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(resultPath), 5000);
        const auto result = readJson(resultPath);
        QCOMPARE(result["hostInJob"].toBool(), useJob);
        const auto error = result["error"].toString();
        const auto pid = result["pid"].toInteger();
        if (!success) {
            QVERIFY(!error.isEmpty());
            QVERIFY2(error.contains("Open EditHere manually"), qPrintable(error));
            QVERIFY2(error.contains("Windows error"), qPrintable(error));
            QCOMPARE(pid, qint64(0));
            QCOMPARE(WaitForSingleObject(host.value, 5000), DWORD(WAIT_OBJECT_0));
            QVERIFY(!QFileInfo::exists(directory.filePath("desktop-started.json")));
            return;
        }
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(pid > 0);
        OwnedProcess desktop;
        desktop.reset(OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, DWORD(pid)));
        QVERIFY2(desktop.value, qPrintable(fixtureError("Open owned desktop fixture")));
        const auto startedPath = directory.filePath("desktop-started.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(startedPath), 5000);
        const auto started = readJson(startedPath);
        QCOMPARE(started["pid"].toInteger(), pid);
        QCOMPARE(started["args"].toArray(), QJsonArray{"--agent-start"});
        QCOMPARE(started["inJob"].toBool(), legacy);
        QCOMPARE(inJob(desktop.value), legacy);
        QCOMPARE(WaitForSingleObject(host.value, 5000), DWORD(WAIT_OBJECT_0));
        job.reset();
        if (survives) {
            QCOMPARE(WaitForSingleObject(desktop.value, 300), DWORD(WAIT_TIMEOUT));
            QVERIFY(!inJob(desktop.value));
        } else {
            QCOMPARE(WaitForSingleObject(desktop.value, 5000), DWORD(WAIT_OBJECT_0));
        }
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.contains("--agent-start")) {
        const auto directory = qEnvironmentVariable("EDITHERE_AGENT_LAUNCH_TEST_DIRECTORY");
        if (directory.isEmpty() || !QDir(directory).exists()) return 2;
        if (!writeJson(QDir(directory).filePath("desktop-started.json"),
                       {{"pid", QCoreApplication::applicationPid()}, {"inJob", inJob(GetCurrentProcess())},
                        {"args", QJsonArray::fromStringList(arguments.mid(1))}})) return 3;
        // Bound the fixture lifetime even if the parent test aborts unexpectedly.
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (arguments.contains("--launch-host")) {
        const auto directory = arguments.value(arguments.indexOf("--launch-host") + 1);
        if (directory.isEmpty() || !QDir(directory).exists()) return 2;
        qputenv("EDITHERE_AGENT_LAUNCH_TEST_DIRECTORY", directory.toUtf8());
        qint64 pid = 0;
        QString error;
        if (arguments.contains("--legacy-launch")) {
            QProcess process;
            process.setProgram(QCoreApplication::applicationFilePath());
            process.setArguments({"--agent-start"});
            // Our fixture is a console test binary. Suppress its window without
            // changing the inherited job behavior of the old detached launch.
            process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *creation) {
                creation->flags &= ~CREATE_NEW_CONSOLE;
                creation->flags |= CREATE_NO_WINDOW;
            });
            if (!process.startDetached(&pid)) error = process.errorString();
        } else {
            error = launchAgentDesktop(QCoreApplication::applicationFilePath(), &pid);
        }
        return writeJson(QDir(directory).filePath("launch.json"),
                         {{"pid", pid}, {"error", error}, {"hostInJob", inJob(GetCurrentProcess())}}) ? 0 : 3;
    }
    AgentLaunchTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "agent_launch_test.moc"
