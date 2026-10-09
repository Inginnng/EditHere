param(
    [Parameter(Mandatory=$true)][ValidateSet('Install','Uninstall','Recover','FailHandoff')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [string]$StageDirectory='',
    [string]$InstallerPath='',
    [ValidateSet('0','1')][string]$Portable='0',
    [ValidateSet('0','1')][string]$Startup='0',
    [ValidateSet('0','1')][string]$AddToPath='1',
    [ValidateSet('0','1')][string]$Desktop='0',
    [string]$Version='', [string]$HandoffId='', [string]$Message='',
    [string]$ScopeRoot='', [string]$DataRoot='', [string]$RegistryRoot='',
    [ValidateSet('','AfterPrepare','AfterOldRename','AfterNewRename','AfterOldFileMove','AfterNewFileMove','AfterIntegration','AfterCommit','IntegrationFailure','UninstallFailure','CleanupFailure')][string]$TestFailure='',
    [int]$TestPauseSeconds=0,
    [switch]$SkipLaunchValidation
)
# One worker owns the complete transaction. NSIS owns UI/bootstrap and extracts a
# fresh payload; this script owns validation, recovery, files and integration.
# Exit: 0 committed/recovered, 1 failed, 2 another installer owns this target,
# 3 failed after application ACK, with the previous executable restored,
# 4 failed with an incomplete recovery: do not launch the uncertain target.
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$script:journal=$null
$script:lock=$null
$script:integrationLock=$null
$script:handoff=$null
$script:acknowledged=$false
$script:resultWritten=$false
$script:logPath=''
$script:journalPath=''
$script:target=''
$script:transactionRoot=''
$script:isFixture=$false
$script:contextValidated=$false
$script:rollbackAllowed=$false
$script:recoveryIncomplete=$false
$script:currentCommitted=$false
$script:shortcutPaths=@()
$utf8=[Text.UTF8Encoding]::new($false)

function FullPath([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path) -or ![IO.Path]::IsPathRooted($path)) { throw '需要完整的绝对目录路径。' }
    return [IO.Path]::GetFullPath($path).TrimEnd('\','/')
}
function IsInside([string]$path,[string]$root) {
    return $path.Equals($root,[StringComparison]::OrdinalIgnoreCase) -or $path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoReparse([string]$path,[bool]$recursive=$false) {
    $cursor=$path
    while ($cursor) {
        if ([IO.File]::Exists($cursor) -or [IO.Directory]::Exists($cursor)) {
            if (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "拒绝重解析目录或文件：$cursor" }
        }
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }
        $cursor=$parent
    }
    if ($recursive -and [IO.Directory]::Exists($path)) {
        # Check each level BEFORE traversing it, so a junction cannot redirect a
        # recursive scan outside this transaction's checked absolute paths.
        $pending=[Collections.Generic.Stack[string]]::new()
        $pending.Push($path)
        while ($pending.Count) {
            $directory=$pending.Pop()
            foreach ($item in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
                $attributes=[IO.File]::GetAttributes($item)
                if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "拒绝重解析目录或文件：$item" }
                if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) { $pending.Push($item) }
            }
        }
    }
}
function Atomic-Json([string]$path,$value) {
    $parent=[IO.Path]::GetDirectoryName($path)
    [void][IO.Directory]::CreateDirectory($parent)
    Assert-NoReparse $parent
    $temporary=Join-Path $parent ('.'+[IO.Path]::GetFileName($path)+'.'+[guid]::NewGuid().ToString('N')+'.tmp')
    $previous=$temporary+'.previous'
    try {
        $json=ConvertTo-Json -InputObject $value -Depth 12 -Compress
        $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $bytes=$utf8.GetBytes($json); $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
        # Windows PowerShell 5 converts a null string argument to an empty
        # filename. Give File.Replace a real sibling backup, then remove it.
        if ([IO.File]::Exists($path)) { [IO.File]::Replace($temporary,$path,$previous) }
        else { [IO.File]::Move($temporary,$path) }
    } finally {
        # A successful atomic publication must not be reported as a failed
        # publication merely because its expendable sibling cannot be removed.
        try { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } } catch { }
        try { if ([IO.File]::Exists($previous)) { [IO.File]::Delete($previous) } } catch { }
    }
}
function Read-Json([string]$path) { return ConvertFrom-Json ([IO.File]::ReadAllText($path)) }
function Log([string]$text) {
    if ($script:logPath) {
        try { [IO.File]::AppendAllText($script:logPath,([DateTime]::UtcNow.ToString('o')+' '+$text+"`r`n"),$utf8) }
        catch { [Console]::Error.WriteLine('维护日志暂时无法写入：'+$_.Exception.Message) }
    }
}
function Save-Journal([string]$state) {
    # In-memory state follows successful durable publication. In particular,
    # failed first commit persistence must still take the rollback path.
    $candidate=ConvertFrom-Json (ConvertTo-Json -InputObject $script:journal -Depth 12 -Compress)
    $candidate.state=$state
    $candidate.updatedUtc=[DateTime]::UtcNow.ToString('o')
    Atomic-Json $script:journalPath $candidate
    $script:journal=$candidate
    Log ('phase='+$state)
}
function Hash-File([string]$path) {
    $algorithm=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($path)
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
function Relative-File([string]$root,[string]$path) { return $path.Substring($root.Length+1).Replace('/','\') }
function Manifest-Path([string]$root,[string]$relative) {
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative.Length -gt 1024 -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or $relative.IndexOfAny([char[]]'*?"<>|') -ge 0) { throw '文件清单含不安全的路径。' }
    $relative=$relative.Replace('/','\')
    foreach ($part in $relative.Split('\')) {
        if (!$part -or $part -eq '.' -or $part -eq '..' -or $part -ne $part.Trim() -or $part.EndsWith('.') -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw "文件清单含不安全的路径：$relative" }
    }
    $resolved=FullPath (Join-Path $root $relative)
    if (!(IsInside $resolved $root) -or $resolved -eq $root) { throw '文件清单路径越过安装目录。' }
    return $resolved
}
function Read-Manifest([string]$root,[bool]$verify) {
    Assert-NoReparse $root $verify
    $manifestFile=Join-Path $root 'manifest.json'
    Assert-NoReparse $manifestFile
    if (![IO.File]::Exists($manifestFile)) { throw "安装目录缺少文件清单，不能安全维护：$root" }
    $entries=Read-Json $manifestFile
    if ($null -eq $entries -or @($entries).Count -eq 0 -or @($entries).Count -gt 100000) { throw '文件清单为空或超出限制。' }
    $owned=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        if (!$entry.PSObject.Properties['path'] -or !$entry.PSObject.Properties['sha256'] -or [string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw '文件清单格式无效。' }
        $path=Manifest-Path $root ([string]$entry.path)
        Assert-ManagedPath $root ([string]$entry.path)
        $relative=Relative-File $root $path
        if ($relative -ieq 'manifest.json' -or $relative -ieq 'Uninstall.exe' -or $owned.ContainsKey($relative)) { throw "文件清单重复或包含保留文件：$relative" }
        $owned.Add($relative,$entry)
        if ($verify) {
            if (![IO.File]::Exists($path) -or (Hash-File $path) -ine [string]$entry.sha256) { throw "安装文件校验失败：$relative" }
        }
    }
    if (!$owned.ContainsKey('EditHere.exe') -or !$owned.ContainsKey('edithere-cli.exe') -or !$owned.ContainsKey('version.txt')) { throw '文件清单缺少必要的程序文件。' }
    if ($verify) {
        foreach ($file in [IO.Directory]::EnumerateFiles($root,'*',[IO.SearchOption]::AllDirectories)) {
            $relative=Relative-File $root $file
            if (!$owned.ContainsKey($relative) -and $relative -ine 'manifest.json' -and $relative -ine 'Uninstall.exe') { throw "新载荷含清单之外的文件：$relative" }
        }
        if ([IO.File]::ReadAllText((Join-Path $root 'version.txt')).Trim() -ne $Version) { throw '安装载荷的版本与预期版本不一致。' }
    }
    # manifest.json and the NSIS-generated uninstaller are installer-owned,
    # but cannot be included in the package's self-referential hash manifest.
    $owned.Add('manifest.json',$null)
    if ([IO.File]::Exists((Join-Path $root 'Uninstall.exe'))) { Assert-ManagedPath $root 'Uninstall.exe'; $owned.Add('Uninstall.exe',$null) }
    return ,$owned
}
function Assert-ManagedPath([string]$root,[string]$relative) {
    $path=Manifest-Path $root $relative
    Assert-NoReparse $path
    if ([IO.Directory]::Exists($path)) { throw "程序文件路径被目录占用：$relative" }
    $parent=[IO.Path]::GetDirectoryName($path)
    while ($parent -and $parent -ine $root) {
        if ([IO.File]::Exists($parent)) { throw "程序目录路径被文件占用：$relative" }
        $parent=[IO.Path]::GetDirectoryName($parent)
    }
}
function Assert-Stage([string]$stage) {
    if (!$stage -or ![IO.Directory]::Exists($stage) -or [IO.Path]::GetDirectoryName($stage) -ine [IO.Path]::GetDirectoryName($script:target) -or $stage -ieq $script:target) { throw '暂存目录必须是安装目录旁的独立目录。' }
    Assert-NoReparse $stage $true
}
function Assert-Writable([string]$directory) {
    $probe=Join-Path $directory ('.edithere-write-'+[guid]::NewGuid().ToString('N'))
    try { $stream=[IO.File]::Open($probe,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None); $stream.Dispose() }
    finally { if ([IO.File]::Exists($probe)) { [IO.File]::Delete($probe) } }
}
function Preflight-Space([string]$stage,$plan) {
    $payloadBytes=[int64]0
    if ($stage -and [IO.Directory]::Exists($stage)) {
        foreach ($file in [IO.Directory]::EnumerateFiles($stage,'*',[IO.SearchOption]::AllDirectories)) { $payloadBytes+=([IO.FileInfo]$file).Length }
    }
    # Payload is already extracted. File moves stay on the same volume; user
    # files remain in place and do not add any copy-space requirement.
    $required=[Math]::Max([int64]16777216,[int64]($payloadBytes*0.05))+([int64]@($plan).Count*4096)
    $volume=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($script:target))
    if (!$volume.IsReady -or $volume.AvailableFreeSpace -lt $required) { throw "磁盘空间不足。还需要至少 $required 字节可用空间。" }
    Log ("space payload=$payloadBytes remainingRequired=$required available="+$volume.AvailableFreeSpace)
    Assert-Writable ([IO.Path]::GetDirectoryName($script:target))
    Assert-Writable $script:transactionRoot
    if ([IO.Directory]::Exists($script:target)) { Assert-Writable $script:target }
    if ($stage) { Assert-Writable $stage }
}
function Integrate([string]$operation,[string]$snapshot='') {
    $scriptFile=Join-Path $PSScriptRoot 'integrate.ps1'
    if (![IO.File]::Exists($scriptFile)) { throw '系统集成脚本缺失。' }
    $arguments=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$scriptFile,'-Mode',$operation,'-InstallDirectory',$script:target)
    if ($operation -eq 'Install') { $arguments+=@('-Startup',$Startup,'-AddToPath',$AddToPath,'-Version',$Version) }
    if ($operation -eq 'Close') { $arguments+=@('-TimeoutSeconds','60') }
    if ($snapshot) { $arguments+=@('-SnapshotPath',$snapshot) }
    if ($script:isFixture) { $arguments+=@('-ScopeRoot',$ScopeRoot,'-RegistryRoot',$RegistryRoot) }
    $output=& (Join-Path $PSHOME 'powershell.exe') @arguments 2>&1
    $code=$LASTEXITCODE
    if ($output) { Log (($output | Out-String).Trim()) }
    if ($code -ne 0) { throw "系统集成阶段 $operation 失败（$code）：$($output | Out-String)" }
}
function Shortcut-Snapshot {
    $snapshots=@()
    foreach ($path in $script:shortcutPaths) {
        Assert-NoReparse $path
        $exists=[IO.File]::Exists($path)
        if ($exists) {
            if ($script:isFixture) { $linked=[string](Read-Json $path).target }
            else { $shell=New-Object -ComObject WScript.Shell; $linked=$shell.CreateShortcut($path).TargetPath }
            if ((FullPath $linked) -ine (Join-Path $script:target 'EditHere.exe') -and (FullPath $linked) -ine (Join-Path $script:target 'Uninstall.exe')) { throw "同名快捷方式属于另一个程序，未覆盖：$path" }
        }
        $snapshots+= [pscustomobject]@{path=$path; existed=$exists; content=if($exists){[Convert]::ToBase64String([IO.File]::ReadAllBytes($path))}else{''}}
    }
    return $snapshots
}
function Shortcut-Restore($snapshots) {
    foreach ($item in $snapshots) {
        if (@($script:shortcutPaths | Where-Object { $_ -ieq [string]$item.path }).Count -ne 1) { throw '快捷方式快照路径无效。' }
        Assert-NoReparse ([string]$item.path)
        if ($item.existed) { [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($item.path)); [IO.File]::WriteAllBytes($item.path,[Convert]::FromBase64String($item.content)) }
        elseif ([IO.File]::Exists($item.path)) { [IO.File]::Delete($item.path) }
    }
}
function Set-Shortcut([string]$path,[string]$executable) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    Assert-NoReparse $path
    if ($script:isFixture) { Atomic-Json $path @{target=$executable}; return }
    $shell=New-Object -ComObject WScript.Shell
    $shortcut=$shell.CreateShortcut($path)
    $shortcut.TargetPath=$executable; $shortcut.WorkingDirectory=$script:target
    $shortcut.Save()
    if (![IO.File]::Exists($path)) { throw "无法创建快捷方式：$path" }
}
function Shortcut-Apply([bool]$uninstall) {
    if ($uninstall) { foreach ($path in $script:shortcutPaths) { if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) } }; return }
    Set-Shortcut $script:shortcutPaths[0] (Join-Path $script:target 'EditHere.exe')
    Set-Shortcut $script:shortcutPaths[1] (Join-Path $script:target 'Uninstall.exe')
    if ($Desktop -eq '1') { Set-Shortcut $script:shortcutPaths[2] (Join-Path $script:target 'EditHere.exe') }
    elseif ([IO.File]::Exists($script:shortcutPaths[2])) { [IO.File]::Delete($script:shortcutPaths[2]) }
}
function Read-Handoff {
    if (!$HandoffId) { return }
    if ($HandoffId -notmatch '^EditHere-update-[A-Za-z0-9]{6,64}$') { throw '更新交接标识无效。' }
    $directory=FullPath (Join-Path ([IO.Path]::GetTempPath()) $HandoffId)
    Assert-NoReparse $directory $true
    $request=Read-Json (Join-Path $directory 'request.json')
    if (!$request.PSObject.Properties['target'] -or !$request.PSObject.Properties['version'] -or !$request.PSObject.Properties['token'] -or [string]$request.token -notmatch '^[A-Za-z0-9{}-]{16,80}$' -or (FullPath ([string]$request.target)) -ine $script:target -or [string]$request.version -ne $Version) { throw '更新请求的目录、版本或令牌不匹配。' }
    $script:handoff=[pscustomobject]@{directory=$directory; request=$request}
}
function Handoff-Status([string]$status,[string]$message) {
    if ($null -eq $script:handoff) { return }
    Atomic-Json (Join-Path $script:handoff.directory 'status.json') @{status=$status; token=[string]$script:handoff.request.token; target=$script:target; version=$Version; message=$message; id=$HandoffId}
}
function Wait-Handoff {
    if ($null -eq $script:handoff) { return }
    $ackPath=Join-Path $script:handoff.directory 'ack.json'
    Handoff-Status 'ready' '安装预检完成，等待程序确认退出。'
    $deadline=[DateTime]::UtcNow.AddSeconds(120)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ([IO.File]::Exists($ackPath)) {
            Assert-NoReparse $ackPath
            $ack=Read-Json $ackPath
            if ([string]$ack.token -ne [string]$script:handoff.request.token) { throw '更新确认令牌不匹配。' }
            if ($ack.action -eq 'cancel') { Handoff-Status 'cancelled' '已取消更新，原安装未改变。'; throw [OperationCanceledException]::new('已取消更新，原安装未改变。') }
            if ($ack.action -ne 'ack') { throw '更新确认内容无效。' }
            $script:acknowledged=$true
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw '更新确认超时，原安装未改变。'
}
function Cleanup-Handoff {
    if ($null -eq $script:handoff) { return }
    # An independent, bounded helper releases the original download after Qt
    # has had time to read its final status. It deletes exact validated files,
    # never a directory tree, and leaves unexpected user files alone.
    $directory=$script:handoff.directory.Replace("'","''")
    $token=([string]$script:handoff.request.token).Replace("'","''")
    $code=@"
Start-Sleep -Seconds 3
`$root='$directory'
`$token='$token'
try {
 if (([IO.File]::GetAttributes(`$root) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { exit }
 `$request=ConvertFrom-Json ([IO.File]::ReadAllText((Join-Path `$root 'request.json')))
 if (`$request.token -ne `$token) { exit }
 `$package=[string]`$request.package
 if (`$package -match '^[A-Za-z0-9_. -]+\.exe`$' -and `$request.sha256 -match '^[a-fA-F0-9]{64}`$') {
  `$file=Join-Path `$root `$package
  if ([IO.File]::Exists(`$file) -and (([IO.File]::GetAttributes(`$file) -band [IO.FileAttributes]::ReparsePoint) -eq 0)) {
   `$algorithm=[Security.Cryptography.SHA256]::Create(); `$stream=[IO.File]::OpenRead(`$file)
   try { `$hash=([BitConverter]::ToString(`$algorithm.ComputeHash(`$stream))).Replace('-','') } finally { `$stream.Dispose(); `$algorithm.Dispose() }
   if (`$hash -ieq `$request.sha256) { [IO.File]::Delete(`$file) }
  }
 }
 foreach (`$name in @('request.json','ack.json','status.json')) {
  `$file=Join-Path `$root `$name
  if ([IO.File]::Exists(`$file) -and (([IO.File]::GetAttributes(`$file) -band [IO.FileAttributes]::ReparsePoint) -eq 0)) { [IO.File]::Delete(`$file) }
 }
 if ([IO.Directory]::Exists(`$root) -and @([IO.Directory]::EnumerateFileSystemEntries(`$root)).Count -eq 0) { [IO.Directory]::Delete(`$root) }
} catch { }
"@
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    try { Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList @('-NoLogo','-NoProfile','-NonInteractive','-EncodedCommand',$encoded) -WindowStyle Hidden | Out-Null }
    catch { Log ('download cleanup could not start: '+$_.Exception.Message) }
}
function Write-Result([string]$status,[string]$message,[bool]$restored=$false) {
    $id=if($null -ne $script:journal){[string]$script:journal.id}elseif($HandoffId){$HandoffId}else{[guid]::NewGuid().ToString('N')}
    $token=if($null -ne $script:handoff){[string]$script:handoff.request.token}else{''}
    $recovery=''
    if ($null -ne $script:journal) {
        if ([IO.Directory]::Exists([string]$script:journal.backup)) { $recovery=[string]$script:journal.backup }
        elseif ([IO.Directory]::Exists([string]$script:journal.displaced)) { $recovery=[string]$script:journal.displaced }
    }
    $pending=($null -ne $script:journal -and [bool]$script:journal.cleanupPending)
    $result=@{schema=1; id=$id; target=$script:target; version=$Version; status=$status; message=$message; token=$token; operation=$Mode; acknowledged=$script:acknowledged; restored=$restored; cleanupPending=$pending; log=$script:logPath; journal=$script:journalPath; recovery=$recovery; stage=$StageDirectory; updatedUtc=[DateTime]::UtcNow.ToString('o')}
    Atomic-Json (Join-Path $DataRoot 'Results\last.json') $result
    if ($script:transactionRoot) { Atomic-Json (Join-Path $script:transactionRoot 'result.json') $result }
    Handoff-Status $status $message
    $script:resultWritten=$true
    Log ('result='+$status+' '+$message)
}
function Test-Point([string]$phase) {
    if (!$script:isFixture) { return }
    if ($phase -eq 'AfterPrepare' -and $TestPauseSeconds -gt 0) { Start-Sleep -Seconds ([Math]::Min(30,$TestPauseSeconds)) }
    if ($TestFailure -eq $phase) {
        Log ('fixture crash='+$phase)
        Stop-Process -Id $PID -Force
    }
}
function File-Plan($oldOwned,$newOwned) {
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $oldOwned.Keys) { [void]$paths.Add($relative) }
    if ($null -ne $newOwned) { foreach ($relative in $newOwned.Keys) { [void]$paths.Add($relative) } }
    if ($InstallerPath) {
        $setup=FullPath $InstallerPath
        if (IsInside $setup $script:target) {
            $relative=Relative-File $script:target $setup
            if ($paths.Contains($relative)) { throw '正在运行的安装包与程序文件重名，请重新下载使用原文件名的安装包。' }
        }
    }
    # Publishing the ownership manifest last keeps partially applied files
    # from masquerading as a complete new installation.
    $ordered=@($paths | Where-Object { $_ -ine 'manifest.json' } | Sort-Object)+@('manifest.json')
    $plan=@()
    foreach ($relative in $ordered) {
        Assert-ManagedPath $script:target $relative
        $old=Manifest-Path $script:target $relative
        $oldExists=[IO.File]::Exists($old)
        $newExists=($null -ne $newOwned -and $newOwned.ContainsKey($relative))
        if ($oldExists -and !$oldOwned.ContainsKey($relative)) { throw "新程序文件与现有文件重名，未覆盖：$relative" }
        $oldHash=if($oldExists){Hash-File $old}else{''}
        $packagedHash=if($oldOwned.ContainsKey($relative) -and $null -ne $oldOwned[$relative]){[string]$oldOwned[$relative].sha256}else{$oldHash}
        $newHash=if($newExists){Hash-File (Manifest-Path $StageDirectory $relative)}else{''}
        $plan+= [pscustomobject]@{path=$relative; oldExists=$oldExists; actualOldHash=$oldHash; packagedOldHash=$packagedHash; newExists=$newExists; newHash=$newHash; rolledBack=$false}
    }
    return $plan
}
function Ensure-TargetDirectory([string]$directory) {
    $missing=@()
    $cursor=$directory
    while (![IO.Directory]::Exists($cursor)) {
        if ([IO.File]::Exists($cursor)) { throw "程序目录被文件占用：$cursor" }
        if (!(IsInside $cursor $script:target)) { throw '创建程序目录越界。' }
        $missing+= $cursor
        $cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    if ($missing.Count) {
        $known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($path in @($script:journal.createdDirectories)) { [void]$known.Add([string]$path) }
        foreach ($path in $missing) { [void]$known.Add($path) }
        $script:journal.createdDirectories=@($known)
        Save-Journal $script:journal.state
        [void][IO.Directory]::CreateDirectory($directory)
    }
    Assert-NoReparse $directory
}
function Ensure-PrivateParent([string]$path) {
    Assert-NoReparse $path
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
}
function Apply-Files {
    $count=@($script:journal.plan).Count
    for ($index=0; $index -lt $count; $index++) {
        $script:journal.cursor=$index
        $entry=$script:journal.plan[$index]
        $relative=[string]$entry.path
        Assert-ManagedPath $script:target $relative
        $old=Manifest-Path $script:target $relative
        $backup=Manifest-Path $script:journal.backup $relative
        if ($entry.oldExists) {
            if (![IO.File]::Exists($old) -or (Hash-File $old) -ne $entry.actualOldHash) { throw "程序文件在安装准备后发生变化：$relative" }
            Save-Journal 'OldFileMoveIntent'
            Ensure-PrivateParent $backup
            [IO.File]::Move($old,$backup)
            Test-Point 'AfterOldFileMove'
            Test-Point 'AfterOldRename'
        } elseif ([IO.File]::Exists($old) -or [IO.Directory]::Exists($old)) { throw "程序文件位置出现了新文件，未覆盖：$relative" }
        if ($entry.newExists) {
            $new=Manifest-Path $script:journal.stage $relative
            Assert-ManagedPath $script:journal.stage $relative
            if (![IO.File]::Exists($new) -or (Hash-File $new) -ne $entry.newHash) { throw "新程序文件在安装准备后发生变化：$relative" }
            Save-Journal 'NewFileMoveIntent'
            Ensure-TargetDirectory ([IO.Path]::GetDirectoryName($old))
            [IO.File]::Move($new,$old)
            Test-Point 'AfterNewFileMove'
            Test-Point 'AfterNewRename'
        }
        $script:journal.cursor=$index+1
        Save-Journal 'Applying'
    }
}
function Delete-KnownEmptyDirectories([string]$root,$paths) {
    $directories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in @($paths)) {
        $directory=[IO.Path]::GetDirectoryName((Manifest-Path $root ([string]$relative)))
        while ($directory -and (IsInside $directory $root)) {
            [void]$directories.Add($directory)
            if ($directory -ieq $root) { break }
            $directory=[IO.Path]::GetDirectoryName($directory)
        }
    }
    foreach ($directory in @($directories | Sort-Object Length -Descending)) {
        Assert-NoReparse $directory
        if ([IO.Directory]::Exists($directory) -and @([IO.Directory]::EnumerateFileSystemEntries($directory)).Count -eq 0) { [IO.Directory]::Delete($directory) }
    }
}
function Delete-CreatedTargetDirectories {
    foreach ($directory in @($script:journal.createdDirectories | Sort-Object Length -Descending)) {
        Assert-NoReparse ([string]$directory)
        if ([IO.Directory]::Exists($directory) -and @([IO.Directory]::EnumerateFileSystemEntries($directory)).Count -eq 0) { [IO.Directory]::Delete($directory) }
    }
}
function Rollback-Files {
    $j=$script:journal
    # Capture the apply cursor before writing rollback state, since a crash
    # between a move and journal publication is inferred from source absence.
    if (!$j.PSObject.Properties['applyCursor']) {
        $j | Add-Member -NotePropertyName applyCursor -NotePropertyValue ([int]$j.cursor)
        $j | Add-Member -NotePropertyName applyState -NotePropertyValue ([string]$j.state)
    }
    Save-Journal 'RollingBack'
    for ($index=@($script:journal.plan).Count-1; $index -ge 0; $index--) {
        $j=$script:journal; $entry=$j.plan[$index]
        if ($entry.rolledBack) { continue }
        $relative=[string]$entry.path
        $target=Manifest-Path $script:target $relative
        $backup=Manifest-Path $j.backup $relative
        $stage=Manifest-Path $j.stage $relative
        $displaced=Manifest-Path $j.displaced $relative
        $applied=($entry.newExists -and ![IO.File]::Exists($stage) -and ([int]$j.applyCursor -gt $index -or ([int]$j.applyCursor -eq $index -and $j.applyState -in @('NewFileMoveIntent','Applying','Integrating'))))
        if ([IO.File]::Exists($backup)) {
            if ((Hash-File $backup) -ne $entry.actualOldHash) { throw "原程序备份发生变化，未覆盖：$relative" }
            if ([IO.File]::Exists($target)) {
                if (!$applied) { throw "恢复位置出现了新文件，未覆盖：$relative" }
                if ([IO.File]::Exists($displaced)) { throw "恢复保留文件已存在：$relative" }
                Ensure-PrivateParent $displaced
                [IO.File]::Move($target,$displaced)
            }
            Ensure-TargetDirectory ([IO.Path]::GetDirectoryName($target))
            [IO.File]::Move($backup,$target)
        } elseif ($entry.oldExists) {
            if (![IO.File]::Exists($target) -or (Hash-File $target) -ne $entry.actualOldHash) { throw "原程序文件和备份均不可用：$relative" }
        } elseif ($applied -and [IO.File]::Exists($target)) {
            if ([IO.File]::Exists($displaced)) { throw "恢复保留文件已存在：$relative" }
            Ensure-PrivateParent $displaced
            [IO.File]::Move($target,$displaced)
        }
        $script:journal.plan[$index].rolledBack=$true
        Save-Journal 'RollingBack'
    }
    if (!$script:journal.portable) {
        if ([IO.File]::Exists($script:journal.integrationSnapshot)) { Integrate 'Restore' $script:journal.integrationSnapshot }
        Shortcut-Restore @($script:journal.shortcuts)
    }
    Delete-CreatedTargetDirectories
    Delete-KnownEmptyDirectories $script:journal.backup @($script:journal.plan | ForEach-Object { $_.path })
    Save-Journal 'RolledBack'
}
function Cleanup-FileBackup {
    $j=$script:journal
    Validate-Journal $j
    $pending=$false
    foreach ($entry in @($j.plan)) {
        $file=Manifest-Path $j.backup ([string]$entry.path)
        if (![IO.File]::Exists($file)) { continue }
        if (!$entry.oldExists -or (Hash-File $file) -ne $entry.packagedOldHash) { $pending=$true; Log ('backup preserved changed file: '+$entry.path); continue }
        try {
            if ($script:isFixture -and $TestFailure -eq 'CleanupFailure') { throw 'fixture cleanup blocked' }
            [IO.File]::Delete($file)
        } catch { $pending=$true; Log ('backup cleanup pending: '+$entry.path+' '+$_.Exception.Message) }
    }
    try { Delete-KnownEmptyDirectories $j.backup @($j.plan | ForEach-Object { $_.path }) } catch { $pending=$true; Log ('backup directory cleanup pending: '+$_.Exception.Message) }
    if ($j.operation -eq 'Install') {
        try { Delete-KnownEmptyDirectories $j.stage @($j.plan | Where-Object { $_.newExists } | ForEach-Object { $_.path }) } catch { Log ('stage empty-directory cleanup deferred: '+$_.Exception.Message) }
    }
    $script:journal.cleanupPending=$pending -or [IO.Directory]::Exists($j.backup)
    Save-Journal 'Committed'
}
function Validate-Journal($value) {
    if ($value.schema -notin @(1,2) -or [string]$value.target -ine $script:target -or [string]$value.id -notmatch '^[a-f0-9]{32}$' -or [string]$value.operation -notin @('Install','Uninstall') -or [string]$value.state -notin @('Prepared','OldMoveIntent','NewMoveIntent','OldFileMoveIntent','NewFileMoveIntent','Applying','Integrating','Committed','RollingBack','RollbackFailed','RolledBack')) { throw '恢复记录身份无效，未修改安装文件。' }
    $parent=[IO.Path]::GetDirectoryName($script:target)
    $locations=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($property in @('stage','backup','displaced')) {
        $path=FullPath ([string]$value.$property)
        if ([IO.Path]::GetDirectoryName($path) -ine $parent -or $path -ieq $script:target -or !$locations.Add($path) -or [IO.File]::Exists($path)) { throw '恢复记录含不安全的目录。' }
        if ($property -ne 'stage' -and [IO.Path]::GetFileName($path) -notmatch ('^\.EditHere-'+$script:targetHash.Substring(0,12)+'-'+$value.id+'\.(backup|recovery)$')) { throw '恢复备份目录身份无效。' }
        if ($value.schema -eq 2 -and $property -ne 'stage') {
            $suffix=if($property -eq 'backup'){'backup'}else{'recovery'}
            if ([IO.Path]::GetFileName($path) -ine ('.EditHere-'+$script:targetHash.Substring(0,12)+'-'+$value.id+'.'+$suffix)) { throw '逐文件恢复目录身份无效。' }
        }
        Assert-NoReparse $path ($value.schema -eq 1)
    }
    if ([string]$value.integrationSnapshot -ine (Join-Path $script:transactionRoot 'integration.json')) { throw '系统快照路径无效。' }
    if ([bool]$value.fixture -ne $script:isFixture -or [string]$value.registryRoot -ine $RegistryRoot) { throw '恢复记录的系统集成范围不匹配。' }
    if ($value.schema -eq 1 -and $value.PSObject.Properties['legacyFileRecovery']) {
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        if (@($value.legacyFileRecovery).Count -gt 100002) { throw '旧版逐文件恢复记录超出限制。' }
        foreach ($entry in @($value.legacyFileRecovery)) {
            if (!$seen.Add([string]$entry.path)) { throw '旧版逐文件恢复记录含重复路径。' }
            foreach ($root in @($script:target,[string]$value.backup,[string]$value.displaced)) { Assert-ManagedPath $root ([string]$entry.path) }
            foreach ($name in @('backupExists','targetExists','restored')) { if ($entry.$name -isnot [bool]) { throw '旧版逐文件恢复标记无效。' } }
            if (($entry.backupExists -and [string]$entry.backupHash -notmatch '^[a-f0-9]{64}$') -or ($entry.targetExists -and [string]$entry.targetHash -notmatch '^[a-f0-9]{64}$')) { throw '旧版逐文件恢复摘要无效。' }
        }
    }
    if ($value.schema -eq 2) {
        if ([string]$value.layout -ne 'files' -or !$value.PSObject.Properties['plan'] -or @($value.plan).Count -gt 100002 -or [int]$value.cursor -lt 0 -or [int]$value.cursor -gt @($value.plan).Count) { throw '逐文件恢复记录无效。' }
        if ($value.PSObject.Properties['applyCursor'] -and ([int]$value.applyCursor -lt 0 -or [int]$value.applyCursor -gt @($value.plan).Count -or [string]$value.applyState -notin @('Prepared','OldFileMoveIntent','NewFileMoveIntent','Applying','Integrating'))) { throw '逐文件恢复记录的操作位置无效。' }
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $allowedDirectories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in @($value.plan)) {
            $relative=[string]$entry.path
            if (!$seen.Add($relative)) { throw '逐文件恢复记录含重复路径。' }
            foreach ($root in @($script:target,[string]$value.stage,[string]$value.backup,[string]$value.displaced)) { Assert-ManagedPath $root $relative }
            $directory=[IO.Path]::GetDirectoryName((Manifest-Path $script:target $relative))
            while ($directory -and (IsInside $directory $script:target)) {
                [void]$allowedDirectories.Add($directory)
                if ($directory -ieq $script:target) { break }
                $directory=[IO.Path]::GetDirectoryName($directory)
            }
            foreach ($name in @('oldExists','newExists','rolledBack')) { if ($entry.$name -isnot [bool]) { throw '逐文件恢复记录标记无效。' } }
            foreach ($name in @('actualOldHash','packagedOldHash','newHash')) {
                $hash=[string]$entry.$name
                if ($hash -and $hash -notmatch '^[a-f0-9]{64}$') { throw '逐文件恢复记录摘要无效。' }
            }
            if (($entry.oldExists -and (!$entry.actualOldHash -or !$entry.packagedOldHash)) -or ($entry.newExists -and !$entry.newHash) -or (!$entry.oldExists -and $entry.actualOldHash) -or (!$entry.newExists -and $entry.newHash) -or ($value.operation -eq 'Uninstall' -and $entry.newExists)) { throw '逐文件恢复记录内容矛盾。' }
        }
        $created=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($directory in @($value.createdDirectories)) {
            $path=FullPath ([string]$directory)
            if (!$allowedDirectories.Contains($path) -or !$created.Add($path)) { throw '恢复记录的创建目录越界或重复。' }
            Assert-NoReparse $path
            if ([IO.File]::Exists($path)) { throw '恢复记录目录被文件占用。' }
        }
        if (@($value.plan).Count -gt 0 -and [string]$value.plan[-1].path -ine 'manifest.json') { throw '逐文件恢复记录未最后处理清单。' }
    }
}
function Rollback {
    $j=$script:journal
    Validate-Journal $j
    if ($j.schema -eq 2) { Rollback-Files; return }
    Log ('rollback from='+$j.state)
    # Backup existence, rather than the last persisted phase alone, handles a
    # process dying after Move but before the subsequent journal update.
    if ($j.PSObject.Properties['legacyFileRecovery']) {
        Restore-LegacyFiles
    } elseif ([IO.Directory]::Exists($j.backup)) {
        if ([IO.Directory]::Exists($script:target)) {
            if ([IO.Directory]::Exists($j.displaced)) { throw "恢复保留目录已存在，请先人工检查：$($j.displaced)" }
            try { [IO.Directory]::Move($script:target,$j.displaced) }
            catch {
                # A historical whole-directory transaction may now be retried
                # by a setup launched from its target. If the failed atomic
                # move did not change either directory, recover owned files
                # individually and leave the running setup/user data in place.
                if (![IO.Directory]::Exists($script:target) -or [IO.Directory]::Exists($j.displaced) -or ![IO.Directory]::Exists($j.backup)) { throw }
                Prepare-LegacyFileRecovery
                Restore-LegacyFiles
            }
        }
        if (!$script:journal.PSObject.Properties['legacyFileRecovery']) { [IO.Directory]::Move($j.backup,$script:target) }
    } elseif (!$j.hadTarget -and $j.state -notin @('Prepared','OldMoveIntent')) {
        if ([IO.Directory]::Exists($script:target)) {
            if ([IO.Directory]::Exists($j.displaced)) { throw '恢复保留目录已存在。' }
            try { [IO.Directory]::Move($script:target,$j.displaced) }
            catch {
                if (![IO.Directory]::Exists($script:target) -or [IO.Directory]::Exists($j.displaced)) { throw }
                Prepare-LegacyFileRecovery
                Restore-LegacyFiles
            }
        }
    } elseif ($j.hadTarget -and ![IO.Directory]::Exists($script:target)) { throw "原安装和备份均不可用，请检查恢复记录：$script:journalPath" }
    if (!$j.portable) {
        if ([IO.File]::Exists($j.integrationSnapshot)) { Integrate 'Restore' $j.integrationSnapshot }
        Shortcut-Restore @($j.shortcuts)
    }
    Save-Journal 'RolledBack'
}
function Prepare-LegacyFileRecovery {
    $j=$script:journal
    $oldOwned=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($j.oldManaged)) {
        if ([string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or !$oldOwned.Add([string]$entry.path)) { throw '旧版恢复所有权清单无效。' }
        Assert-ManagedPath $j.backup ([string]$entry.path)
    }
    $newOwned=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    # In schema1, stage disappears only after the complete new directory has
    # been placed at target. Otherwise a newly created target remains unknown.
    if (![IO.Directory]::Exists($j.stage) -and [IO.File]::Exists((Join-Path $script:target 'manifest.json'))) { $newOwned=Read-Manifest $script:target $false }
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $oldOwned) { [void]$paths.Add($relative) }
    foreach ($relative in $newOwned.Keys) { [void]$paths.Add($relative) }
    $ordered=@($paths | Where-Object { $_ -ine 'manifest.json' } | Sort-Object)
    if ($paths.Contains('manifest.json')) { $ordered+=@('manifest.json') }
    $plan=@()
    foreach ($relative in $ordered) {
        Assert-ManagedPath $script:target $relative
        $target=Manifest-Path $script:target $relative
        $backup=Manifest-Path $j.backup $relative
        $oldExists=($oldOwned.Contains($relative) -and [IO.File]::Exists($backup))
        $targetExists=[IO.File]::Exists($target)
        if ($targetExists -and !$newOwned.ContainsKey($relative)) { throw "旧版恢复位置出现了用户文件，未覆盖：$relative" }
        $plan+=[pscustomobject]@{path=$relative; backupExists=$oldExists; backupHash=if($oldExists){Hash-File $backup}else{''}; targetExists=$targetExists; targetHash=if($targetExists){Hash-File $target}else{''}; restored=$false}
    }
    $j | Add-Member -NotePropertyName legacyFileRecovery -NotePropertyValue @($plan)
    Save-Journal 'RollingBack'
    Log 'historical directory recovery switched to owned files; unknown backup files retained'
}
function Restore-LegacyFiles {
    Validate-Journal $script:journal
    for ($index=0; $index -lt @($script:journal.legacyFileRecovery).Count; $index++) {
        $j=$script:journal; $entry=$j.legacyFileRecovery[$index]
        if ($entry.restored) { continue }
        $relative=[string]$entry.path
        $target=Manifest-Path $script:target $relative
        $backup=Manifest-Path $j.backup $relative
        $displaced=Manifest-Path $j.displaced $relative
        # The persistent plan is also the intent record for these moves. On a
        # retry, backup absence + the original hash identifies a restored file.
        if ($entry.backupExists -and [IO.File]::Exists($backup)) {
            if ((Hash-File $backup) -ne $entry.backupHash) { throw "旧版恢复备份发生变化：$relative" }
            if ([IO.File]::Exists($target)) {
                if (!$entry.targetExists -or [IO.File]::Exists($displaced)) { throw "旧版恢复位置出现了新文件，未覆盖：$relative" }
                Ensure-PrivateParent $displaced
                [IO.File]::Move($target,$displaced)
            }
            Assert-NoReparse ([IO.Path]::GetDirectoryName($target))
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            [IO.File]::Move($backup,$target)
            Test-Point 'AfterOldFileMove'
        } elseif ($entry.backupExists) {
            if (![IO.File]::Exists($target) -or (Hash-File $target) -ne $entry.backupHash) { throw "旧版原程序和备份均不可用：$relative" }
        } elseif ($entry.targetExists -and [IO.File]::Exists($target)) {
            if ([IO.File]::Exists($displaced)) { throw "旧版恢复位置出现了新文件，未覆盖：$relative" }
            Ensure-PrivateParent $displaced
            [IO.File]::Move($target,$displaced)
        }
        $script:journal.legacyFileRecovery[$index].restored=$true
        Save-Journal 'RollingBack'
    }
    Delete-KnownEmptyDirectories $script:journal.backup @($script:journal.legacyFileRecovery | ForEach-Object { $_.path })
    $script:journal.cleanupPending=[IO.Directory]::Exists($script:journal.backup)
    Save-Journal 'RollingBack'
}
function Delete-EmptyDirectories([string]$root) {
    if (![IO.Directory]::Exists($root)) { return }
    Assert-NoReparse $root $true
    $directories=@([IO.Directory]::EnumerateDirectories($root,'*',[IO.SearchOption]::AllDirectories) | Sort-Object Length -Descending)
    foreach ($directory in $directories) { if (@([IO.Directory]::EnumerateFileSystemEntries($directory)).Count -eq 0) { [IO.Directory]::Delete($directory) } }
    if (@([IO.Directory]::EnumerateFileSystemEntries($root)).Count -eq 0) { [IO.Directory]::Delete($root) }
}
function Cleanup-Backup {
    $j=$script:journal
    if ($j.schema -eq 2) { Cleanup-FileBackup; return }
    if (![IO.Directory]::Exists($j.backup)) { $j.cleanupPending=$false; Save-Journal 'Committed'; return }
    Assert-NoReparse $j.backup $true
    $oldOwned=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($j.oldManaged)) {
        [void](Manifest-Path $j.backup ([string]$entry.path))
        if ([string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $oldOwned.ContainsKey([string]$entry.path)) { throw '旧程序文件回收清单无效。' }
        $oldOwned.Add([string]$entry.path,$entry)
    }
    $preserved=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($j.preserved)) { $preserved.Add([string]$entry.path,$entry) }
    $pending=$false
    foreach ($file in @([IO.Directory]::EnumerateFiles($j.backup,'*',[IO.SearchOption]::AllDirectories))) {
        $relative=Relative-File $j.backup $file
        $safe=$oldOwned.ContainsKey($relative) -and (Hash-File $file) -eq [string]$oldOwned[$relative].sha256
        if (!$safe -and $preserved.ContainsKey($relative)) {
            $entry=$preserved[$relative]
            $current=Manifest-Path $script:target $relative
            $safe=[IO.File]::Exists($current) -and (Hash-File $current) -eq $entry.sha256 -and (Hash-File $file) -eq $entry.sha256
        }
        if (!$safe) { $pending=$true; Log ('backup preserved unexpected/changed file: '+$relative); continue }
        try {
            if ($script:isFixture -and $TestFailure -eq 'CleanupFailure') { throw 'fixture cleanup blocked' }
            [IO.File]::Delete($file)
        } catch { $pending=$true; Log ('backup cleanup pending: '+$relative+' '+$_.Exception.Message) }
    }
    try { Delete-EmptyDirectories $j.backup } catch { $pending=$true; Log ('backup directory cleanup pending: '+$_.Exception.Message) }
    $j.cleanupPending=$pending -or [IO.Directory]::Exists($j.backup)
    Save-Journal 'Committed'
}
function Validate-Launch {
    if ($SkipLaunchValidation) { Log 'fixture launch validation explicitly skipped'; return }
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=Join-Path $script:target 'edithere-cli.exe'
    $info.Arguments='--version'
    $info.WorkingDirectory=$script:target
    $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $process=[Diagnostics.Process]::new(); $process.StartInfo=$info
    try {
        if (!$process.Start()) { throw '安装后的命令行程序无法启动。' }
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(15000)) { try { $process.Kill() } catch { }; throw '安装后的程序验证超时。' }
        $out=$stdout.GetAwaiter().GetResult().Trim()
        $err=$stderr.GetAwaiter().GetResult().Trim()
        if ($process.ExitCode -ne 0 -or $out -ne ('EditHere '+$Version)) { throw "安装后的程序验证失败（$($process.ExitCode)）：$out $err" }
        Log ('runtime verified: '+$out)
    } finally { $process.Dispose() }
}

try {
    $script:target=FullPath $InstallDirectory
    if ($script:target -eq [IO.Path]::GetPathRoot($script:target).TrimEnd('\') -or $script:target.Contains(';') -or $script:target.StartsWith('\\')) { throw '安装目录不能是磁盘根目录、网络目录或包含分号。' }
    Assert-NoReparse $script:target
    if ([IO.File]::Exists($script:target)) { throw '安装目标是文件，不能用作目录。' }
    if ($ScopeRoot -or $RegistryRoot -or $TestFailure -or $TestPauseSeconds -or $SkipLaunchValidation) {
        $ScopeRoot=FullPath $ScopeRoot
        if (![IO.File]::Exists((Join-Path $ScopeRoot '.edithere-maintenance-fixture')) -or !(IsInside $script:target $ScopeRoot) -or $script:target -ieq $ScopeRoot -or $RegistryRoot -notmatch '^Software\\EditHere\\InstallerTests\\[A-Za-z0-9_-]{6,80}$') { throw '测试维护范围无效，未执行系统集成。' }
        Assert-NoReparse $ScopeRoot
        $script:isFixture=$true
    }
    if (!$DataRoot) { $DataRoot=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'EditHere\Installer' }
    $DataRoot=FullPath $DataRoot
    if (IsInside $DataRoot $script:target) { throw '维护记录必须保存在安装目录之外。' }
    if ($script:isFixture -and !(IsInside $DataRoot $ScopeRoot)) { throw '测试维护记录越过隔离目录。' }
    Assert-NoReparse $DataRoot $true
    $script:contextValidated=$true
    if ($Mode -eq 'FailHandoff') {
        Read-Handoff
        if ($null -eq $script:handoff) { throw '缺少更新请求。' }
        if (!$Message) { $Message='安装器准备失败，原安装未改变。' }
        Write-Result 'failed' $Message
        Cleanup-Handoff
        exit 1
    }
    $parent=[IO.Path]::GetDirectoryName($script:target)
    if (![IO.Directory]::Exists($parent)) { throw '安装目录的父目录不存在。' }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $script:targetHash=([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($script:target.ToUpperInvariant())))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    $script:transactionRoot=Join-Path $DataRoot ('Transactions\'+$script:targetHash)
    [void][IO.Directory]::CreateDirectory($script:transactionRoot)
    Assert-NoReparse $script:transactionRoot
    $script:journalPath=Join-Path $script:transactionRoot 'journal.json'
    $script:logPath=Join-Path $script:transactionRoot 'maintenance.log'
    Read-Handoff
    try { $script:lock=[IO.File]::Open((Join-Path $script:transactionRoot 'lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch [IO.IOException] {
        Handoff-Status 'failed' '另一个安装或卸载正在处理这个目录，请等待它完成。'
        [Console]::Error.WriteLine('另一个安装或卸载正在处理这个目录，请等待它完成。')
        Cleanup-Handoff
        exit 2
    }
    Log ('begin operation='+$Mode+' target='+$script:target)
    # Different target directories still share this user's PATH, associations
    # and shortcut names. Serialize that scope as well as each file target.
    if ($Portable -eq '0') {
        try { $script:integrationLock=[IO.File]::Open((Join-Path $DataRoot 'integration.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
        catch [IO.IOException] {
            Handoff-Status 'failed' '另一个 EditHere 安装或卸载正在修改当前用户的系统设置。'
            [Console]::Error.WriteLine('另一个 EditHere 安装或卸载正在修改当前用户的系统设置。')
            Cleanup-Handoff
            exit 2
        }
        if ($Mode -ne 'Recover') {
            # A crashed transaction releases locks but still owns a system
            # snapshot. Do not let a different target commit newer registration
            # that an eventual recovery of that snapshot could erase.
            $transactions=Join-Path $DataRoot 'Transactions'
            foreach ($directory in [IO.Directory]::EnumerateDirectories($transactions)) {
                if ($directory -ieq $script:transactionRoot) { continue }
                $otherPath=Join-Path $directory 'journal.json'
                if (![IO.File]::Exists($otherPath)) { continue }
                Assert-NoReparse $otherPath
                $other=Read-Json $otherPath
                if ($other.schema -notin @(1,2) -or [bool]$other.fixture -ne $script:isFixture) { throw "其他维护记录身份无效，请先检查：$otherPath" }
                if (!$other.portable -and $other.state -notin @('Committed','RolledBack') -and [string]$other.registryRoot -ieq $RegistryRoot) {
                    throw "另一个安装目录有尚未恢复的维护事务，请先在该目录重试或恢复：$($other.target)（记录：$otherPath）"
                }
            }
        }
    }
    if ($script:isFixture) {
        $shellRoot=Join-Path $ScopeRoot 'Shell'
        $script:shortcutPaths=@((Join-Path $shellRoot 'Programs\EditHere\EditHere.lnk'),(Join-Path $shellRoot 'Programs\EditHere\卸载 EditHere.lnk'),(Join-Path $shellRoot 'Desktop\EditHere.lnk'))
    } else {
        $programs=[Environment]::GetFolderPath('Programs')
        $desktopPath=[Environment]::GetFolderPath('DesktopDirectory')
        $script:shortcutPaths=@((Join-Path $programs 'EditHere\EditHere.lnk'),(Join-Path $programs 'EditHere\卸载 EditHere.lnk'),(Join-Path $desktopPath 'EditHere.lnk'))
    }
    if ([IO.File]::Exists($script:journalPath)) {
        $script:journal=Read-Json $script:journalPath
        Validate-Journal $script:journal
        if ($script:journal.state -eq 'Committed') {
            try { Cleanup-Backup }
            catch {
                $script:journal.cleanupPending=$true
                Save-Journal 'Committed'
                Log ('preceding backup cleanup deferred: '+$_.Exception.Message)
            }
        }
        elseif ($script:journal.state -ne 'RolledBack') {
            $script:recoveryIncomplete=$true
            if ($HandoffId) { throw "上次维护需要独立恢复；本次更新尚未确认，未改变安装目录。请重新运行安装包处理该目录：$script:target" }
            Integrate 'Close'
            Rollback
            $script:recoveryIncomplete=$false
        }
        if ($Mode -eq 'Recover') {
            Write-Result 'recovered' '维护恢复完成。' ([IO.File]::Exists((Join-Path $script:target 'EditHere.exe')))
            exit 0
        }
        if ($script:journal.cleanupPending) { Log ('preceding backup retained without blocking maintenance: '+$script:journal.backup) }
        # Preserve the preceding receipt/journal instead of silently replacing
        # the only record needed to identify a retained recovery directory.
        Atomic-Json (Join-Path $script:transactionRoot ('history-'+$script:journal.id+'.json')) $script:journal
        $script:journal=$null
    } elseif ($Mode -eq 'Recover') { Write-Result 'recovered' '没有需要恢复的维护事务。'; exit 0 }
    $hadTarget=[IO.Directory]::Exists($script:target)
    $oldOwned=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    if ($hadTarget -and [IO.File]::Exists((Join-Path $script:target 'manifest.json'))) { $oldOwned=Read-Manifest $script:target $false }
    elseif ($hadTarget -and [IO.File]::Exists((Join-Path $script:target 'EditHere.exe'))) { throw '现有程序缺少有效文件清单，不能安全更新或卸载。' }
    if ($Mode -eq 'Install' -and $Portable -eq '1' -and (!$hadTarget -or !$oldOwned.Count -or ![IO.File]::Exists((Join-Path $script:target 'EditHere.exe')))) { throw '便携更新必须使用现有程序及有效文件清单所在的目录。' }
    if ($Mode -eq 'Uninstall' -and (!$hadTarget -or !$oldOwned.Count)) { throw '未找到可安全卸载的程序清单。' }
    $newOwned=$null
    $id=[guid]::NewGuid().ToString('N')
    if ($Mode -eq 'Install') {
        if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw '安装版本无效。' }
        $StageDirectory=FullPath $StageDirectory
        if ($script:isFixture -and !(IsInside $StageDirectory $ScopeRoot)) { throw '测试载荷越过隔离目录。' }
        Assert-Stage $StageDirectory
        $newOwned=Read-Manifest $StageDirectory $true
        if ($Portable -eq '0' -and ![IO.File]::Exists((Join-Path $StageDirectory 'Uninstall.exe'))) { throw '普通安装缺少卸载程序。' }
    } else {
        $StageDirectory=Join-Path $parent ('.EditHere-'+$script:targetHash.Substring(0,12)+'-'+$id+'.remaining')
    }
    $plan=@(File-Plan $oldOwned $newOwned)
    Preflight-Space $(if($Mode -eq 'Install'){$StageDirectory}else{''}) $plan
    $shortcutSnapshot=@()
    $snapshotPath=Join-Path $script:transactionRoot 'integration.json'
    if ($Portable -eq '0') {
        $shortcutSnapshot=@(Shortcut-Snapshot)
        Integrate 'Snapshot' $snapshotPath
    }
    Wait-Handoff
    # Close is part of the checked worker, after readiness/ACK. It asks the
    # application to save/discard normally and never force-kills user work.
    Integrate 'Close'
    Assert-NoReparse $script:target
    $script:journal=[pscustomobject]@{schema=2; layout='files'; id=$id; operation=$Mode; target=$script:target; stage=$StageDirectory; backup=(Join-Path $parent ('.EditHere-'+$script:targetHash.Substring(0,12)+'-'+$id+'.backup')); displaced=(Join-Path $parent ('.EditHere-'+$script:targetHash.Substring(0,12)+'-'+$id+'.recovery')); state='Prepared'; cursor=0; plan=$plan; createdDirectories=@(); hadTarget=$hadTarget; portable=($Portable -eq '1'); version=$Version; fixture=$script:isFixture; registryRoot=$RegistryRoot; integrationSnapshot=$snapshotPath; shortcuts=$shortcutSnapshot; cleanupPending=$false; createdUtc=[DateTime]::UtcNow.ToString('o'); updatedUtc=[DateTime]::UtcNow.ToString('o')}
    $script:rollbackAllowed=$true
    Save-Journal 'Prepared'
    Test-Point 'AfterPrepare'
    Apply-Files
    Save-Journal 'Integrating'
    if ($Portable -eq '0') {
        if ($script:isFixture -and $TestFailure -eq 'IntegrationFailure') { throw 'fixture integration failure' }
        if ($script:isFixture -and $TestFailure -eq 'UninstallFailure') { throw 'fixture uninstall integration failure' }
        Integrate $Mode
        Shortcut-Apply ($Mode -eq 'Uninstall')
    }
    Test-Point 'AfterIntegration'
    # Verify only the managed payload at its final destination. Unknown files
    # have remained in place, without reading, copying or moving their contents.
    if ($Mode -eq 'Install') {
        foreach ($entry in @($script:journal.plan)) {
            if ($entry.newExists) {
                $file=Manifest-Path $script:target ([string]$entry.path)
                if (![IO.File]::Exists($file) -or (Hash-File $file) -ine [string]$entry.newHash) { throw '最终安装文件校验失败。' }
            }
        }
    }
    if ($Mode -eq 'Install') { Validate-Launch }
    Save-Journal 'Committed'
    $script:currentCommitted=$true
    Write-Result 'committed' $(if($Mode -eq 'Uninstall'){'卸载已提交，正在清理旧程序备份。'}else{'安装已提交，正在清理旧程序备份。'})
    Test-Point 'AfterCommit'
    Cleanup-Backup
    if ($Mode -eq 'Uninstall') { Delete-CreatedTargetDirectories }
    $message=if($Mode -eq 'Uninstall'){'卸载完成，用户文件已保留。'}else{'安装完成。'}
    if ($script:journal.cleanupPending) { $message+=' 旧程序备份尚有文件待清理，可解除占用后重试恢复清理。' }
    Write-Result 'committed' $message
    Cleanup-Handoff
    exit 0
} catch {
    $reason=$_.Exception.Message
    $cancelled=$_.Exception -is [OperationCanceledException]
    $restored=$false
    if ($script:currentCommitted -and $null -ne $script:journal -and $script:journal.state -eq 'Committed') {
        # Cleanup and receipt publication happen after the durable commit.
        # They cannot turn a working committed installation into a failed
        # installation or trigger rollback of an already-cleaned backup.
        $script:journal.cleanupPending=$true
        try {
            Save-Journal 'Committed'
            Write-Result 'committed' ('维护已完成，收尾待重试：'+$reason)
        } catch { [Console]::Error.WriteLine('维护已经提交，但记录收尾失败：'+$_.Exception.Message) }
        Cleanup-Handoff
        exit 0
    }
    try {
        if ($script:rollbackAllowed -and $null -ne $script:journal -and $script:journal.state -notin @('Committed','RolledBack')) {
            try { Rollback; $restored=[IO.File]::Exists((Join-Path $script:target 'EditHere.exe')) }
            catch { $script:recoveryIncomplete=$true; $reason+=' 恢复未完成：'+$_.Exception.Message; Save-Journal 'RollbackFailed' }
        } elseif (!$script:recoveryIncomplete -and $script:target) { $restored=[IO.File]::Exists((Join-Path $script:target 'EditHere.exe')) }
        if ($script:contextValidated) { Write-Result $(if($cancelled){'cancelled'}else{'failed'}) $reason $restored }
    } catch { [Console]::Error.WriteLine('无法写入安装结果：'+$_.Exception.Message) }
    [Console]::Error.WriteLine($reason)
    Cleanup-Handoff
    if ($script:recoveryIncomplete -and !$HandoffId) { exit 4 }
    if ($script:acknowledged -and $restored) { exit 3 }
    exit 1
} finally {
    if ($null -ne $script:integrationLock) { $script:integrationLock.Dispose() }
    if ($null -ne $script:lock) { $script:lock.Dispose() }
}
