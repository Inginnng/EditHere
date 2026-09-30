# -*- coding: utf-8 -*-
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"
!include "x64.nsh"

# Make sure no EditHere instance is holding the executable or its Qt DLLs.
# integrate.ps1 uses exit codes to separate the cases: 0 ready, 1 fatal,
# 2 still running. Running instances are asked to exit through their own
# save/discard prompt, then we wait instead of pushing the work onto the user.
!macro EnsureEditHereClosed UNIQUE SCRIPTDIR
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "${SCRIPTDIR}\integrate.ps1" -Mode Check -InstallDirectory "$INSTDIR"'
    Pop $0
    Pop $1
    ${If} $0 == 0
        Goto ${UNIQUE}_done
    ${ElseIf} $0 != 2
        MessageBox MB_ICONSTOP "$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} ${Silent}
        StrCpy $2 90
    ${Else}
        StrCpy $2 10
    ${EndIf}
    ${UNIQUE}_wait:
        nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "${SCRIPTDIR}\integrate.ps1" -Mode Close -InstallDirectory "$INSTDIR" -TimeoutSeconds $2'
        Pop $0
        Pop $1
        ${If} $0 == 0
            Goto ${UNIQUE}_done
        ${EndIf}
        ${If} ${Silent}
            SetErrorLevel 1
            ; Skip in-app updates start the installer and quit; do not leave the
            ; user with a closed app when the update cannot be applied.
            ${If} $UpdateMode == 1
            ${AndIf} ${FileExists} "$INSTDIR\EditHere.exe"
                Exec '"$INSTDIR\EditHere.exe" --autostart'
            ${EndIf}
            Abort
        ${EndIf}
        StrCpy $2 60
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "EditHere 正在运行，安装程序无法替换正在使用的文件。$\r$\n$\r$\n请右键系统托盘中的 EditHere 图标选择「退出」。窗口可能已经被关闭，只在托盘里驻留。$\r$\n$\r$\n点「重试」继续等待最多 60 秒，点「取消」退出安装。" /SD IDCANCEL IDRETRY ${UNIQUE}_wait IDCANCEL ${UNIQUE}_cancel
    ${UNIQUE}_cancel:
        SetErrorLevel 1
        Abort
    ${UNIQUE}_done:
!macroend

Name "EditHere · 改这里"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\EditHere"
InstallDirRegKey HKCU "Software\EditHere\Installer" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID zlib
BrandingText "EditHere · 改这里"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "EditHere"
VIAddVersionKey /LANG=2052 "FileDescription" "EditHere · 改这里 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Inginnng"
!define MUI_ICON "${PROJECT_ROOT}\assets\icons\edithere.ico"
!define MUI_UNICON "${PROJECT_ROOT}\assets\icons\edithere.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\EditHere.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--autostart"
!define MUI_FINISHPAGE_RUN_TEXT "启动 EditHere（驻留系统托盘）"
!define MUI_FINISHPAGE_TEXT "安装完成。命令行：edithere-cli --help。已打开的终端或 Agent 需重新启动，才能读取更新后的 PATH。应用内设置可以管理开机自启。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Var StartupChoice
Var PathChoice
Var PreviousDirectory
Var UpdateMode
Var PortableMode
Var Parameters
Var StageDirectory
Var BackupDirectory
Var ParentDirectory
Section "EditHere 程序和 Agent skill（必需）" Core
    SectionIn RO
    SetShellVarContext current
    ${If} $PreviousDirectory != ""
    ${AndIf} $PreviousDirectory != $INSTDIR
    ${AndIf} $PortableMode != 1
        MessageBox MB_ICONSTOP "升级请保留原安装目录。若要迁移，请先卸载旧版。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=integrate.ps1 "${PROJECT_ROOT}\packaging\windows\integrate.ps1"
    !insertmacro EnsureEditHereClosed Core "$PLUGINSDIR"
    ; Use NSIS's Unicode file operations rather than a generated cmd/tar script.
    ; Prepare a complete sibling directory before touching the working copy.
    ${GetParent} "$INSTDIR" $ParentDirectory
    ${If} $ParentDirectory == ""
    ${OrIf} $ParentDirectory == $INSTDIR
        MessageBox MB_ICONSTOP "更新目录无效。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ClearErrors
    CreateDirectory "$ParentDirectory"
    GetTempFileName $StageDirectory "$ParentDirectory"
    Delete "$StageDirectory"
    CreateDirectory "$StageDirectory"
    IfErrors stage_failed
    ${If} ${FileExists} "$INSTDIR\*.*"
        CopyFiles /SILENT "$INSTDIR\*.*" "$StageDirectory"
        IfErrors stage_failed
    ${EndIf}
    SetOutPath "$StageDirectory"
    SetOverwrite on
    File /r "${PACKAGE_DIR}\*"
    IfErrors stage_failed
    ; Release the process's working directory before renaming directories.
    SetOutPath "$PLUGINSDIR"
    StrCpy $BackupDirectory ""
    ${If} ${FileExists} "$INSTDIR\*.*"
        GetTempFileName $BackupDirectory "$ParentDirectory"
        Delete "$BackupDirectory"
        IfErrors stage_failed
        Rename "$INSTDIR" "$BackupDirectory"
        IfErrors stage_failed
    ${EndIf}
    ClearErrors
