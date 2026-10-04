#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace h2d;
namespace {
std::atomic<int> warningsReceived{0};
void priorHandler(QtMsgType type, const QMessageLogContext &, const QString &) {
    if (type == QtWarningMsg || type == QtCriticalMsg) ++warningsReceived;
}
struct HandlerRestore {
    QtMessageHandler original = qInstallMessageHandler(priorHandler);
    ~HandlerRestore() { diagnostics::stop(); qInstallMessageHandler(original); }
};
struct StopSession {
    ~StopSession() { diagnostics::stop(); }
};
QList<QJsonObject> records(bool *valid = nullptr) {
    QList<QJsonObject> result;
    bool okay = true;
    for (const auto &path : diagnostics::logFiles()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { okay = false; continue; }
        const auto bytes = file.readAll();
        if (!bytes.isEmpty() && !bytes.endsWith('\n')) okay = false;
        for (const auto &line : bytes.split('\n')) {
            if (line.isEmpty()) continue;
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) okay = false;
            result.append(document.object());
        }
    }
    if (valid) *valid = okay;
    return result;
}
QByteArray allLogs() {
    QByteArray result;
    for (const auto &path : diagnostics::logFiles()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) result += file.readAll();
    }
    return result;
}
} // namespace

class DiagnosticsTests : public QObject {
    Q_OBJECT
  private slots:
    void cleanup() { diagnostics::stop(); }
    void boundedRollingSession() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(temporary.isValid());
        QString error;
        QVERIFY2(diagnostics::start({temporary.path(), 1024, 3}, &error), qPrintable(error));
        for (int index = 0; index < 100; ++index) {
            diagnostics::write(diagnostics::Level::Warning, "capture", QString(2000, QChar(0x4e2d)),
                               {{"index", index}, {"large", QString(20000, QChar('x'))}});
        }
        QVERIFY(diagnostics::logFiles().size() <= 3);
        bool valid = false;
        records(&valid);
        QVERIFY(valid);
        for (const auto &path : diagnostics::logFiles()) QVERIFY(QFileInfo(path).size() <= 1024);
        diagnostics::stop();
        QVERIFY(diagnostics::logFiles().size() <= 3);
        const auto stored = records();
        QVERIFY(!stored.isEmpty());
        QCOMPARE(stored.last().value("message").toString(), QString("session.end"));
        for (int index = 0; index < 10; ++index) {
            QVERIFY(diagnostics::start({temporary.path(), 1024, 3}, &error));
            diagnostics::stop();
        }
        QVERIFY(diagnostics::logFiles().size() <= 3);
    }
    void concurrentWritesRemainComplete() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.path(), 1024 * 1024, 3}));
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 6; ++worker) {
            workers.emplace_back([worker] {
                for (int sequence = 0; sequence < 100; ++sequence)
                    diagnostics::write(diagnostics::Level::Error, "worker", "parallel failure",
                                       {{"worker", worker}, {"sequence", sequence}});
            });
        }
        for (auto &worker : workers) worker.join();
        diagnostics::stop();
        bool valid = false;
        const auto stored = records(&valid);
        QVERIFY(valid);
        QSet<QString> seen;
        for (const auto &record : stored) {
            if (record.value("component").toString() != "worker") continue;
            const auto fields = record.value("fields").toObject();
            seen.insert(QString("%1/%2").arg(fields.value("worker").toInt()).arg(fields.value("sequence").toInt()));
            QVERIFY(!record.value("thread").toString().isEmpty());
        }
        QCOMPARE(seen.size(), 600);
    }
    void restoresAndForwardsQtHandler() {
        QTemporaryDir temporary;
        StopSession session;
        HandlerRestore restore;
        warningsReceived = 0;
        QVERIFY(diagnostics::start({temporary.path()}));
        qWarning("Qt warning fixture");
        qCritical("Qt critical fixture");
        qInfo("Qt information fixture");
        QCOMPARE(warningsReceived.load(), 2);
        const auto stored = allLogs();
        QVERIFY(stored.contains("Qt warning fixture"));
        QVERIFY(stored.contains("Qt critical fixture"));
        QVERIFY(!stored.contains("Qt information fixture"));
        diagnostics::stop();
        const auto installed = qInstallMessageHandler(priorHandler);
        QVERIFY(installed == priorHandler);
        qWarning("After stop fixture");
        QCOMPARE(warningsReceived.load(), 3);
        QVERIFY(!allLogs().contains("After stop fixture"));
    }
    void redactsCommonSecretsPathsAndUrlDetails() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.path()}));
        diagnostics::write(diagnostics::Level::Error, "network",
            QDir::homePath() + "/file.png https://alice:pass@example.test/path?token=url-secret#private-fragment password=message-secret Bearer bearer-secret",
            {{"access_token", "field-secret"}, {"nested", QJsonObject{{"api-key", "nested-secret"},
                 {"url", "https://example.test/resource?key=secret#secret"}}},
             {"screenshot", "image-payload"}, {"detail", "Useful error detail"}});
        auto escapedHome = QDir::toNativeSeparators(QDir::homePath());
        escapedHome.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        diagnostics::write(diagnostics::Level::Warning, "qt", escapedHome + " \\\"token\\\": \\\"quoted-secret\\\"");
        const auto stored = allLogs();
        for (const auto &secret : {"url-secret", "private-fragment", "message-secret", "bearer-secret",
                                   "field-secret", "nested-secret", "image-payload", "alice", "pass@", "quoted-secret"})
            QVERIFY2(!stored.contains(secret), secret);
        QVERIFY(!stored.contains(QDir::homePath().toUtf8()));
        QVERIFY(stored.contains("<home>"));
        QVERIFY(stored.contains("Useful error detail"));
        QVERIFY(stored.contains("https://example.test/resource"));
        QVERIFY(stored.contains("[redacted]"));
    }
    void exportIsAtomicBoundedAndRefiltersLegacyEntries() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.filePath("logs")}));
        diagnostics::write(diagnostics::Level::Error, "save", "Permission denied", {{"code", "EACCES"}});
        const auto legacyPath = temporary.filePath("logs/edithere-legacy-1.000.jsonl");
        QFile legacy(legacyPath);
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("{\"message\":\"https://example.test/path?secret=legacy-secret#legacy-fragment\",\"fields\":{\"token\":\"legacy-token\"}}\n");
        legacy.close();
        const auto destination = temporary.filePath("report.txt");
        QVERIFY(diagnostics::exportReport(destination).isEmpty());
        QFile report(destination);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto bytes = report.readAll();
        report.close();
        QVERIFY(bytes.contains("Permission denied"));
        QVERIFY(bytes.contains("EACCES"));
        QVERIFY(bytes.contains("Environment:"));
        QVERIFY(!bytes.contains("legacy-secret"));
        QVERIFY(!bytes.contains("legacy-fragment"));
        QVERIFY(!bytes.contains("legacy-token"));
        QVERIFY(bytes.size() <= 4 * 1024 * 1024);
        QVERIFY(!diagnostics::exportReport(diagnostics::logFiles().first()).isEmpty());
        QVERIFY(!diagnostics::exportReport(temporary.filePath("missing/report.txt")).isEmpty());
        QVERIFY(!diagnostics::exportReport(temporary.path()).isEmpty());
        QVERIFY(report.open(QIODevice::ReadOnly));
        QCOMPARE(report.readAll(), bytes);
    }
    void failedStartDoesNotReplaceHandlerOrCrash() {
        QTemporaryDir temporary;
        StopSession session;
        HandlerRestore restore;
        QFile occupied(temporary.filePath("occupied"));
        QVERIFY(occupied.open(QIODevice::WriteOnly));
        occupied.write("retained");
        occupied.close();
        QString error;
        QVERIFY(!diagnostics::start({occupied.fileName()}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!diagnostics::lastError().isEmpty());
        diagnostics::write(diagnostics::Level::Error, "test", "Safe after failed initialization");
        diagnostics::stop();
        QVERIFY(qInstallMessageHandler(priorHandler) == priorHandler);
        QVERIFY(!diagnostics::start({temporary.path(), 1, 0}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("retained"));
    }
    void rotationFailureLeavesReadableLogsAndApplicationAlive() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.path(), 1024, 3}));
        const auto initial = diagnostics::logFiles().first();
        auto blockedPart = initial;
        blockedPart.replace(".000.jsonl", ".001.jsonl");
        QVERIFY(QDir().mkdir(blockedPart));
        for (int index = 0; index < 5; ++index)
            diagnostics::write(diagnostics::Level::Error, "save", QString(700, QChar('x')));
        QVERIFY(!diagnostics::lastError().isEmpty());
        diagnostics::write(diagnostics::Level::Error, "save", "Another failure after logger I/O failure");
        diagnostics::stop();
        bool valid = false;
        records(&valid);
        QVERIFY(valid);
        QVERIFY(QFile::exists(initial));
        QVERIFY(diagnostics::exportReport(temporary.filePath("report.txt")).isEmpty());
        QFile report(temporary.filePath("report.txt"));
        QVERIFY(report.open(QIODevice::ReadOnly));
        QVERIFY(report.readAll().contains("Logging error:"));
    }
    void largeReportHonorsTotalBudget() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.filePath("logs"), 256 * 1024, 30}));
        for (int index = 0; index < 700; ++index)
            diagnostics::write(diagnostics::Level::Warning, "capture", QString(7000, QChar('x')), {{"index", index}});
        const auto destination = temporary.filePath("report.txt");
        QVERIFY(diagnostics::exportReport(destination).isEmpty());
        const auto size = QFileInfo(destination).size();
        QVERIFY(size > 3 * 1024 * 1024);
        QVERIFY(size <= 4 * 1024 * 1024);
        QFile report(destination);
        QVERIFY(report.open(QIODevice::ReadOnly));
        QVERIFY(report.readAll().endsWith('\n'));
    }
    void livePeerRetainedAndKilledPeerIdentified() {
        QTemporaryDir temporary;
        StopSession session;
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--diagnostics-child", temporary.path()});
        QVERIFY2(child.waitForStarted(10000), qPrintable(child.errorString()));
        QVERIFY(child.waitForReadyRead(10000));
        QVERIFY(child.readAllStandardOutput().contains("ready"));
        const auto peerFiles = QDir(temporary.path()).entryList({"edithere-*.jsonl"}, QDir::Files);
        QVERIFY(!peerFiles.isEmpty());
        QVERIFY(diagnostics::start({temporary.path(), 1024, 1}));
        for (int index = 0; index < 20; ++index)
            diagnostics::write(diagnostics::Level::Info, "parent", QString(700, QChar('x')));
        QVERIFY(!allLogs().contains("previous_session.not_cleanly_closed"));
        for (const auto &name : peerFiles) QVERIFY(QFile::exists(temporary.filePath(name)));
        diagnostics::stop();
        for (const auto &name : peerFiles) QVERIFY(QFile::exists(temporary.filePath(name)));
        child.kill();
        QVERIFY(child.waitForFinished(10000));
        QVERIFY(diagnostics::start({temporary.path(), 1024, 10}));
        QVERIFY(allLogs().contains("previous_session.not_cleanly_closed"));
    }
    void equalModificationTimesOrderNumericPartsAndRetainCleanEnd() {
        QTemporaryDir temporary;
        StopSession session;
        QVERIFY(diagnostics::start({temporary.path(), 1024, 10}));
        diagnostics::stop();
        for (const auto &path : diagnostics::logFiles()) QVERIFY(QFile::remove(path));
        const auto fixedTime = QDateTime::fromString("2000-01-01T00:00:00.000Z", Qt::ISODateWithMs);
        QVERIFY(fixedTime.isValid());
        const auto prefix = QStringLiteral("edithere-20000101T000000000Z-42-fixture");
        QStringList expected;
        // Deliberately create in a different order from numeric or lexical sort.
        for (int part : {1000, 999, 1001}) {
            const auto path = temporary.filePath(prefix + QString(".%1.jsonl").arg(part, 3, 10, QChar('0')));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QJsonObject record{{"component", "diagnostics"},
                {"message", part == 1001 ? "session.end" : "fixture.progress"}, {"fields", QJsonObject{{"part", part}}}};
            const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
            QCOMPARE(file.write(bytes), bytes.size());
            QVERIFY(file.flush());
            QVERIFY(file.setFileTime(fixedTime, QFileDevice::FileModificationTime));
            file.close();
            QCOMPARE(QFileInfo(path).lastModified(), fixedTime);
        }
        for (int part : {999, 1000, 1001})
            expected.append(temporary.filePath(prefix + QString(".%1.jsonl").arg(part, 3, 10, QChar('0'))));
        QCOMPARE(diagnostics::logFiles(), expected);
        const auto reportPath = temporary.filePath("report.txt");
        QVERIFY(diagnostics::exportReport(reportPath).isEmpty());
        QFile report(reportPath);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto bytes = report.readAll();
        const auto part999 = bytes.indexOf((prefix + ".999.jsonl").toUtf8());
        const auto part1000 = bytes.indexOf((prefix + ".1000.jsonl").toUtf8());
        const auto part1001 = bytes.indexOf((prefix + ".1001.jsonl").toUtf8());
        QVERIFY(part1001 >= 0 && part1001 < part1000 && part1000 < part999);
        report.close();
        QVERIFY(diagnostics::start({temporary.path(), 1024, 10}));
        QVERIFY(!allLogs().contains("previous_session.not_cleanly_closed"));
        diagnostics::stop();
        // Under a tighter budget, prune the old equal-time fragments first.
        QVERIFY(diagnostics::start({temporary.path(), 1024, 3}));
        QVERIFY(!QFile::exists(expected.first()));
        QVERIFY(QFile::exists(expected.last()));
        for (int index = 0; index < 20; ++index)
            diagnostics::write(diagnostics::Level::Warning, "capture", QString(700, QChar('x')));
        diagnostics::stop();
        QVERIFY(diagnostics::logFiles().size() <= 3);
        QCOMPARE(records().last().value("message").toString(), QString("session.end"));
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("EditHere diagnostics tests");
    app.setApplicationVersion("test-version");
    if (app.arguments().contains("--diagnostics-child")) {
        const auto arguments = app.arguments();
        const auto path = arguments.value(arguments.indexOf("--diagnostics-child") + 1);
        if (!diagnostics::start({path, 1024, 3})) return 2;
        for (int index = 0; index < 10; ++index)
            diagnostics::write(diagnostics::Level::Warning, "child", QString(700, QChar('x')));
        std::puts("ready");
        std::fflush(stdout);
        return app.exec(); // Parent deliberately kills this fixture process.
    }
    DiagnosticsTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "diagnostics_test.moc"
