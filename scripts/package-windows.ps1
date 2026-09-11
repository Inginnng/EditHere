param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$CompilerBin = "",
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist/Help2Design-Native-0.4.0-win-x64"
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot ".tools/qt/6.8.3/mingw_64" }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (-not $outputPath.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Output must be inside the project." }
if (Test-Path -LiteralPath $outputPath) { throw "Output already exists. Choose a fresh OutputDirectory." }
if (-not (Test-Path -LiteralPath (Join-Path $buildPath "Help2Design.exe"))) { throw "Build the application first." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "packaging/licenses/qtbase/LGPL-3.0-only.txt"))) { throw "Third-party license materials are missing." }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
Copy-Item -LiteralPath (Join-Path $buildPath "Help2Design.exe") -Destination $outputPath
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
    if ($CompilerBin) { $env:PATH = $CompilerBin + ";" + $env:PATH }
    & (Join-Path $QtRoot "bin/windeployqt.exe") --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation,tls --include-plugins qwebp (Join-Path $outputPath "Help2Design.exe")
    if ($LASTEXITCODE) { throw "Qt deployment failed." }
} finally { $env:PATH = $previousPath }
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/licenses") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "schema") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/使用说明.txt") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/THIRD-PARTY-NOTICES.md") -Destination $outputPath
[IO.File]::WriteAllText((Join-Path $outputPath "qt.conf"), "[Paths]`nPrefix=.`nPlugins=.`n", [Text.UTF8Encoding]::new($false))
$manifest = Get-ChildItem -LiteralPath $outputPath -Recurse -File | ForEach-Object {
    [PSCustomObject]@{ path = [IO.Path]::GetRelativePath($outputPath, $_.FullName); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $outputPath "manifest.json") -Encoding utf8
Compress-Archive -LiteralPath $outputPath -DestinationPath ($outputPath + ".zip") -CompressionLevel Optimal
Write-Host "Portable folder: $outputPath"
Write-Host "ZIP: $outputPath.zip"
