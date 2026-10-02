param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$Compiler = "",
    [string]$Ninja = "",
    [string]$VcVars = "",
    [string]$BuildDirectory = "build",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot ".tools/qt/6.8.3/mingw_64" }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$arguments = @("-S", $projectRoot, "-B", $buildPath, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$QtRoot")
if ($Compiler) { $arguments += "-DCMAKE_CXX_COMPILER=$Compiler" }
if ($Ninja) { $arguments += "-DCMAKE_MAKE_PROGRAM=$Ninja" }
$previousEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $previousEnvironment[$_.Name] = $_.Value }
try {
    # MSVC needs its SDK include/lib paths as well as cl.exe. Load the matching
    # vcvars script in a child cmd process, then import its environment only for
    # this build; the caller's complete environment is restored below.
    if ($VcVars) {
        $VcVars = (Resolve-Path -LiteralPath $VcVars).Path
        $vcCommand = 'call "' + $VcVars + '" >nul && set'
        $vcEnvironment = & $env:COMSPEC /d /s /c $vcCommand
        if ($LASTEXITCODE) { throw "MSVC environment initialization failed." }
        foreach ($line in $vcEnvironment) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) {
                [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), "Process")
            }
        }
    }
    $env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
    if ($Compiler) { $env:PATH = (Split-Path $Compiler -Parent) + ";" + $env:PATH }
    & $cmake @arguments
    if ($LASTEXITCODE) { throw "CMake configuration failed." }
    & $cmake --build $buildPath --parallel
    if ($LASTEXITCODE) { throw "Build failed." }
    if (-not $SkipTests) {
        $env:H2D_TEST_ARTIFACTS = Join-Path $projectRoot "artifacts/native-ui"
        & (Join-Path (Split-Path $cmake -Parent) "ctest.exe") --test-dir $buildPath --output-on-failure
        if ($LASTEXITCODE) { throw "Tests failed." }
    }
} finally {
    Get-ChildItem Env: | Where-Object { -not $previousEnvironment.ContainsKey($_.Name) } | ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, "Process") }
    foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], "Process") }
}
Write-Host "Built: $buildPath/EditHere.exe"
