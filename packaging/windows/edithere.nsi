# -*- coding: utf-8 -*-
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"
!include "x64.nsh"
!include "WordFunc.nsh"
!include "TextFunc.nsh"

# Registry reads use the same guarded fixture scope as the maintenance worker.
!ifdef EDITHERE_TEST_SCOPE_ROOT
!define INSTALLER_REGISTRY "${EDITHERE_TEST_REGISTRY_ROOT}\Installer"
!define RUN_REGISTRY "${EDITHERE_TEST_REGISTRY_ROOT}\Run"
!define UNINSTALL_REGISTRY "${EDITHERE_TEST_REGISTRY_ROOT}\Uninstall\EditHere"
!else
!define INSTALLER_REGISTRY "Software\EditHere\Installer"
!define RUN_REGISTRY "Software\Microsoft\Windows\CurrentVersion\Run"
!define UNINSTALL_REGISTRY "Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere"
!endif

!macro WorkerContext
    StrCpy $WorkerArguments '-InstallDirectory "$INSTDIR" -Version "${APP_VERSION}" -InstallerPath "$EXEPATH"'
!ifdef EDITHERE_TEST_SCOPE_ROOT
    StrCpy $WorkerArguments '$WorkerArguments -ScopeRoot "${EDITHERE_TEST_SCOPE_ROOT}" -DataRoot "${EDITHERE_TEST_DATA_ROOT}" -RegistryRoot "${EDITHERE_TEST_REGISTRY_ROOT}"'
!endif
!ifdef EDITHERE_TEST_SKIP_LAUNCH_VALIDATION
    StrCpy $WorkerArguments '$WorkerArguments -SkipLaunchValidation'
!endif
!ifdef EDITHERE_TEST_FAIL_COMMIT
    StrCpy $WorkerArguments '$WorkerArguments -TestFailure IntegrationFailure'
!endif
!macroend

Name "EditHere"
Caption "EditHere 安装"
OutFile "${OUTPUT_FILE}"
!ifdef EDITHERE_TEST_SCOPE_ROOT
InstallDir "${EDITHERE_TEST_SCOPE_ROOT}\default"
!else
InstallDir "$LOCALAPPDATA\Programs\EditHere"
!endif
InstallDirRegKey HKCU "${INSTALLER_REGISTRY}" "InstallDir"
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
!define MUI_CUSTOMFUNCTION_ABORT ReportCancelledHandoff
!define MUI_FINISHPAGE_RUN "$INSTDIR\EditHere.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--autostart"
!define MUI_FINISHPAGE_RUN_TEXT "启动 EditHere（驻留系统托盘）"
!define MUI_FINISHPAGE_TEXT "EditHere 已安装，可以开始使用。"
Page custom ShowUpdateConfirmation
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipInstallPageForUpdate
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipInstallPageForUpdate
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipInstallPageForUpdate
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipInstallPageForUpdate
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ShowUpdateProgress
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ShowCompletion
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Var StartupChoice
Var PathChoice
Var PreviousDirectory
Var UpdateMode
Var AutomaticUpdate
Var PreviousVersion
Var PortableMode
Var Parameters
Var StageDirectory
Var ParentDirectory
Var FailureStep
Var HandoffId
Var WorkerArguments
Var WorkerStarted
Var WorkerReturn
Var DesktopChoice
Var RecoveryLaunchAttempted
Var DirectoryAllowed
Var RegisteredDirectoryMatches
Var FailureSummary

Function SkipInstallPageForUpdate
    ${If} $UpdateMode == 1
        Abort
    ${EndIf}
FunctionEnd

Function ShowUpdateProgress
    ${If} $UpdateMode == 1
        !insertmacro MUI_HEADER_TEXT "正在更新 EditHere" "正在安装程序文件，请稍候。"
    ${Else}
        !insertmacro MUI_HEADER_TEXT "正在安装 EditHere" "正在安装程序文件，请稍候。"
    ${EndIf}
FunctionEnd

