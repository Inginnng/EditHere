# -*- coding: utf-8 -*-
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"
!include "x64.nsh"
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
Section "EditHere 程序和 Agent skill（必需）" Core
    SectionIn RO
    SetShellVarContext current
    ${If} $PreviousDirectory != ""
    ${AndIf} $PreviousDirectory != $INSTDIR
        MessageBox MB_ICONSTOP "升级请保留原安装目录。若要迁移，请先卸载旧版。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=integrate.ps1 "${PROJECT_ROOT}\packaging\windows\integrate.ps1"
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\integrate.ps1" -Mode Check -InstallDirectory "$INSTDIR"'
    Pop $0
    Pop $1
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    SetOutPath "$INSTDIR"
    File /r "${PACKAGE_DIR}\*"
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
        Exec "$INSTDIR\EditHere.exe"
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
    ${GetParameters} $0
    StrCpy $UpdateMode 0
    ${GetOptions} $0 "/UPDATE" $1
    ${IfNot} ${Errors}
        StrCpy $UpdateMode 1
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
    ${EndIf}
    ${GetOptions} $0 "/STARTUP=" $1
    ${If} $1 == "0"
        !insertmacro UnselectSection ${Startup}
    ${ElseIf} $1 == "1"
        !insertmacro SelectSection ${Startup}
    ${EndIf}
    StrCpy $1 ""
    ${GetOptions} $0 "/ADDPATH=" $1
    ${If} $1 == "0"
        !insertmacro UnselectSection ${CommandPath}
    ${EndIf}
FunctionEnd
Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd
Section "Uninstall"
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\integrate.ps1" -Mode Check -InstallDirectory "$INSTDIR"'
    Pop $0
    Pop $1
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
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
