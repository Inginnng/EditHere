param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$Compiler = "",
    [string]$Ninja = "",
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
$previousPath = $env:PATH
try {
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
} finally { $env:PATH = $previousPath }
Write-Host "Built: $buildPath/Help2Design.exe"