Function ShowUpdateConfirmation
    ${If} $UpdateMode != 1
    ${OrIf} $AutomaticUpdate == 1
        Abort
    ${EndIf}
    !insertmacro MUI_HEADER_TEXT "更新现有版本" "更新程序文件，保留您的设置、项目和安装选项。"
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}
    ${NSD_CreateLabel} 0 8u 100% 20u "已安装 EditHere，点击下面的按钮即可更新现有版本。"
    Pop $0
    ${NSD_CreateLabel} 0 42u 100% 14u "当前版本：$PreviousVersion"
    Pop $0
    ${NSD_CreateLabel} 0 62u 100% 14u "安装包版本：${APP_VERSION}"
    Pop $0
    ${NSD_CreateLabel} 0 90u 100% 40u "安装位置：$INSTDIR"
    Pop $0
    GetDlgItem $0 $HWNDPARENT 1
    SendMessage $0 ${WM_SETTEXT} 0 "STR:更新现有版本"
    nsDialogs::Show
FunctionEnd

Function ShowCompletion
    ${If} $UpdateMode == 1
        SendMessage $mui.FinishPage.Title ${WM_SETTEXT} 0 "STR:EditHere 更新完成"
        SendMessage $mui.FinishPage.Text ${WM_SETTEXT} 0 "STR:现有版本已更新，可以继续使用 EditHere。"
    ${Else}
        SendMessage $mui.FinishPage.Title ${WM_SETTEXT} 0 "STR:EditHere 安装完成"
    ${EndIf}
FunctionEnd

Function CompareRegisteredDirectory
    StrCpy $RegisteredDirectoryMatches 0
    ${If} $PreviousDirectory == ""
        Return
    ${EndIf}
    Push $0
    Push $1
    Push $2
    GetFullPathName $0 "$PreviousDirectory"
    GetFullPathName $1 "$INSTDIR"
    ; Normalize Windows-equivalent paths for updater identity checks.
    StrCpy $2 $0 1 -1
    ${If} $2 == "\"
        StrCpy $0 $0 -1
    ${EndIf}
    StrCpy $2 $1 1 -1
    ${If} $2 == "\"
        StrCpy $1 $1 -1
    ${EndIf}
    ${If} $0 == $1
        StrCpy $RegisteredDirectoryMatches 1
    ${EndIf}
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

Function ValidateRegisteredDirectory
    StrCpy $DirectoryAllowed 1
    ${If} $PreviousDirectory == ""
    ${OrIf} $PortableMode == 1
    ${OrIf} $HandoffId == ""
        Return
    ${EndIf}
    Call CompareRegisteredDirectory
    ${If} $RegisteredDirectoryMatches != 1
        StrCpy $DirectoryAllowed 0
        StrCpy $FailureStep "更新请求与现有安装记录不一致，现有版本未被修改。"
    ${EndIf}
FunctionEnd

Function ReadPreviousVersion
    StrCpy $PreviousVersion ""
    ClearErrors
    FileOpen $0 "$INSTDIR\version.txt" r
    ${IfNot} ${Errors}
        FileRead $0 $PreviousVersion
        FileClose $0
        ${TrimNewLines} $PreviousVersion $PreviousVersion
    ${EndIf}
    ${If} $PreviousVersion == ""
        ClearErrors
        GetDLLVersion "$INSTDIR\EditHere.exe" $0 $1
        ${IfNot} ${Errors}
            IntOp $2 $0 >> 16
            IntOp $3 $0 & 0xFFFF
            IntOp $4 $1 >> 16
            StrCpy $PreviousVersion "$2.$3.$4"
        ${EndIf}
    ${EndIf}
    ${If} $PreviousVersion == ""
    ${AndIf} $PortableMode != 1
        ReadRegStr $PreviousVersion HKCU "${UNINSTALL_REGISTRY}" "DisplayVersion"
    ${EndIf}
    ${If} $PreviousVersion != ""
        ${VersionCompare} "$PreviousVersion" "${APP_VERSION}" $0
        ${If} $0 == 1
            StrCpy $FailureStep "已安装更新版本的 EditHere（$PreviousVersion）。请使用最新安装包。"
            Call ReportEarlyFailure
            MessageBox MB_ICONINFORMATION "$FailureStep" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${Else}
        StrCpy $PreviousVersion "已安装"
    ${EndIf}
