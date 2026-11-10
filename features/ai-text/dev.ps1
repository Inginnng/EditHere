param(
    [string]$QtRoot = "",
    [string]$BuildDirectory = "",
    [switch]$Standalone,
    [switch]$NoLaunch,
    [switch]$Live,
    [switch]$Dark,
    [switch]$Settings
)
$ErrorActionPreference = "Stop"
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
if (-not (Test-Path -LiteralPath (Join-Path $repoPath 'CMakeLists.txt'))) { $Standalone = $true }
$cachePath = Join-Path $repoPath "build/CMakeCache.txt"
$cacheText = if (Test-Path -LiteralPath $cachePath) { Get-Content -Raw -LiteralPath $cachePath } else { "" }
if (-not $QtRoot) {
    $qtMatch = [regex]::Match($cacheText, '(?m)^Qt6_DIR:PATH=(.+)$')
    if ($qtMatch.Success) { $QtRoot = [IO.Path]::GetFullPath((Join-Path $qtMatch.Groups[1].Value.Trim() "../../..")) }
    elseif ($env:QT_ROOT) { $QtRoot = $env:QT_ROOT }
    else { throw "Specify -QtRoot with a Qt 6.8+ installation containing Widgets, Svg and Network." }
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
if (-not $BuildDirectory) { $BuildDirectory = if ($Standalone) { "build-ai-text" } else { "build" } }
$buildPath = [IO.Path]::GetFullPath((Join-Path $repoPath $BuildDirectory))
$sourcePath = if ($Standalone) { $PSScriptRoot } else { $repoPath }
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$compiler = [regex]::Match($cacheText, '(?m)^CMAKE_CXX_COMPILER:[^=]+=(.+)$').Groups[1].Value.Trim()
$ninja = [regex]::Match($cacheText, '(?m)^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$').Groups[1].Value.Trim()
if (-not $compiler -and $QtRoot -match 'msvc') {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vsWhere) {
        $vsInstall = & $vsWhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInstall) {
            $toolset = (Get-Content -LiteralPath (Join-Path $vsInstall 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
            $compiler = Join-Path $vsInstall "VC/Tools/MSVC/$toolset/bin/Hostx64/x64/cl.exe"
            if (-not $ninja) { $ninja = Join-Path $vsInstall 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' }
        }
    }
}
$arguments = @('-S', $sourcePath, '-B', $buildPath, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtRoot")
if ($compiler) { $arguments += "-DCMAKE_CXX_COMPILER=$compiler" }
if ($ninja) { $arguments += "-DCMAKE_MAKE_PROGRAM=$ninja" }
$previousEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $previousEnvironment[$_.Name] = $_.Value }
try {
    if ($compiler -match '[\\/]VC[\\/]Tools[\\/]MSVC[\\/]') {
        $vsPath = ($compiler -split '[\\/]VC[\\/]Tools[\\/]MSVC[\\/]')[0]
        $vcVars = Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat'
        $vcEnvironment = & $env:COMSPEC /d /s /c ('call "' + $vcVars + '" >nul && set')
        if ($LASTEXITCODE) { throw "MSVC environment initialization failed." }
        foreach ($line in $vcEnvironment) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process') }
        }
    }
    $env:VSLANG = "1033"
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
    & $cmake @arguments
    if ($LASTEXITCODE) { throw "CMake configuration failed." }
    & $cmake --build $buildPath --target ai_text_workbench --parallel
    if ($LASTEXITCODE) { throw "AI text workbench build failed." }
    $executable = Join-Path $buildPath 'ai_text_workbench.exe'
    $deployment = & (Join-Path $QtRoot 'bin/windeployqt.exe') --no-translations $executable 2>&1
    if ($LASTEXITCODE) { throw "Qt deployment failed: $deployment" }
    $offscreenPlugin = Join-Path $QtRoot 'plugins/platforms/qoffscreen.dll'
    if (Test-Path -LiteralPath $offscreenPlugin) {
        Copy-Item -LiteralPath $offscreenPlugin -Destination (Join-Path $buildPath 'platforms/qoffscreen.dll')
    }
    Write-Host "Ready: $executable"
    if (-not $NoLaunch) {
        $launchArguments = @()
        if ($Live) { $launchArguments += '--live' }
        if ($Dark) { $launchArguments += '--dark' }
        if ($Settings) { $launchArguments += '--settings' }
        & $executable @launchArguments | Out-Null
    }
} finally {
    Get-ChildItem Env: | Where-Object { -not $previousEnvironment.ContainsKey($_.Name) } | ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, 'Process') }
    foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
}