!ifdef EDITHERE_TEST_FAIL_COMMIT
    ; Test-only fault injection: exercise recovery after the old directory has
    ; moved successfully. Production packaging never defines this symbol.
    SetErrors
!else
    Rename "$StageDirectory" "$INSTDIR"
!endif
    ${If} ${Errors}
        ${If} $BackupDirectory != ""
            ClearErrors
            Rename "$BackupDirectory" "$INSTDIR"
            ${If} ${Errors}
                MessageBox MB_ICONSTOP "更新未完成，旧版本保存在：$\r$\n$BackupDirectory$\r$\n请将此目录恢复到 $INSTDIR。" /SD IDOK
                SetErrorLevel 1
                Abort
            ${EndIf}
        ${EndIf}
        Goto stage_failed
    ${EndIf}
    ; Keep the backup intact, including any user-created files. It provides
    ; recovery even after a power loss or a later integration failure.
    ${If} $BackupDirectory != ""
        FileOpen $0 "$INSTDIR\update-backup.txt" w
        FileWriteUTF16LE $0 "$BackupDirectory$\r$\n"
        FileClose $0
    ${EndIf}
    ${If} $PortableMode == 1
        Exec '"$INSTDIR\EditHere.exe" --autostart'
        SetErrorLevel 0
        Quit
    ${EndIf}
    Goto stage_complete
    stage_failed:
        SetOutPath "$PLUGINSDIR"
        ; Do not delete any directory on failure: both copies may be needed to
        ; diagnose interrupted copies, low disk space or antivirus locks.
        MessageBox MB_ICONSTOP "更新未完成，原版本已保留。请检查磁盘空间和目录权限。$\r$\n暂存目录：$StageDirectory" /SD IDOK
        ${If} $UpdateMode == 1
        ${AndIf} ${FileExists} "$INSTDIR\EditHere.exe"
            Exec '"$INSTDIR\EditHere.exe" --autostart'
        ${EndIf}
        SetErrorLevel 1
        Abort
    stage_complete:
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\EditHere"
    CreateShortcut "$SMPROGRAMS\EditHere\EditHere.lnk" "$INSTDIR\EditHere.exe"
    CreateShortcut "$SMPROGRAMS\EditHere\卸载 EditHere.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd
Section "登录 Windows 后启动（仅驻留托盘）" Startup
SectionEnd
Section "将命令行加入当前用户 PATH" CommandPath
SectionEnd
Section /o "创建桌面快捷方式" Desktop
    CreateShortcut "$DESKTOP\EditHere.lnk" "$INSTDIR\EditHere.exe"