FunctionEnd

Function PrepareMaintenanceWorker
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File /oname=maintain.ps1 "${PROJECT_ROOT}\packaging\windows\maintain.ps1"
    File /oname=integrate.ps1 "${PROJECT_ROOT}\packaging\windows\integrate.ps1"
FunctionEnd

Function ReportEarlyFailure
    Call RestartPreviousApplication
    ${If} $HandoffId == ""
    ${OrIf} $WorkerStarted == 1
        Return
    ${EndIf}
    Push $0
    Push $1
    !insertmacro WorkerContext
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\maintain.ps1" -Mode FailHandoff $WorkerArguments -HandoffId "$HandoffId" -Message "$FailureStep"'
    Pop $0
    Pop $1
    Pop $1
    Pop $0
FunctionEnd

Function RestartPreviousApplication
!ifndef EDITHERE_TEST_SCOPE_ROOT
    ${If} $UpdateMode != 1
    ${OrIf} $DirectoryAllowed == 0
    ${OrIf} $RecoveryLaunchAttempted == 1
    ${OrIfNot} ${FileExists} "$INSTDIR\EditHere.exe"
        Return
    ${EndIf}
    ; Cancelling a manual confirmation or rejecting an older package must
    ; not launch the application before any maintenance has begun.
    ${If} $AutomaticUpdate != 1
    ${AndIf} $WorkerStarted != 1
        Return
    ${EndIf}
    ; Current applications stay open until ACK and restart only after the
    ; worker confirms recovery. Older updaters exited before starting NSIS.
    ${If} $HandoffId != ""
        ${If} $WorkerReturn != 3
            Return
        ${EndIf}
    ${Else}
        ${If} $WorkerReturn != ""
        ${AndIf} $WorkerReturn != 1
        ${AndIf} $WorkerReturn != 3
        ${AndIf} $WorkerReturn != "error"
        ${AndIf} $WorkerReturn != "timeout"
            Return
        ${EndIf}
    ${EndIf}
    StrCpy $RecoveryLaunchAttempted 1
    Exec '"$INSTDIR\EditHere.exe" --autostart'
!endif
FunctionEnd

Function ReportCancelledHandoff
    StrCpy $FailureStep "用户取消了安装，现有版本未被修改。"
    Call ReportEarlyFailure
FunctionEnd

Section "EditHere 程序和 Agent skill（必需）" Core
    SectionIn RO
    SetShellVarContext current
    ; Silent installs and callers skipping pages use the same directory guard,
    ; before creating a staging directory or extracting the application payload.
    Call ValidateRegisteredDirectory
    ${If} $DirectoryAllowed != 1
        Goto stage_failed
    ${EndIf}
    ; Extract a fresh complete payload. The worker uses the ownership manifest
    ; to retain user files, remove obsolete application files and recover crashes.
    ${GetParent} "$INSTDIR" $ParentDirectory
    ${If} $ParentDirectory == ""
    ${OrIf} $ParentDirectory == $INSTDIR
        StrCpy $FailureStep "安装目录无效。"
        Goto stage_failed
    ${EndIf}
    StrCpy $FailureStep "无法创建暂存目录。请检查磁盘空间和目录写入权限。"
    ClearErrors
    CreateDirectory "$ParentDirectory"
    GetTempFileName $StageDirectory "$ParentDirectory"
    Delete "$StageDirectory"
    CreateDirectory "$StageDirectory"
    IfErrors stage_failed
    StrCpy $FailureStep "解压程序文件失败。请检查磁盘空间、目录权限及杀毒软件拦截。"
    SetOutPath "$StageDirectory"
    SetOverwrite on
    File /r "${PACKAGE_DIR}\*"
    IfErrors stage_failed
    ${If} $PortableMode != 1
        StrCpy $FailureStep "无法生成卸载程序。请检查磁盘空间和目录写入权限。"
        WriteUninstaller "$StageDirectory\Uninstall.exe"
        IfErrors stage_failed
    ${EndIf}
    ; Keep the worker independent of the selected install directory.
    SetOutPath "$PLUGINSDIR"
    Goto stage_complete
    stage_failed:
        SetOutPath "$PLUGINSDIR"
        Call ReportEarlyFailure
        StrCpy $FailureSummary "安装准备未完成，目标目录中的现有文件未被修改。$\r$\n$FailureStep"
        ${If} $StageDirectory != ""
            StrCpy $FailureSummary "$FailureSummary$\r$\n目标目录：$INSTDIR$\r$\n暂存目录：$StageDirectory"
        ${EndIf}
        MessageBox MB_ICONSTOP "$FailureSummary" /SD IDOK
        SetErrorLevel 1
        Abort
    stage_complete:
