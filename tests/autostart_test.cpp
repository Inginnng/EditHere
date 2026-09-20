#include "autostart.h"
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace h2d::autostart_detail;
namespace {
// No test constructs the native store or calls the public functions that write OS login items.
class IniRunValueStore final : public RunValueStore {
  public:
    explicit IniRunValueStore(QString path) : path(std::move(path)) {}
    QString path;
    bool denyRead = false, denyWrite = false, denyRemove = false;
    int writes = 0;
    bool read(QString *command, QString *error) override {
        if (denyRead)
            return failure(error, "读取被拒绝");
        QSettings settings(path, QSettings::IniFormat);
        *command = settings.value("HelpDesign").toString();
        return settings.status() == QSettings::NoError || failure(error, "读取失败");
    }
    bool write(const QString &command, QString *error) override {
        if (denyWrite)
            return failure(error, "写入被拒绝");
        ++writes;
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue("HelpDesign", command);
        settings.sync();
        return settings.status() == QSettings::NoError || failure(error, "写入失败");
    }
    bool remove(QString *error) override {
        if (denyRemove)
            return failure(error, "移除被拒绝");
        QSettings settings(path, QSettings::IniFormat);
        settings.remove("HelpDesign");
        settings.sync();
        return settings.status() == QSettings::NoError || failure(error, "移除失败");
    }
  private:
    static bool failure(QString *error, const QString &message) {
        if (error)
            *error = message;
        return false;
    }
};
} // namespace