SectionEnd
Section -Integrate
    StrCpy $StartupChoice 0
    StrCpy $PathChoice 0
    ${If} ${SectionIsSelected} ${Startup}
        StrCpy $StartupChoice 1
    ${EndIf}
    ${If} ${SectionIsSelected} ${CommandPath}
        StrCpy $PathChoice 1
    ${EndIf}
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\integrate.ps1" -Mode Install -InstallDirectory "$INSTDIR" -Startup $StartupChoice -AddToPath $PathChoice'
    Pop $0
    Pop $1
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "系统集成未完成：$\r$\n$1$\r$\n可重新运行安装程序修复。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "DisplayName" "EditHere · 改这里"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "Publisher" "Inginnng"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "DisplayIcon" "$INSTDIR\EditHere.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere" "NoRepair" 1
    ${If} $UpdateMode == 1
        Exec '"$INSTDIR\EditHere.exe" --autostart'
        SetErrorLevel 0
        Quit
    ${EndIf}
SectionEnd
Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "EditHere 需要 64 位 Windows。" /SD IDOK
        Abort
    ${EndIf}
    SetRegView 64
    ReadRegStr $PreviousDirectory HKCU "Software\EditHere\Installer" "InstallDir"
    ${GetParameters} $Parameters
    StrCpy $UpdateMode 0
    ${GetOptions} $Parameters "/UPDATE" $1
    ${IfNot} ${Errors}
        StrCpy $UpdateMode 1
    ${EndIf}
    StrCpy $PortableMode 0
    ClearErrors
    ${GetOptions} $Parameters "/PORTABLE" $1
    ${IfNot} ${Errors}
        StrCpy $PortableMode 1
        ; /D= is parsed by NSIS itself. Require an existing portable app and
        ; never register it as an installation or modify another installation.
        ${If} $UpdateMode != 1
        ${OrIfNot} ${FileExists} "$INSTDIR\EditHere.exe"
        ${OrIf} $INSTDIR == $PreviousDirectory
            MessageBox MB_ICONSTOP "便携更新需要指定现有便携版目录。" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
        Return
    ${EndIf}
    ${If} $UpdateMode == 1
    ${AndIf} $PreviousDirectory != ""
        StrCpy $INSTDIR $PreviousDirectory
    ${EndIf}
    ${If} $PreviousDirectory != ""
        ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "EditHere"
        ${If} $0 == ""
            !insertmacro UnselectSection ${Startup}
        ${EndIf}
        ReadRegDWORD $0 HKCU "Software\EditHere\Installer" "AddedToPath"
        ${If} $0 != 1
            !insertmacro UnselectSection ${CommandPath}
        ${EndIf}
    ${EndIf}
    StrCpy $1 ""
    ${GetOptions} $Parameters "/STARTUP=" $1
    ${If} $1 == "0"
        !insertmacro UnselectSection ${Startup}
    ${ElseIf} $1 == "1"
        !insertmacro SelectSection ${Startup}
    ${EndIf}
    StrCpy $1 ""
    ${GetOptions} $Parameters "/ADDPATH=" $1
    ${If} $1 == "0"
        !insertmacro UnselectSection ${CommandPath}
    ${ElseIf} $1 == "1"
        !insertmacro SelectSection ${CommandPath}
    ${EndIf}
FunctionEnd
Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd
Section "Uninstall"
    !insertmacro EnsureEditHereClosed Uninst "$INSTDIR"
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\integrate.ps1" -Mode Uninstall -InstallDirectory "$INSTDIR"'
    Pop $0
    Pop $1
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    Delete "$SMPROGRAMS\EditHere\EditHere.lnk"
    Delete "$SMPROGRAMS\EditHere\卸载 EditHere.lnk"
    RMDir "$SMPROGRAMS\EditHere"
    Delete "$DESKTOP\EditHere.lnk"
    !include "${REMOVE_INCLUDE}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