SectionEnd
Section "登录 Windows 后启动（仅驻留托盘）" Startup
SectionEnd
Section "将命令行加入当前用户 PATH" CommandPath
SectionEnd
Section /o "创建桌面快捷方式" Desktop
SectionEnd
Section -Maintain
    StrCpy $StartupChoice 0
    StrCpy $PathChoice 0
    StrCpy $DesktopChoice 0
    ${If} ${SectionIsSelected} ${Startup}
        StrCpy $StartupChoice 1
    ${EndIf}
    ${If} ${SectionIsSelected} ${CommandPath}
        StrCpy $PathChoice 1
    ${EndIf}
    ${If} ${SectionIsSelected} ${Desktop}
        StrCpy $DesktopChoice 1
    ${EndIf}
    !insertmacro WorkerContext
    StrCpy $WorkerArguments '$WorkerArguments -StageDirectory "$StageDirectory" -Startup $StartupChoice -AddToPath $PathChoice -Desktop $DesktopChoice -Portable $PortableMode -HandoffId "$HandoffId"'
    StrCpy $WorkerStarted 1
    ; Cancellation is handled before READY/ACK. Once the synchronous worker
    ; owns the transaction it must commit or restore it before the UI exits.
    GetDlgItem $0 $HWNDPARENT 2
    EnableWindow $0 0
    DetailPrint "正在安装程序文件，请稍候..."
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\maintain.ps1" -Mode Install $WorkerArguments'
    Pop $WorkerReturn
    Pop $1
    ${If} $WorkerReturn == "error"
    ${OrIf} $WorkerReturn == "timeout"
        StrCpy $WorkerStarted 0
        StrCpy $FailureStep "无法启动维护程序，请检查 PowerShell 和杀毒软件拦截。"
        Call ReportEarlyFailure
        StrCpy $WorkerStarted 1
        StrCpy $1 "$FailureStep"
    ${EndIf}
    GetDlgItem $0 $HWNDPARENT 2
    EnableWindow $0 1
    ${If} $WorkerReturn != 0
        StrCpy $FailureStep "$1"
        Call RestartPreviousApplication
        MessageBox MB_ICONSTOP "安装未完成：$\r$\n$1$\r$\n目标目录：$INSTDIR$\r$\n诊断日志：$LOCALAPPDATA\EditHere\Installer" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $PortableMode == 1
    ${OrIf} $AutomaticUpdate == 1
!ifndef EDITHERE_TEST_SCOPE_ROOT
        Exec '"$INSTDIR\EditHere.exe" --autostart'
!endif
        SetErrorLevel 0
        Quit
    ${EndIf}