class AutostartTests : public QObject {
    Q_OBJECT
  private slots:
    void quotedAbsoluteCommand() {
        QString error = "previous";
        QCOMPARE(windowsLaunchCommand("C:/用户/Help Design/HelpDesign.exe", &error),
                 QString("\"C:\\用户\\Help Design\\HelpDesign.exe\" --autostart"));
        QVERIFY(error.isEmpty());
        QCOMPARE(windowsLaunchCommand("\\\\server\\shared folder\\HelpDesign.exe", &error),
                 QString("\"\\\\server\\shared folder\\HelpDesign.exe\" --autostart"));
        QVERIFY(error.isEmpty());
    }
    void invalidCommands_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("empty") << QString();
        QTest::newRow("relative") << QString("HelpDesign.exe");
        QTest::newRow("drive-relative") << QString("C:HelpDesign.exe");
        QTest::newRow("embedded-quote") << QString("C:/bad\"path/HelpDesign.exe");
        QTest::newRow("newline") << QString("C:/bad\npath/HelpDesign.exe");
        QTest::newRow("nul") << (QString("C:/bad") + QChar::Null + "/HelpDesign.exe");
        QTest::newRow("directory") << QString("C:/app/");
        QTest::newRow("device-path") << QString("\\\\.\\C:\\HelpDesign.exe");
        QTest::newRow("long-prefix") << QString("\\\\?\\C:\\HelpDesign.exe");
        QTest::newRow("unc-directory") << QString("\\\\server\\share");
    }
    void invalidCommands() {
        QFETCH(QString, path);
        QString error;
        QVERIFY(windowsLaunchCommand(path, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void commandLengthBoundary() {
        const auto fixed = windowsLaunchCommand("C:/app.exe");
        const auto path = QString("C:/") + QString(260 - fixed.size(), 'a') + "app.exe";
        QString error;
        QCOMPARE(windowsLaunchCommand(path, &error).size(), 260);
        QVERIFY(error.isEmpty());
        QVERIFY(windowsLaunchCommand(path + "a", &error).isEmpty());
        QVERIFY(error.contains("260"));
    }
    void persistenceIdempotenceAndOwnership() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("run.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("OtherApplication", "other.exe --startup");
            settings.setValue("HelpDesignOther", "keep.exe");
            settings.sync();
        }
        IniRunValueStore store(path);
        const QString executable = "C:/用户/Help Design/HelpDesign.exe";
        QString error = "previous";
        QVERIFY(!windowsLaunchAtLoginEnabled(store, executable, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(store.writes, 0);
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, executable, true, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(windowsLaunchAtLoginEnabled(store, executable, &error));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, executable, true, &error));
        QCOMPARE(store.writes, 1);
        {
            QSettings reloaded(path, QSettings::IniFormat);
            QCOMPARE(reloaded.value("HelpDesign").toString(), windowsLaunchCommand(executable));
        }
        // Moving the app must keep the old registration visible, so it can still be turned off.
        QVERIFY(windowsLaunchAtLoginEnabled(store, "D:/new/HelpDesign.exe", &error));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "D:/new/HelpDesign.exe", false, &error));
        QVERIFY(!windowsLaunchAtLoginEnabled(store, executable, &error));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "D:/new/HelpDesign.exe", true, &error));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "D:/new/HelpDesign.exe", false, &error));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "D:/new/HelpDesign.exe", false, &error));
        QVERIFY(!windowsLaunchAtLoginEnabled(store, executable, &error));
        QSettings reloaded(path, QSettings::IniFormat);
        QVERIFY(!reloaded.contains("HelpDesign"));
        QCOMPARE(reloaded.value("OtherApplication").toString(), QString("other.exe --startup"));
        QCOMPARE(reloaded.value("HelpDesignOther").toString(), QString("keep.exe"));
    }
    void repairExistingRegistrationWithoutToggle() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        IniRunValueStore store(directory.filePath("run.ini"));
        QString error;
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "E:/old/HelpDesign.exe", true, &error));
        QVERIFY(windowsLaunchAtLoginEnabled(store, "C:/Users/Test/Programs/EditHere/EditHere.exe", &error));
        // Saving an already-checked option repairs a portable/renamed registration in place.
        const QString installed = "C:/Users/Test/Programs/EditHere/EditHere.exe";
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, installed, true, &error));
        QCOMPARE(store.writes, 2);
        QString command;
        QVERIFY(store.read(&command, &error));
        QCOMPARE(command, windowsLaunchCommand(installed));
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, installed, true, &error));
        QCOMPARE(store.writes, 2);
        QVERIFY(windowsLaunchAtLoginNotice(command, installed, true, StartupApproval::NotRecorded).isEmpty());
    }
    void startupApprovalRecords() {
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("020000000000000000000000")),
                 StartupApproval::Enabled);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("060000000000000000000000")),
                 StartupApproval::Enabled);
        // A disabled record can contain a timestamp; it does not change the disabled state.
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("0300000023456789abcdef01")),
                 StartupApproval::Disabled);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("070000000000000000000000")),
                 StartupApproval::Disabled);
        QCOMPARE(windowsStartupApproval({}), StartupApproval::Unknown);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("02")), StartupApproval::Unknown);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("02000000000000000000000000")),
                 StartupApproval::Unknown);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("020100000000000000000000")),
                 StartupApproval::Unknown);
        QCOMPARE(windowsStartupApproval(QByteArray::fromHex("ff0000000000000000000000")),
                 StartupApproval::Unknown);
    }
    void startupNoticesExplainRepairAndSystemDisableSeparately() {
        const QString current = "C:/Users/Test/Programs/EditHere/EditHere.exe";
        const auto command = windowsLaunchCommand(current);
        QVERIFY(windowsLaunchAtLoginNotice({}, current, false, StartupApproval::Disabled).isEmpty());
        QVERIFY(windowsLaunchAtLoginNotice(command, current, true, StartupApproval::Enabled).isEmpty());
        QVERIFY(windowsLaunchAtLoginNotice(command.toLower(), current, true,
                                          StartupApproval::NotRecorded).isEmpty());
        const auto disabled = windowsLaunchAtLoginNotice(command, current, true, StartupApproval::Disabled);
        QVERIFY(disabled.contains("Windows 已禁用"));
        QVERIFY(disabled.contains("仅在此处保存不会解除系统禁用"));
        QVERIFY(!disabled.contains("程序已不存在"));
        const auto moved = windowsLaunchAtLoginNotice(windowsLaunchCommand("E:/old/HelpDesign.exe"),
                                                    current, true, StartupApproval::NotRecorded);
        QVERIFY(moved.contains("保持勾选并保存"));
        const auto missing = windowsLaunchAtLoginNotice(windowsLaunchCommand("E:/old/HelpDesign.exe"),
                                                      current, false, StartupApproval::Disabled);
        QVERIFY(missing.contains("程序已不存在"));
        QVERIFY(missing.contains("Windows 已禁用"));
        QVERIFY(missing.contains("修复为当前程序位置"));
        const auto unknown = windowsLaunchAtLoginNotice(command, current, true, StartupApproval::Unknown);
        QVERIFY(unknown.contains("无法确认"));
        QVERIFY(!unknown.contains("已禁用"));
    }
    void failuresPreserveExistingRegistration() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        IniRunValueStore store(directory.filePath("run.ini"));
        const QString original = "C:/original/HelpDesign.exe", changed = "C:/changed/HelpDesign.exe";
        QString error;
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, original, true, &error));
        store.denyWrite = true;
        QVERIFY(!setWindowsLaunchAtLoginEnabled(store, changed, true, &error));
        QCOMPARE(error, QString("写入被拒绝"));
        store.denyRemove = true;
        QVERIFY(!setWindowsLaunchAtLoginEnabled(store, original, false, &error));
        QCOMPARE(error, QString("移除被拒绝"));
        QVERIFY(windowsLaunchAtLoginEnabled(store, original, &error));
        QVERIFY(error.isEmpty());
        store.denyRead = true;
        QVERIFY(!windowsLaunchAtLoginEnabled(store, original, &error));
        QCOMPARE(error, QString("读取被拒绝"));
        QVERIFY(!setWindowsLaunchAtLoginEnabled(store, changed, true, &error));
        QCOMPARE(error, QString("读取被拒绝"));
        store.denyRead = false;
        QVERIFY(!setWindowsLaunchAtLoginEnabled(store, "relative.exe", true, &error));
        QString stored;
        QVERIFY(store.read(&stored, &error));
        QCOMPARE(stored, windowsLaunchCommand(original));
        // Removing an old entry does not depend on a valid/short current executable path.
        store.denyRemove = false;
        QVERIFY(setWindowsLaunchAtLoginEnabled(store, "relative.exe", false, &error));
    }
    void actualStorageFailureIsReported() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // A directory cannot be replaced by a settings file, on either supported OS.
        IniRunValueStore store(directory.path());
        QString error;
        QVERIFY(!setWindowsLaunchAtLoginEnabled(store, "C:/app/HelpDesign.exe", true, &error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AutostartTests)
#include "autostart_test.moc"
