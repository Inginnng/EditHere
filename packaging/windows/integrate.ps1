param(
    [Parameter(Mandatory=$true)][ValidateSet('Check','Close','Install','Uninstall','Snapshot','Restore')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [ValidateSet('0','1')][string]$Startup='0',
    [ValidateSet('0','1')][string]$AddToPath='1',
    [string]$Version='',
    [string]$SnapshotPath='',
    [string]$ScopeRoot='',
    [string]$RegistryRoot='',
    [int]$TimeoutSeconds=0
)
$ErrorActionPreference='Stop'
$quitRequestSince=[version]'0.9.4'
function SamePath([string]$first,[string]$second) {
    return $first.Trim().Trim('"').TrimEnd('\') -ieq $second.TrimEnd('\')
}
function UnderScope([string]$path,[string]$scope) {
    $candidate=[IO.Path]::GetFullPath($path).TrimEnd('\')
    $boundary=[IO.Path]::GetFullPath($scope).TrimEnd('\')
    return $candidate -ieq $boundary -or $candidate.StartsWith($boundary+'\',[StringComparison]::OrdinalIgnoreCase)
}
function Get-EditHereProcesses {
    $target=[IO.Path]::GetFullPath((Join-Path $InstallDirectory 'EditHere.exe'))
    @(Get-Process -Name EditHere -ErrorAction SilentlyContinue | Where-Object {
        if (!$_.Path) { throw 'Cannot determine the path of a running EditHere process.' }
        [IO.Path]::GetFullPath($_.Path) -ieq $target
    })
}
function Test-QuitRequestSupported([string]$directory) {
    $versionFile=Join-Path $directory 'version.txt'
    if (!(Test-Path -LiteralPath $versionFile -PathType Leaf)) { return $false }
    $parsed=$null
    if (![version]::TryParse([IO.File]::ReadAllText($versionFile).Trim(),[ref]$parsed)) { return $false }
    return $parsed -ge $quitRequestSince
}
function Wait-EditHereExit([int]$seconds) {
    $deadline=(Get-Date).AddSeconds([Math]::Max(0,$seconds))
    while ((Get-EditHereProcesses).Count) {
        if ((Get-Date) -ge $deadline) { return $false }
        Start-Sleep -Milliseconds 400
    }
    return $true
}
function Read-Value([string]$keyPath,[string]$name) {
    $key=$user.OpenSubKey($keyPath)
    try {
        if (!$key) { return [pscustomobject]@{ keyPresent=$false; present=$false; kind=''; data=$null } }
        if (!(@($key.GetValueNames()) -contains $name)) {
            return [pscustomobject]@{ keyPresent=$true; present=$false; kind=''; data=$null }
        }
        $kind=$key.GetValueKind($name).ToString()
        $value=$key.GetValue($name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $data=switch ($kind) {
            'Binary' { [Convert]::ToBase64String([byte[]]$value) }
            'None' { [Convert]::ToBase64String([byte[]]$value) }
            'DWord' { ([int32]$value).ToString([Globalization.CultureInfo]::InvariantCulture) }
            'QWord' { ([int64]$value).ToString([Globalization.CultureInfo]::InvariantCulture) }
            'MultiString' { ,@([string[]]$value) }
            'String' { [string]$value }
            'ExpandString' { [string]$value }
            default { throw "Unsupported registry value kind: $kind" }
        }
        return [pscustomobject]@{ keyPresent=$true; present=$true; kind=$kind; data=$data }
    } finally { if($key){$key.Dispose()} }
}
function Set-SavedValue($key,[string]$name,$saved) {
    $kind=[Microsoft.Win32.RegistryValueKind]$saved.kind
    $value=switch ([string]$saved.kind) {
        'Binary' { ,[Convert]::FromBase64String([string]$saved.data) }
        'None' { ,[Convert]::FromBase64String([string]$saved.data) }
        'DWord' { [int32]::Parse([string]$saved.data,[Globalization.CultureInfo]::InvariantCulture) }
        'QWord' { [int64]::Parse([string]$saved.data,[Globalization.CultureInfo]::InvariantCulture) }
        'MultiString' { ,([string[]]@($saved.data)) }
        default { [string]$saved.data }
    }
    $key.SetValue($name,$value,$kind)
}
function Read-Tree([string]$keyPath) {
    $key=$user.OpenSubKey($keyPath)
    if(!$key){return [pscustomobject]@{ present=$false; values=@(); children=@() }}
    try {
        $values=@(foreach($name in $key.GetValueNames()) {
            [pscustomobject]@{ name=$name; value=(Read-Value $keyPath $name) }
        })
        $children=@(foreach($name in $key.GetSubKeyNames()) {
            [pscustomobject]@{ name=$name; tree=(Read-Tree ($keyPath+'\'+$name)) }
        })
        return [pscustomobject]@{ present=$true; values=$values; children=$children }
    } finally { $key.Dispose() }
}
function Write-Tree([string]$keyPath,$saved) {
    $key=$user.CreateSubKey($keyPath)
    try { foreach($entry in @($saved.values)){Set-SavedValue $key ([string]$entry.name) $entry.value} }
    finally { $key.Dispose() }
    foreach($child in @($saved.children)) {
        if([string]$child.name -match '[\\/]'){throw 'Invalid registry snapshot child.'}
        Write-Tree ($keyPath+'\'+$child.name) $child.tree
    }
}
function Restore-Tree([string]$keyPath,$saved) {
    $user.DeleteSubKeyTree($keyPath,$false)
    if($saved.present){Write-Tree $keyPath $saved}
}
function Restore-Value([string]$keyPath,[string]$name,$saved) {
    if($saved.present) {
        $key=$user.CreateSubKey($keyPath)
        try { Set-SavedValue $key $name $saved } finally { $key.Dispose() }
    } else {
        $key=$user.OpenSubKey($keyPath,$true)
        if($key) {
            try { $key.DeleteValue($name,$false); $empty=$key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0 }
            finally { $key.Dispose() }
            if(!$saved.keyPresent -and $empty){$user.DeleteSubKey($keyPath,$false)}
        }
    }
}
function Read-PathState {
    $saved=Read-Value $environmentKey 'Path'
    if($saved.present -and $saved.kind -notin @('String','ExpandString')){throw 'PATH registry type is not supported.'}
    $raw=if($saved.present){[string]$saved.data}else{''}
    # Keep a single PATH token as an array: PowerShell otherwise unwraps it to
    # a string and indexed snapshot enumeration sees its first character.
    [string[]]$tokens=@()
    if($raw.Length){$tokens=$raw.Split([char]';')}
    return [pscustomobject]@{ value=$saved; raw=$raw; tokens=$tokens }
}
function Restore-OwnPathToken($saved) {
    $current=Read-PathState
    $tokens=[Collections.Generic.List[string]]::new()
    foreach($token in @($current.tokens)){if(!(SamePath $token $root)){$tokens.Add($token)}}
    foreach($entry in @($saved.ownTokens)) {
        $tokens.Insert([Math]::Min([int]$entry.index,$tokens.Count),[string]$entry.token)
    }
    if(!$saved.value.present -and $tokens.Count -eq 0) {
        $key=$user.OpenSubKey($environmentKey,$true)
        if($key){try{$key.DeleteValue('Path',$false)}finally{$key.Dispose()}}
        return
    }
    if(!$current.value.present -and $tokens.Count -eq 0){return}
    $kind=if($current.value.present){[Microsoft.Win32.RegistryValueKind]$current.value.kind}
          elseif($saved.value.present){[Microsoft.Win32.RegistryValueKind]$saved.value.kind}
          else{[Microsoft.Win32.RegistryValueKind]::ExpandString}
    $key=$user.CreateSubKey($environmentKey)
    try{$key.SetValue('Path',([string]::Join(';',$tokens.ToArray())),$kind)}finally{$key.Dispose()}
}
function Notify-Shell {
    if($RegistryRoot){return}
    try {
        if(!('EditHereShell' -as [type])) {
            # NSIS's plugin working directory contains a native System.dll.
            # Windows PowerShell 5's compiler otherwise resolves its implicit
            # System.dll reference there and rejects it as managed metadata.
            # Anchor every framework reference to the running .NET runtime.
            $framework=[Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()
            $references=@([object].Assembly.Location,
                [IO.Path]::Combine($framework,'System.dll'),
                [IO.Path]::Combine($framework,'System.Core.dll'))
            Add-Type -ReferencedAssemblies $references -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class EditHereShell { [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, string l, uint f, uint t, out UIntPtr r); [DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b); }'
        }
        $result=[UIntPtr]::Zero
        [void][EditHereShell]::SendMessageTimeout([IntPtr]0xffff,0x1a,[UIntPtr]::Zero,'Environment',2,5000,[ref]$result)
        [EditHereShell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
    } catch {
        # Registry changes/restoration have already succeeded. Notification is
        # best effort; its failure must not abort installation or rollback.
        Write-Warning ('系统设置已保存，但 Windows 通知未完成：'+$_.Exception.Message)
    }
}
try {
    $root=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
    if($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\') -or $root.Contains(';')){throw '安装路径无效。'}
    if($RegistryRoot -or $ScopeRoot) {
        if(!$ScopeRoot -or $RegistryRoot -notmatch '^Software\\EditHere\\InstallerTests\\[A-Za-z0-9_-]{6,80}$' -or
           !(Test-Path -LiteralPath (Join-Path $ScopeRoot '.edithere-maintenance-fixture') -PathType Leaf) -or
           !(UnderScope $root $ScopeRoot) -or ($SnapshotPath -and !(UnderScope $SnapshotPath $ScopeRoot))) {
            throw 'Invalid isolated integration fixture.'
        }
    }
    if($Mode -eq 'Check') {
        if((Get-EditHereProcesses).Count){[Console]::Error.WriteLine('EditHere 正在运行。');exit 2}
        exit 0
    }
    if($Mode -eq 'Close') {
        if(!(Get-EditHereProcesses).Count){exit 0}
        $exe=Join-Path $root 'EditHere.exe'
        if((Test-Path -LiteralPath $exe -PathType Leaf) -and (Test-QuitRequestSupported $root)) {
            $asked=Start-Process -FilePath $exe -ArgumentList '--quit' -PassThru -WindowStyle Hidden
            [void]$asked.WaitForExit(20000)
        }
        if(Wait-EditHereExit $TimeoutSeconds) {
            Start-Sleep -Milliseconds 400
            if(!(Get-EditHereProcesses).Count){exit 0}
        }
        [Console]::Error.WriteLine('EditHere 仍在运行，请右键系统托盘中的 EditHere 图标选择「退出」。')
        exit 2
    }
    $user=[Microsoft.Win32.Registry]::CurrentUser
    $environmentKey=if($RegistryRoot){$RegistryRoot+'\Environment'}else{'Environment'}
    $appKey=if($RegistryRoot){$RegistryRoot+'\Installer'}else{'Software\EditHere\Installer'}
    $runKey=if($RegistryRoot){$RegistryRoot+'\Run'}else{'Software\Microsoft\Windows\CurrentVersion\Run'}
    $uninstallKey=if($RegistryRoot){$RegistryRoot+'\Uninstall\EditHere'}else{'Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere'}
    $classes=if($RegistryRoot){$RegistryRoot+'\Classes'}else{'Software\Classes'}
    $handlerKey=$classes+'\EditHere.Project'
    $extensionKey=$classes+'\.edithere'
    $openWithKey=$extensionKey+'\OpenWithProgids'
    $exe=Join-Path $root 'EditHere.exe'
    $command='"'+$exe+'" --autostart'
    $meta=$user.OpenSubKey($appKey)
    $previousRoot=if($meta){[string]$meta.GetValue('InstallDir','')}else{''}
    $addedBefore=if($meta){[int]$meta.GetValue('AddedToPath',0)}else{0}
    if($meta){$meta.Dispose()}
    $uninstall=$user.OpenSubKey($uninstallKey)
    $registeredLocation=if($uninstall){[string]$uninstall.GetValue('InstallLocation','')}else{''}
    if($uninstall){$uninstall.Dispose()}
    if($Mode -ne 'Restore' -and (($previousRoot -and !(SamePath $previousRoot $root)) -or
       ($registeredLocation -and !(SamePath $registeredLocation $root)))){throw '安装目录与现有登记不一致，未修改系统设置。'}
    if($Mode -eq 'Snapshot') {
        if(!$SnapshotPath){throw 'SnapshotPath is required.'}
        $pathState=Read-PathState
        $ownTokens=@(for($index=0;$index -lt $pathState.tokens.Count;$index++) {
            if(SamePath $pathState.tokens[$index] $root){[pscustomobject]@{index=$index;token=$pathState.tokens[$index]}}
        })
        $snapshot=[pscustomobject]@{schema=1;installDirectory=$root;registryRoot=$RegistryRoot;
            trees=[pscustomobject]@{installer=(Read-Tree $appKey);uninstall=(Read-Tree $uninstallKey);handler=(Read-Tree $handlerKey)};
            values=[pscustomobject]@{run=(Read-Value $runKey 'EditHere');extension=(Read-Value $extensionKey '');openWith=(Read-Value $openWithKey 'EditHere.Project')};
            path=[pscustomobject]@{value=$pathState.value;ownTokens=$ownTokens}}
        $destination=[IO.Path]::GetFullPath($SnapshotPath)
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        $temporary=$destination+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
        [IO.File]::WriteAllText($temporary,($snapshot | ConvertTo-Json -Depth 64),[Text.UTF8Encoding]::new($true))
        if([IO.File]::Exists($destination)) {
            $previous=$temporary+'.previous'
            try {[IO.File]::Replace($temporary,$destination,$previous)}
            finally {if([IO.File]::Exists($previous)){[IO.File]::Delete($previous)}}
        } else {[IO.File]::Move($temporary,$destination)}
        exit 0
    }
    if($Mode -eq 'Restore') {
        if(!$SnapshotPath){throw 'SnapshotPath is required.'}
        $saved=[IO.File]::ReadAllText($SnapshotPath) | ConvertFrom-Json
        if($saved.schema -ne 1 -or !(SamePath ([string]$saved.installDirectory) $root) -or [string]$saved.registryRoot -cne $RegistryRoot){throw 'Registry snapshot identity mismatch.'}
        Restore-Tree $appKey $saved.trees.installer
        Restore-Tree $uninstallKey $saved.trees.uninstall
        Restore-Tree $handlerKey $saved.trees.handler
        Restore-Value $runKey 'EditHere' $saved.values.run
        Restore-Value $openWithKey 'EditHere.Project' $saved.values.openWith
        Restore-Value $extensionKey '' $saved.values.extension
        Restore-OwnPathToken $saved.path
        Notify-Shell
        exit 0
    }
    $pathState=Read-PathState
    $parts=[Collections.Generic.List[string]]::new()
    foreach($part in @($pathState.tokens)){$parts.Add($part)}
    $pathKind=if($pathState.value.present){[Microsoft.Win32.RegistryValueKind]$pathState.value.kind}else{[Microsoft.Win32.RegistryValueKind]::ExpandString}
    if($Mode -eq 'Install') {
        if(!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $root 'edithere-cli.exe') -PathType Leaf)){throw '安装文件不完整。'}
        if($Version -notmatch '^\d+\.\d+\.\d+$'){throw '安装版本无效。'}
        if($Startup -eq '1' -and $command.Length -gt 260){throw '登录启动命令超过 Windows 长度限制，请使用更短的安装目录。'}
        $added=$addedBefore
        if($AddToPath -eq '1' -and !(@($parts | Where-Object {SamePath $_ $root}).Count)) {
            $parts.Add($root);$added=1
        } elseif($AddToPath -eq '0' -and $addedBefore) {
            $kept=@($parts | Where-Object {!(SamePath $_ $root)});$parts.Clear()
            foreach($part in $kept){$parts.Add($part)};$added=0
        }
        if([string]::Join(';',$parts.ToArray()) -cne $pathState.raw) {
            $pathKey=$user.CreateSubKey($environmentKey)
            try{$pathKey.SetValue('Path',([string]::Join(';',$parts.ToArray())),$pathKind)}finally{$pathKey.Dispose()}
        }
        $run=$user.CreateSubKey($runKey)
        try {if($Startup -eq '1'){$run.SetValue('EditHere',$command,[Microsoft.Win32.RegistryValueKind]::String)}
             elseif([string]$run.GetValue('EditHere','') -eq $command){$run.DeleteValue('EditHere',$false)}}finally{$run.Dispose()}
        $handler=$user.CreateSubKey($handlerKey);try{$handler.SetValue('','EditHere 项目')}finally{$handler.Dispose()}
        $icon=$user.CreateSubKey($handlerKey+'\DefaultIcon');try{$icon.SetValue('','"'+$exe+'",0')}finally{$icon.Dispose()}
        $open=$user.CreateSubKey($handlerKey+'\shell\open\command');try{$open.SetValue('','"'+$exe+'" "%1"')}finally{$open.Dispose()}
        $merged=if($RegistryRoot){$user.OpenSubKey($extensionKey)}else{[Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('.edithere')}
        $oldDefault=if($merged){[string]$merged.GetValue('','')}else{''}
        if($merged){$merged.Dispose()}
        $extension=$user.CreateSubKey($extensionKey);try{if(!$oldDefault){$extension.SetValue('','EditHere.Project')}}finally{$extension.Dispose()}
        $with=$user.CreateSubKey($openWithKey);try{$with.SetValue('EditHere.Project','')}finally{$with.Dispose()}
        $meta=$user.CreateSubKey($appKey)
        try{$meta.SetValue('InstallDir',$root);$meta.SetValue('AddedToPath',$added,[Microsoft.Win32.RegistryValueKind]::DWord);$meta.SetValue('Version',$Version)}finally{$meta.Dispose()}
        $uninstall=$user.CreateSubKey($uninstallKey)
        try {
            $uninstall.SetValue('DisplayName','EditHere · 改这里');$uninstall.SetValue('DisplayVersion',$Version)
            $uninstall.SetValue('Publisher','Inginnng');$uninstall.SetValue('InstallLocation',$root)
            $uninstall.SetValue('DisplayIcon',$exe);$uninstall.SetValue('UninstallString','"'+(Join-Path $root 'Uninstall.exe')+'"')
            $uninstall.SetValue('QuietUninstallString','"'+(Join-Path $root 'Uninstall.exe')+'" /S')
            $uninstall.SetValue('NoModify',1,[Microsoft.Win32.RegistryValueKind]::DWord)
            $uninstall.SetValue('NoRepair',1,[Microsoft.Win32.RegistryValueKind]::DWord)
        } finally {$uninstall.Dispose()}
    } elseif($Mode -eq 'Uninstall') {
        # A missing registration is a legitimate interrupted-uninstall state.
        # Never remove another installation's values or unrelated associations.
        if($addedBefore) {
            $kept=@($parts | Where-Object {!(SamePath $_ $root)})
            $pathKey=$user.CreateSubKey($environmentKey)
            try{$pathKey.SetValue('Path',([string]::Join(';',[string[]]$kept)),$pathKind)}finally{$pathKey.Dispose()}
        }
        $run=$user.OpenSubKey($runKey,$true)
        if($run){try{if([string]$run.GetValue('EditHere','') -eq $command){$run.DeleteValue('EditHere',$false)}}finally{$run.Dispose()}}
        $open=$user.OpenSubKey($handlerKey+'\shell\open\command')
        $ownHandler=$open -and [string]$open.GetValue('','') -eq ('"'+$exe+'" "%1"')
        if($open){$open.Dispose()}
        if($ownHandler) {
            $user.DeleteSubKeyTree($handlerKey,$false)
            $extension=$user.OpenSubKey($extensionKey,$true)
            if($extension){try{if([string]$extension.GetValue('','') -eq 'EditHere.Project'){$extension.DeleteValue('',$false)}}finally{$extension.Dispose()}}
            $with=$user.OpenSubKey($openWithKey,$true)
            if($with){try{$with.DeleteValue('EditHere.Project',$false)}finally{$with.Dispose()}}
        }
        $user.DeleteSubKeyTree($appKey,$false)
        $user.DeleteSubKeyTree($uninstallKey,$false)
    }
    Notify-Shell
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
