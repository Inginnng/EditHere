#include "projectfiles.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace h2d {
bool registerProjectFileAssociation(QString *error) {
    if (error)
        error->clear();
#ifdef Q_OS_WIN
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (!QFileInfo(executable).isFile()) {
        if (error)
            *error = QStringLiteral("找不到当前 EditHere 程序");
        return false;
    }
    const QString classes = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\");
    const QString progId = QStringLiteral("EditHere.Project");
    QSettings handler(classes + progId, QSettings::NativeFormat);
    handler.setValue(QStringLiteral("."), QStringLiteral("EditHere 项目"));
    handler.setValue(QStringLiteral("DefaultIcon/."), QStringLiteral("\"%1\",0").arg(executable));
    handler.setValue(QStringLiteral("shell/open/command/."),
                     QStringLiteral("\"%1\" \"%2\"").arg(executable, QStringLiteral("%1")));
    handler.sync();
    if (handler.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("无法为当前用户注册项目打开方式");
        return false;
    }
    QSettings extension(classes + QStringLiteral(".edithere"), QSettings::NativeFormat);
    QSettings merged(QStringLiteral("HKEY_CLASSES_ROOT\\.edithere"), QSettings::NativeFormat);
    const QString existingDefault = merged.value(QStringLiteral(".")).toString();
    extension.setValue(QStringLiteral("OpenWithProgids/") + progId, QString{});
    if (existingDefault.isEmpty())
        extension.setValue(QStringLiteral("."), progId);
    extension.sync();
    if (extension.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("项目已保存，但无法注册文件打开方式");
        return false;
    }
    // Notify Explorer without adding a new link dependency to the portable build.
    if (const HMODULE shell = LoadLibraryW(L"shell32.dll")) {
        using Notify = void(WINAPI *)(LONG, UINT, LPCVOID, LPCVOID);
        if (const auto notify = reinterpret_cast<Notify>(GetProcAddress(shell, "SHChangeNotify")))
            notify(0x08000000L /* SHCNE_ASSOCCHANGED */, 0, nullptr, nullptr);
        FreeLibrary(shell);
    }
#endif
    return true;
}
} // namespace h2d
