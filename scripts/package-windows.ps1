param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$CompilerBin = "",
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot ".tools/qt/6.8.3/mingw_64" }
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
if (-not (Test-Path -LiteralPath (Join-Path $buildPath "EditHere.exe"))) { throw "Build the application first." }
$versionPath = Join-Path $buildPath "version.txt"
if (-not (Test-Path -LiteralPath $versionPath)) { throw "Build version is missing. Rebuild the application first." }
$buildVersion = [IO.File]::ReadAllText($versionPath).Trim()
if ($buildVersion -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid build version in $versionPath." }
if (-not $OutputDirectory) { $OutputDirectory = "dist/EditHere-$buildVersion-win-x64" }
$outputPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (-not $outputPath.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Output must be inside the project." }
if ((Test-Path -LiteralPath $outputPath) -or (Test-Path -LiteralPath ($outputPath + ".zip"))) { throw "Output folder or ZIP already exists. Choose a fresh OutputDirectory." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "packaging/licenses/qtbase/LGPL-3.0-only.txt"))) { throw "Third-party license materials are missing." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "schema/feedback-v0.7.schema.json"))) { throw "The current feedback schema is missing." }
foreach ($notice in @("LICENSE", "LICENSING.md", "COMMERCIAL-LICENSE.md", "NOTICE")) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $notice))) { throw "Application license material is missing: $notice" }
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
Copy-Item -LiteralPath (Join-Path $buildPath "EditHere.exe") -Destination $outputPath
Copy-Item -LiteralPath $versionPath -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $buildPath "edithere-cli.exe") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/windows/integrate.ps1") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "skills") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "docs/AGENT-CLI.md") -Destination $outputPath
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
    if ($CompilerBin) { $env:PATH = $CompilerBin + ";" + $env:PATH }
    & (Join-Path $QtRoot "bin/windeployqt.exe") --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation --include-plugins qwebp (Join-Path $outputPath "EditHere.exe")
    if ($LASTEXITCODE) { throw "Qt deployment failed." }
} finally { $env:PATH = $previousPath }
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/licenses") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "schema") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/使用说明.txt") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/THIRD-PARTY-NOTICES.md") -Destination $outputPath
foreach ($notice in @("LICENSE", "LICENSING.md", "COMMERCIAL-LICENSE.md", "NOTICE")) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $notice) -Destination $outputPath
}
$packagedGuide = Join-Path $outputPath "AGENT-CLI.md"
$guideText = [IO.File]::ReadAllText($packagedGuide).Replace("../skills/", "skills/").Replace("../schema/", "schema/").Replace("../README.md", "https://github.com/Inginnng/EditHere").Replace("USER-GUIDE.md", "https://github.com/Inginnng/EditHere/blob/codex/native/docs/USER-GUIDE.md")
[IO.File]::WriteAllText($packagedGuide, $guideText, [Text.UTF8Encoding]::new($false))
$packagedLicensing = Join-Path $outputPath "LICENSING.md"
[IO.File]::WriteAllText($packagedLicensing, [IO.File]::ReadAllText($packagedLicensing).Replace("packaging/THIRD-PARTY-NOTICES.md", "THIRD-PARTY-NOTICES.md"), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $outputPath "qt.conf"), "[Paths]`nPrefix=.`nPlugins=.`n", [Text.UTF8Encoding]::new($false))
$manifest = Get-ChildItem -LiteralPath $outputPath -Recurse -File | ForEach-Object {
    [PSCustomObject]@{ path = [IO.Path]::GetRelativePath($outputPath, $_.FullName); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $outputPath "manifest.json") -Encoding utf8
# Zip the CONTENTS of the package folder (no top-level directory): the in-app
# updater extracts this archive straight onto the app directory with
# "tar -xf <zip> -C <appdir>", and a wrapped folder would land the new files in
# a nested subdirectory instead of replacing the app.
Compress-Archive -Path (Join-Path $outputPath '*') -DestinationPath ($outputPath + ".zip") -CompressionLevel Optimal
Write-Host "Portable folder: $outputPath"
Write-Host "ZIP: $outputPath.zip"
