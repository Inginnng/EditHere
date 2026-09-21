param(
    [ValidateSet('Check','Install','Uninstall')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [ValidateSet('0','1')][string]$Startup='0',
    [ValidateSet('0','1')][string]$AddToPath='1'
)
$ErrorActionPreference='Stop'
try {
    $root=[IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
    if ($root -eq [IO.Path]::GetPathRoot($root).TrimEnd('\') -or $root.Contains(';')) { throw '安装路径无效。' }
    if ($Mode -eq 'Check') {
        $running=@(Get-Process -Name EditHere -ErrorAction SilentlyContinue)
        if ($running.Count) { throw '请先从系统托盘退出 EditHere，再继续安装或卸载。未保存的批注应先保存。' }
        exit 0
    }
    $user=[Microsoft.Win32.Registry]::CurrentUser
    $appKey='Software\EditHere\Installer'
    $runKey='Software\Microsoft\Windows\CurrentVersion\Run'
    $uninstallKey='Software\Microsoft\Windows\CurrentVersion\Uninstall\EditHere'
    $classes='Software\Classes'
    $exe=Join-Path $root 'EditHere.exe'
    $command='"'+$exe+'" --autostart'
    if($Mode -eq 'Install' -and $Startup -eq '1' -and $command.Length -gt 260){throw '登录启动命令超过 Windows 长度限制，请使用更短的安装目录。'}
    $pathKey=$user.CreateSubKey('Environment')
    $kind=[Microsoft.Win32.RegistryValueKind]::ExpandString
    $rawPath=[string]$pathKey.GetValue('Path','',[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    if ($null -ne $pathKey.GetValue('Path',$null)) { $kind=$pathKey.GetValueKind('Path') }
    $parts=@($rawPath -split ';' | Where-Object {$_ -ne ''})
    function SamePath([string]$a,[string]$b) { return $a.Trim().Trim('"').TrimEnd('\') -ieq $b.TrimEnd('\') }
    $meta=$user.OpenSubKey($appKey)
    $previousRoot=if($meta){[string]$meta.GetValue('InstallDir','')}else{''}
    $addedBefore=if($meta){[int]$meta.GetValue('AddedToPath',0)}else{0}
    if($meta){$meta.Dispose()}
    if($Mode -eq 'Install') {
        if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $root 'edithere-cli.exe') -PathType Leaf)) { throw '安装文件不完整。' }
        if($previousRoot -and !(SamePath $previousRoot $root)){throw '请先卸载旧安装目录，或使用原目录升级。'}
        $added=$addedBefore
        if($AddToPath -eq '1' -and !(@($parts | Where-Object {SamePath $_ $root}).Count)) {
            $rawPath=if($rawPath -eq ''){$root}else{$rawPath.TrimEnd(';')+';'+$root}
            $pathKey.SetValue('Path',$rawPath,$kind); $added=1
        } elseif($AddToPath -eq '0' -and $addedBefore) {
            $pathKey.SetValue('Path',(($rawPath -split ';' | Where-Object {!(SamePath $_ $root)}) -join ';'),$kind); $added=0
        }
        $run=$user.CreateSubKey($runKey)
        if($Startup -eq '1') { $run.SetValue('EditHere',$command,[Microsoft.Win32.RegistryValueKind]::String) }
        else { $run.DeleteValue('EditHere',$false) }
        $run.Dispose()
        # Reuse the legacy handler; do not replace a different application's default association.
        $handler=$user.CreateSubKey($classes+'\EditHere.Project')
        $handler.SetValue('','EditHere 项目'); $handler.Dispose()
        $icon=$user.CreateSubKey($classes+'\EditHere.Project\DefaultIcon'); $icon.SetValue('','"'+$exe+'",0'); $icon.Dispose()
        $open=$user.CreateSubKey($classes+'\EditHere.Project\shell\open\command'); $open.SetValue('','"'+$exe+'" "%1"'); $open.Dispose()
        $merged=[Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('.edithere')
        $oldDefault=if($merged){[string]$merged.GetValue('','')}else{''}
        if($merged){$merged.Dispose()}
        $ext=$user.CreateSubKey($classes+'\.edithere')
        if(!$oldDefault){$ext.SetValue('','EditHere.Project')}; $ext.Dispose()
        $with=$user.CreateSubKey($classes+'\.edithere\OpenWithProgids'); $with.SetValue('EditHere.Project',''); $with.Dispose()
        $meta=$user.CreateSubKey($appKey)
        $meta.SetValue('InstallDir',$root); $meta.SetValue('AddedToPath',$added,[Microsoft.Win32.RegistryValueKind]::DWord); $meta.Dispose()
    } else {
        if(!(SamePath $previousRoot $root)){throw '卸载目录与登记目录不一致，未修改系统设置。'}
        if($addedBefore){$pathKey.SetValue('Path',(($rawPath -split ';' | Where-Object {!(SamePath $_ $root)}) -join ';'),$kind)}
        $run=$user.OpenSubKey($runKey,$true)
        if($run){if([string]$run.GetValue('EditHere','') -eq $command){$run.DeleteValue('EditHere',$false)}; $run.Dispose()}
        $open=$user.OpenSubKey($classes+'\EditHere.Project\shell\open\command')
        $ownHandler=$open -and [string]$open.GetValue('','') -eq ('"'+$exe+'" "%1"')
        if($open){$open.Dispose()}
        if($ownHandler){
            $user.DeleteSubKeyTree($classes+'\EditHere.Project',$false)
            $ext=$user.OpenSubKey($classes+'\.edithere',$true)
            if($ext){if([string]$ext.GetValue('','') -eq 'EditHere.Project'){$ext.DeleteValue('',$false)}; $ext.Dispose()}
            $with=$user.OpenSubKey($classes+'\.edithere\OpenWithProgids',$true)
            if($with){$with.DeleteValue('EditHere.Project',$false);$with.Dispose()}
        }
        $user.DeleteSubKeyTree($appKey,$false)
        $user.DeleteSubKeyTree($uninstallKey,$false)
    }
    $pathKey.Dispose()
    Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class EditHereShell { [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, string l, uint f, uint t, out UIntPtr r); [DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, uint f, IntPtr a, IntPtr b); }'
    $result=[UIntPtr]::Zero
    [void][EditHereShell]::SendMessageTimeout([IntPtr]0xffff,0x1a,[UIntPtr]::Zero,'Environment',2,5000,[ref]$result)
    [EditHereShell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
