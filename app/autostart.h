#pragma once
#include <QByteArray>
#include <QString>

namespace h2d {
// These functions inspect/change the current user's OS registration, not a saved preference.
// Registered items disabled in Windows or awaiting macOS approval also return true,
// so users can still remove them. This is not a guarantee that the OS will launch the app.
// Present launchAtLoginNotice() alongside that state and after enabling.
bool launchAtLoginEnabled(QString *error = nullptr);
bool setLaunchAtLoginEnabled(bool enabled, QString *error = nullptr);
QString launchAtLoginNotice();

// Call before QApplication construction to remember macOS's launch Apple event.
// Neither function registers a login item. Windows uses the explicit --autostart argument.
void initializeLaunchAtLoginDetection();
bool wasLaunchedAtLogin();

namespace autostart_detail {
// Small storage boundary keeps tests away from the user's actual Run registry key.
// A missing value is represented by an empty command. Implementations own only EditHere.
class RunValueStore {
  public:
    virtual ~RunValueStore() = default;
    virtual bool read(QString *command, QString *error) = 0;
    virtual bool write(const QString &command, QString *error) = 0;
    virtual bool remove(QString *error) = 0;
};
enum class StartupApproval { NotRecorded, Enabled, Disabled, Unknown };
// Read-only interpretation of known Windows StartupApproved values; unknown formats stay unknown.
StartupApproval windowsStartupApproval(const QByteArray &value);
QString windowsLaunchAtLoginNotice(const QString &command, const QString &executablePath,
                                  bool registeredExecutableExists, StartupApproval approval);
QString windowsLaunchCommand(const QString &executablePath, QString *error = nullptr);
bool windowsLaunchAtLoginEnabled(RunValueStore &store, const QString &executablePath,
                                QString *error = nullptr);
bool setWindowsLaunchAtLoginEnabled(RunValueStore &store, const QString &executablePath, bool enabled,
                                   QString *error = nullptr);
} // namespace autostart_detail
} // namespace h2d
