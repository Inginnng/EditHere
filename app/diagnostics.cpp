#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <memory>

namespace h2d::diagnostics {
namespace {
constexpr qint64 reportLimit = 4 * 1024 * 1024;
constexpr qint64 recordLimit = 16 * 1024;
struct State {
    QMutex mutex;
    QString path;
    QString error;
    QString session;
    Options options;
    QFile file;
    std::unique_ptr<QLockFile> lock;
    int part = 0;
    bool running = false;
};
State &state() {
    // Qt's message handler can run during static teardown. Keep its small state
    // alive for the process lifetime; normal shutdown explicitly calls stop().
    static auto *instance = new State;
    return *instance;
}
std::atomic<QtMessageHandler> previousHandler{nullptr};
thread_local bool handlingMessage = false;
thread_local bool writingLog = false;
struct InternalGuard {
    bool previous = writingLog;
    InternalGuard() { writingLog = true; }
    ~InternalGuard() { writingLog = previous; }
};

QString utcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString sessionFor(const QString &name) {
    static const QRegularExpression pattern(QStringLiteral("^(edithere-[A-Za-z0-9-]+)\\.[0-9]{3,}\\.jsonl$"));
    return pattern.match(name).captured(1);
}
qint64 partFor(const QFileInfo &entry) {
    return entry.fileName().section('.', -2, -2).toLongLong();
}
QFileInfoList files(const QString &path) {
    auto entries = QDir(path).entryInfoList({QStringLiteral("edithere-*.jsonl")}, QDir::Files, QDir::NoSort);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const QFileInfo &entry) {
        return sessionFor(entry.fileName()).isEmpty();
    }), entries.end());
    // QDir's time ordering leaves ties dependent on filesystem enumeration.
    // Apply one tuple ordering across every session, and compare numeric parts
    // so part 1000 follows 999 even when their modification times are equal.
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        if (left.lastModified() != right.lastModified()) return left.lastModified() < right.lastModified();
        const auto leftSession = sessionFor(left.fileName());
        const auto rightSession = sessionFor(right.fileName());
        if (leftSession != rightSession) return leftSession < rightSession;
        return partFor(left) < partFor(right);
    });
    return entries;
}
QString safeString(QString value) {
    // Bound work before regular expressions, even when a caller passes a huge
    // diagnostic. No screenshots or binary payloads are collected by this module.
    if (value.size() > 8192) value = value.left(8192) + QStringLiteral(" [truncated]");
    const auto home = QDir::homePath();
    if (!home.isEmpty()) {
        value.replace(home, QStringLiteral("<home>"), Qt::CaseInsensitive);
        value.replace(QDir::toNativeSeparators(home), QStringLiteral("<home>"), Qt::CaseInsensitive);
        auto backslashHome = home;
        backslashHome.replace('/', '\\');
        value.replace(backslashHome, QStringLiteral("<home>"), Qt::CaseInsensitive);
        backslashHome.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        value.replace(backslashHome, QStringLiteral("<home>"), Qt::CaseInsensitive);
    }
    static const QRegularExpression urls(QStringLiteral("[A-Za-z][A-Za-z0-9+.-]*://[^\\s\\\"<>]+"));
    auto iterator = urls.globalMatch(value);
    QList<QPair<QRegularExpressionMatch, QString>> replacements;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        QUrl url(match.captured());
        if (!url.isValid()) continue;
        url.setQuery(QString());
        url.setFragment(QString());
        if (!url.userInfo().isEmpty()) url.setUserInfo(QStringLiteral("redacted"));
        replacements.append({match, url.toString(QUrl::FullyEncoded)});
    }
    for (auto index = replacements.size(); index > 0; --index) {
        const auto &replacement = replacements[index - 1];
        value.replace(replacement.first.capturedStart(), replacement.first.capturedLength(), replacement.second);
    }
    static const QRegularExpression bearer(QStringLiteral("\\bBearer\\s+[^\\s,;]+"), QRegularExpression::CaseInsensitiveOption);
    value.replace(bearer, QStringLiteral("Bearer [redacted]"));
    static const QRegularExpression secrets(
        QStringLiteral(R"rx((?:\\?["'])?\b(password|passwd|token|secret|authorization|api[_-]?key|access[_-]?key|cookie|credential)(?:\\?["'])?\s*([:=])\s*(?:\\?"[^"]*\\?"|\\?'[^']*\\?'|[^\s,;]+))rx"),
        QRegularExpression::CaseInsensitiveOption);
    value.replace(secrets, QStringLiteral("\\1\\2[redacted]"));
    return value;
}
bool sensitiveKey(QString key) {
    key = key.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    static const QStringList names{QStringLiteral("password"), QStringLiteral("passwd"), QStringLiteral("token"),
        QStringLiteral("secret"), QStringLiteral("authorization"), QStringLiteral("apikey"), QStringLiteral("accesskey"),
        QStringLiteral("cookie"), QStringLiteral("credential"), QStringLiteral("sessionid")};
    for (const auto &name : names) if (key.contains(name)) return true;
    static const QStringList content{QStringLiteral("image"), QStringLiteral("imagedata"), QStringLiteral("screenshot"),
        QStringLiteral("pixels"), QStringLiteral("projectcontent"), QStringLiteral("projectdata"),
        QStringLiteral("annotations"), QStringLiteral("annotation"), QStringLiteral("feedbackcontent")};
    return content.contains(key);
}
QJsonValue safeValue(const QJsonValue &value, int depth = 0) {
    if (depth > 8) return QStringLiteral("[truncated]");
    if (value.isString()) return safeString(value.toString());
    if (value.isArray()) {
        QJsonArray result;
        const auto source = value.toArray();
        for (int index = 0; index < qMin<qsizetype>(64, source.size()); ++index) result.append(safeValue(source[index], depth + 1));
        if (source.size() > 64) result.append(QStringLiteral("[truncated]"));
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const auto source = value.toObject();
        int count = 0;
        for (auto it = source.begin(); it != source.end(); ++it) {
            if (++count > 64) { result.insert(QStringLiteral("_truncated"), true); break; }
            result.insert(safeString(it.key()).left(128), sensitiveKey(it.key()) ? QJsonValue(QStringLiteral("[redacted]")) : safeValue(it.value(), depth + 1));
        }
        return result;
    }
    return value;
}
QJsonObject environment() {
    return {{QStringLiteral("application"), QCoreApplication::applicationName()},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
            {QStringLiteral("os"), QSysInfo::prettyProductName()},
            {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("buildAbi"), QSysInfo::buildAbi()}};
}
std::unique_ptr<QLockFile> inactiveLock(const QString &path, const QString &session) {
    auto lock = std::make_unique<QLockFile>(QDir(path).filePath(session + QStringLiteral(".lock")));
    lock->setStaleLockTime(0); // Age never proves a long-running GUI is stale.
    if (!lock->tryLock()) return {};
    return lock;
}
void prune(State &s) {
    auto entries = files(s.path);
    int remaining = entries.size();
    for (const auto &entry : entries) {
        if (remaining <= s.options.maxFiles) break;
        const auto session = sessionFor(entry.fileName());
        if (session == s.session && s.running) {
            if (entry.absoluteFilePath() == s.file.fileName()) continue;
            // This process owns the session lock. Earlier parts are closed and
            // safe to prune, so a single long-running session stays bounded too.
            if (QFile::remove(entry.absoluteFilePath())) --remaining;
            continue;
        }
        auto lock = inactiveLock(s.path, session);
        if (!lock) continue;
        if (QFile::remove(entry.absoluteFilePath())) --remaining;
    }
}
bool openPart(State &s) {
    s.file.setFileName(QDir(s.path).filePath(s.session + QStringLiteral(".%1.jsonl").arg(s.part, 3, 10, QLatin1Char('0'))));
    if (s.file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return true;
    s.error = QStringLiteral("Cannot open diagnostic log: %1").arg(s.file.errorString());
    return false;
}
void append(State &s, Level level, const QString &component, const QString &message, const QJsonObject &fields = {}) {
    if (!s.running || !s.file.isOpen()) return;
    QJsonObject record{{QStringLiteral("time"), utcNow()},
        {QStringLiteral("level"), level == Level::Error ? QStringLiteral("error") : level == Level::Warning ? QStringLiteral("warning") : QStringLiteral("info")},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("thread"), QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)},
        {QStringLiteral("component"), safeString(component).left(128)},
        {QStringLiteral("message"), safeString(message)},
        {QStringLiteral("fields"), safeValue(fields)}};
    const auto maximum = qMin(recordLimit, s.options.maxFileBytes);
    auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > maximum) {
        record[QStringLiteral("fields")] = QJsonObject{{QStringLiteral("_truncated"), true}};
        record[QStringLiteral("truncated")] = true;
        auto text = record.value(QStringLiteral("message")).toString();
        do {
            text = text.left(text.size() / 2);
            record[QStringLiteral("message")] = text + QStringLiteral(" [truncated]");
            bytes = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
        } while (bytes.size() > maximum && !text.isEmpty());
    }
    if (s.file.size() > 0 && s.file.size() + bytes.size() > s.options.maxFileBytes) {
        s.file.close();
        ++s.part;
        if (!openPart(s)) return;
        prune(s);
    }
    const auto oldSize = s.file.size();
    if (s.file.write(bytes) != bytes.size() || !s.file.flush()) {
        s.error = QStringLiteral("Cannot write diagnostic log: %1").arg(s.file.errorString());
        s.file.resize(oldSize); // Best effort: do not retain a partial JSON line.
        s.file.close(); // Stop disk-full/revoked-permission retries on every event.
    }
}
void qtMessages(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const auto previous = previousHandler.load();
    const bool outermost = !handlingMessage;
    if (outermost) {
        handlingMessage = true;
        if (!writingLog && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)) {
            write(type == QtWarningMsg ? Level::Warning : Level::Error, QStringLiteral("qt"), message,
                  {{QStringLiteral("category"), QString::fromUtf8(context.category ? context.category : "")},
                   {QStringLiteral("source"), QString::fromUtf8(context.file ? context.file : "")},
                   {QStringLiteral("line"), context.line},
                   {QStringLiteral("function"), QString::fromUtf8(context.function ? context.function : "")},
                   {QStringLiteral("fatal"), type == QtFatalMsg}});
        }
    }
    // Retain existing console/debugger/custom-handler behavior; never invoke Qt
    // logging from the file writer. qFatal still aborts after this handler returns.
    if (previous && outermost) previous(type, context, message);
    else {
        const auto formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fwrite(formatted.constData(), 1, size_t(formatted.size()), stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
    if (outermost) handlingMessage = false;
}
QStringList uncleanSessions(State &s) {
    QMap<QString, QFileInfo> newest;
    for (const auto &entry : files(s.path)) {
        const auto session = sessionFor(entry.fileName());
        const auto part = partFor(entry);
        if (!newest.contains(session) || partFor(newest[session]) < part)
            newest[session] = entry;
    }
    QStringList unclean;
    for (auto it = newest.begin(); it != newest.end(); ++it) {
        auto lock = inactiveLock(s.path, it.key());
        if (!lock) continue; // Still-running processes are not crashes.
        QFile file(it.value().absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto tail = qMax<qint64>(0, file.size() - recordLimit * 2);
        file.seek(tail);
        const auto lines = file.readAll().split('\n');
        QJsonObject finalRecord;
        for (auto index = lines.size(); index > 0; --index) {
            if (lines[index - 1].trimmed().isEmpty()) continue;
            finalRecord = QJsonDocument::fromJson(lines[index - 1]).object();
            break;
        }
        if (finalRecord.value(QStringLiteral("component")) != QStringLiteral("diagnostics") ||
            finalRecord.value(QStringLiteral("message")) != QStringLiteral("session.end")) unclean.append(it.key());
    }
    return unclean;
}
} // namespace

bool start(const Options &options, QString *error) {
    InternalGuard internal;
    auto &s = state();
    QMutexLocker guard(&s.mutex);
    if (error) error->clear();
    if (s.running) return true;
    s.error.clear();
    const auto standardPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    s.path = options.directory.isEmpty()
        ? (standardPath.isEmpty() ? QString() : QDir(standardPath).filePath(QStringLiteral("logs")))
        : QDir(options.directory).absolutePath();
    s.options = options;
    if (options.maxFileBytes < 1024 || options.maxFileBytes > 64 * 1024 * 1024 || options.maxFiles < 1 || options.maxFiles > 100) {
        s.error = QStringLiteral("Diagnostic limits must be 1 KiB–64 MiB per file and 1–100 files.");
    } else if (s.path.isEmpty() || !QDir().mkpath(s.path)) {
        s.error = QStringLiteral("Cannot create diagnostic directory: %1").arg(s.path);
    }
    if (!s.error.isEmpty()) { if (error) *error = s.error; return false; }
    const auto previousUnclean = uncleanSessions(s);
    s.session = QStringLiteral("edithere-%1-%2-%3")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ")))
        .arg(QCoreApplication::applicationPid()).arg(QUuid::createUuid().toString(QUuid::Id128));
    s.lock = inactiveLock(s.path, s.session);
    if (!s.lock) s.error = QStringLiteral("Cannot lock diagnostic session.");
    s.part = 0;
    if (!s.error.isEmpty() || !openPart(s)) {
        s.lock.reset();
        if (error) *error = s.error;
        return false;
    }
    s.running = true;
    append(s, Level::Info, QStringLiteral("diagnostics"), QStringLiteral("session.start"), environment());
    for (const auto &session : previousUnclean.mid(0, 20)) {
        append(s, Level::Warning, QStringLiteral("diagnostics"), QStringLiteral("previous_session.not_cleanly_closed"),
               {{QStringLiteral("session"), session}, {QStringLiteral("note"), QStringLiteral("May indicate a crash, forced termination, or logging write failure.")}});
    }
    prune(s);
    if (!s.file.isOpen()) {
        s.running = false;
        s.lock.reset();
        if (error) *error = s.error;
        return false;
    }
    previousHandler.store(qInstallMessageHandler(qtMessages));
    return true;
}
void stop() {
    InternalGuard internal;
    auto &s = state();
    QMutexLocker guard(&s.mutex);
    if (!s.running) return;
    const auto installed = qInstallMessageHandler(previousHandler.load());
    // Do not overwrite a handler installed by another component after start().
    if (installed != qtMessages) qInstallMessageHandler(installed);
    append(s, Level::Info, QStringLiteral("diagnostics"), QStringLiteral("session.end"));
    s.file.close();
    s.running = false;
    s.lock.reset();
    prune(s);
}
void write(Level level, const QString &component, const QString &message, const QJsonObject &fields) {
    if (writingLog) return;
    InternalGuard internal;
    auto &s = state();
    QMutexLocker guard(&s.mutex);
    append(s, level, component, message, fields);
}
QString directory() { auto &s = state(); QMutexLocker guard(&s.mutex); return s.path; }
QString lastError() { auto &s = state(); QMutexLocker guard(&s.mutex); return s.error; }
QStringList logFiles() {
    InternalGuard internal;
    auto &s = state();
    QMutexLocker guard(&s.mutex);
    QStringList result;
    for (const auto &entry : files(s.path)) result.append(entry.absoluteFilePath());
    return result;
}
QString exportReport(const QString &destination) {
    InternalGuard internal;
    auto &s = state();
    QMutexLocker guard(&s.mutex);
    if (destination.isEmpty()) return QStringLiteral("No diagnostic report destination was selected.");
    if (s.path.isEmpty()) return QStringLiteral("Diagnostics have not been initialized.");
    const auto entries = files(s.path);
    const auto absolute = QFileInfo(destination).absoluteFilePath();
    for (const auto &entry : entries) {
        if (absolute.compare(entry.absoluteFilePath(), Qt::CaseInsensitive) == 0)
            return QStringLiteral("A diagnostic report cannot replace a source log.");
    }
    QByteArray report = "EditHere diagnostic report\nCreated UTC: " + utcNow().toUtf8() +
        "\nEnvironment: " + QJsonDocument(safeValue(environment()).toObject()).toJson(QJsonDocument::Compact) +
        "\nMessages have basic home-path, URL-query and secret filtering. Review before sharing; filtering is not exhaustive."
        "\nNo screenshots or project contents are collected by the diagnostics module."
        "\nNewest logs first; report limited to 4 MiB.\n";
    if (!s.error.isEmpty()) report += "Logging error: " + safeString(s.error).toUtf8() + '\n';
    if (s.file.isOpen()) s.file.flush();
    for (auto index = entries.size(); index > 0 && report.size() < reportLimit - 1024; --index) {
        QFile file(entries[index - 1].absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto budget = qMin<qint64>(256 * 1024, reportLimit - report.size() - 512);
        if (budget <= 0) break;
        const auto offset = qMax<qint64>(0, file.size() - budget);
        file.seek(offset);
        auto bytes = file.read(budget);
        if (offset > 0) {
            const auto newline = bytes.indexOf('\n');
            bytes = newline < 0 ? QByteArray() : bytes.mid(newline + 1);
        }
        report += "\n--- " + entries[index - 1].fileName().toUtf8() + " ---\n";
        if (offset > 0) report += "[Earlier entries omitted]\n";
        // Re-filter old on-disk logs too, including manually imported legacy
        // entries, rather than trusting their collection-time sanitization.
        for (const auto &line : bytes.split('\n')) {
            if (line.isEmpty()) continue;
            const auto document = QJsonDocument::fromJson(line);
            if (!document.isObject()) { report += "[Unreadable entry omitted]\n"; continue; }
            const auto filtered = QJsonDocument(safeValue(document.object()).toObject()).toJson(QJsonDocument::Compact) + '\n';
            if (report.size() + filtered.size() > reportLimit - 128) break;
            report += filtered;
        }
    }
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot create diagnostic report: %1").arg(output.errorString());
    if (output.write(report) != report.size() || !output.commit())
        return QStringLiteral("Cannot save diagnostic report: %1").arg(output.errorString());
    return {};
}
} // namespace h2d::diagnostics