SectionEnd
Function .onInit
    SetRegView 64
    SetShellVarContext current
    ${GetParameters} $Parameters
    StrCpy $WorkerStarted 0
    StrCpy $RecoveryLaunchAttempted 0
    StrCpy $HandoffId ""
    StrCpy $FailureStep "安装器准备失败，请重新运行安装包。"
    ${GetOptions} $Parameters "/HANDOFF=" $HandoffId
    Call PrepareMaintenanceWorker
    ${IfNot} ${RunningX64}
        StrCpy $FailureStep "EditHere 需要 64 位 Windows。"
        Call ReportEarlyFailure
        MessageBox MB_ICONSTOP "$FailureStep" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    StrCpy $UpdateMode 0
    StrCpy $AutomaticUpdate 0
    ClearErrors
    ${GetOptions} $Parameters "/UPDATE" $1
    ${IfNot} ${Errors}
        StrCpy $UpdateMode 1
        StrCpy $AutomaticUpdate 1
    ${EndIf}
    ${If} $HandoffId != ""
        StrCpy $UpdateMode 1
        StrCpy $AutomaticUpdate 1
    ${EndIf}
    StrCpy $PortableMode 0
    ClearErrors
    ${GetOptions} $Parameters "/PORTABLE" $1
    ${IfNot} ${Errors}
        StrCpy $PortableMode 1
    ${EndIf}
    ReadRegStr $PreviousDirectory HKCU "${INSTALLER_REGISTRY}" "InstallDir"
    Call CompareRegisteredDirectory
    Call ValidateRegisteredDirectory
    ${If} $PortableMode == 1
        ; Leave interrupted-install recovery and payload ownership checks to
        ; the worker; the executable may be temporarily absent after a crash.
        ${If} $UpdateMode != 1
        ${OrIf} $RegisteredDirectoryMatches == 1
            StrCpy $FailureStep "便携更新需要指定现有便携版目录。"
            Call ReportEarlyFailure
            MessageBox MB_ICONSTOP "$FailureStep" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
        Call ReadPreviousVersion
        Return
    ${EndIf}
    ; An existing installation always follows its recorded directory. In-app
    ; handoffs instead retain their requested identity and validate it above.
    ${If} $PreviousDirectory != ""
        ${If} $HandoffId == ""
            StrCpy $INSTDIR "$PreviousDirectory"
        ${ElseIf} $DirectoryAllowed != 1
            Call ReportEarlyFailure
            MessageBox MB_ICONSTOP "$FailureStep" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
        StrCpy $UpdateMode 1
        Call ReadPreviousVersion
        ReadRegStr $0 HKCU "${RUN_REGISTRY}" "EditHere"
        ${If} $0 == ""
            !insertmacro UnselectSection ${Startup}
        ${EndIf}
        ReadRegDWORD $0 HKCU "${INSTALLER_REGISTRY}" "AddedToPath"
        ${If} $0 != 1
            !insertmacro UnselectSection ${CommandPath}
        ${EndIf}
        ; Preserve the existing desktop choice on update/repair. The worker
        ; validates shortcut ownership before applying any changes.
!ifdef EDITHERE_TEST_SCOPE_ROOT
        ${If} ${FileExists} "${EDITHERE_TEST_SCOPE_ROOT}\Shell\Desktop\EditHere.lnk"
!else
        ${If} ${FileExists} "$DESKTOP\EditHere.lnk"
!endif
            !insertmacro SelectSection ${Desktop}
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
    StrCpy $1 ""
    ${GetOptions} $Parameters "/DESKTOP=" $1
    ${If} $1 == "0"
        !insertmacro UnselectSection ${Desktop}
    ${ElseIf} $1 == "1"
        !insertmacro SelectSection ${Desktop}
    ${EndIf}
FunctionEnd
Function un.onInit
    SetRegView 64
    SetShellVarContext current
FunctionEnd
Section "Uninstall"
    ; The worker must remain available after the install directory is removed.
    ; NSIS already relocates the uninstaller image; copy its scripts as well.
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    ClearErrors
    CopyFiles /SILENT "$INSTDIR\maintain.ps1" "$PLUGINSDIR\maintain.ps1"
    CopyFiles /SILENT "$INSTDIR\integrate.ps1" "$PLUGINSDIR\integrate.ps1"
    ${If} ${Errors}
        MessageBox MB_ICONSTOP "无法准备卸载维护程序。请重新运行安装包修复后卸载。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    !insertmacro WorkerContext
    GetDlgItem $0 $HWNDPARENT 2
    EnableWindow $0 0
    nsExec::ExecToStack '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\maintain.ps1" -Mode Uninstall $WorkerArguments'
    Pop $0
    Pop $1
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "卸载未完成：$\r$\n$1$\r$\n可重新运行卸载程序重试。" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    SetErrorLevel 0
SectionEnd
