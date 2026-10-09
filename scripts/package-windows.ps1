param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$CompilerBin = "",
    [string]$VcVars = "",
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "",
    [string]$ValidationDirectory = ""
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
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "packaging/licenses/qtmultimedia/LGPL-3.0-only.txt"))) { throw "Qt Multimedia license materials are missing." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "packaging/licenses/ffmpeg/COPYING.LGPLv2.1"))) { throw "FFmpeg license materials are missing." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "schema/feedback-minimal.schema.json"))) { throw "The current feedback schema is missing." }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot "schema/project-v3.schema.json"))) { throw "The project schema is missing." }
foreach ($videoFile in @("docs/VIDEO-ANNOTATION.md", "schema/video-feedback-v1.schema.json", "schema/video-project-v1.schema.json")) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $videoFile))) { throw "Video usage material is missing: $videoFile" }
}
if ($ValidationDirectory) { $ValidationDirectory = (Resolve-Path -LiteralPath (Join-Path $projectRoot $ValidationDirectory)).Path }
foreach ($notice in @("LICENSE", "LICENSING.md", "COMMERCIAL-LICENSE.md", "NOTICE")) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $notice))) { throw "Application license material is missing: $notice" }
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
Copy-Item -LiteralPath (Join-Path $buildPath "EditHere.exe") -Destination $outputPath
Copy-Item -LiteralPath $versionPath -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $buildPath "edithere-cli.exe") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/windows/integrate.ps1") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/windows/maintain.ps1") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "skills") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "docs/AGENT-CLI.md") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "docs/VIDEO-ANNOTATION.md") -Destination $outputPath
if ($ValidationDirectory) { Copy-Item -LiteralPath $ValidationDirectory -Destination (Join-Path $outputPath "validation") -Recurse }
$previousEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $previousEnvironment[$_.Name] = $_.Value }
try {
    if ($VcVars) {
        $VcVars = (Resolve-Path -LiteralPath $VcVars).Path
        $vcCommand = 'call "' + $VcVars + '" >nul && set'
        $vcEnvironment = & $env:COMSPEC /d /s /c $vcCommand
        if ($LASTEXITCODE) { throw "MSVC deployment environment initialization failed." }
        foreach ($line in $vcEnvironment) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) {
                [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), "Process")
            }
        }
    }
    $env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
    if ($CompilerBin) { $env:PATH = $CompilerBin + ";" + $env:PATH }
    $isMsvc = Test-Path -LiteralPath (Join-Path $QtRoot "lib/Qt6Core.lib")
    $compilerRuntimeOption = if ($isMsvc) { "--no-compiler-runtime" } else { "--compiler-runtime" }
    # The playback surface paints QVideoFrames itself. Keep the full video runtime
    # even when the linker has no direct MultimediaWidgets import to discover.
    & (Join-Path $QtRoot "bin/windeployqt.exe") --release $compilerRuntimeOption -multimedia -multimediawidgets --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation --include-plugins qwebp,ffmpegmediaplugin (Join-Path $outputPath "EditHere.exe")
    if ($LASTEXITCODE) { throw "Qt deployment failed." }
    if ($isMsvc) {
        # windeployqt bundles the VC redistributable installer for MSVC. A
        # portable preview instead needs the freely redistributable release CRT
        # DLLs beside the executable, so running it needs no separate install.
        if (-not $env:VCToolsRedistDir) { throw "MSVC redistribution path is missing. Pass -VcVars with the matching vcvars64.bat." }
        $crtDirectories = @(Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir "x64") -Filter "Microsoft.VC*.CRT" -Directory)
        if ($crtDirectories.Count -ne 1) { throw "Could not identify the matching MSVC x64 release CRT." }
        Get-ChildItem -LiteralPath $crtDirectories[0].FullName -Filter "*.dll" -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $outputPath }
        foreach ($runtimeFile in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
            if (-not (Test-Path -LiteralPath (Join-Path $outputPath $runtimeFile))) { throw "MSVC runtime deployment is incomplete: $runtimeFile. Pass -VcVars with the matching vcvars64.bat." }
        }
    }
} finally {
    Get-ChildItem Env: | Where-Object { -not $previousEnvironment.ContainsKey($_.Name) } | ForEach-Object { [Environment]::SetEnvironmentVariable($_.Name, $null, "Process") }
    foreach ($name in $previousEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], "Process") }
}
# A developer SDK on PATH can hide an incomplete video deployment. Require the
# backend and each FFmpeg library beside the packaged executable before zipping.
foreach ($runtimeFile in @("Qt6Multimedia.dll", "Qt6MultimediaWidgets.dll", "multimedia/ffmpegmediaplugin.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $outputPath $runtimeFile))) { throw "Video runtime deployment is incomplete: $runtimeFile" }
}
$ffmpegLibraries = @()
foreach ($component in @("avcodec", "avformat", "avutil", "swresample", "swscale")) {
    $libraries = @(Get-ChildItem -LiteralPath (Join-Path $QtRoot "bin") -Filter "$component-*.dll" -File)
    if ($libraries.Count -ne 1) { throw "Expected one matching FFmpeg $component runtime in the Qt SDK." }
    $library = $libraries[0]
    $deployedLibrary = Join-Path $outputPath $library.Name
    if (-not (Test-Path -LiteralPath $deployedLibrary)) { throw "FFmpeg deployment is incomplete: $($library.Name)" }
    $sourceHash = (Get-FileHash -LiteralPath $library.FullName -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $deployedLibrary -Algorithm SHA256).Hash -ne $sourceHash) { throw "FFmpeg deployment does not match this Qt SDK: $($library.Name)" }
    $ffmpegLibraries += [PSCustomObject]@{ file = $library.Name; bytes = $library.Length; sha256 = $sourceHash.ToLowerInvariant() }
}
$qtVersion = (& (Join-Path $QtRoot "bin/qmake.exe") -query QT_VERSION).Trim()
if ($LASTEXITCODE -or $qtVersion -notmatch '^6\.\d+\.\d+$') { throw "Could not identify the deployed Qt version." }
$ffmpegVersion = (Get-ChildItem -LiteralPath (Join-Path $QtRoot "bin") -Filter "avutil-*.dll" -File)[0].VersionInfo.ProductVersion
[PSCustomObject]@{ qt = $qtVersion; mediaBackend = "ffmpeg"; ffmpeg = $ffmpegVersion; ffmpegLibraries = $ffmpegLibraries } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputPath "dependency-versions.json") -Encoding utf8
# Qt's own strings (file dialogs, standard message box buttons) follow the interface
# language through qtbase_<locale>.qm. windeployqt runs with --no-translations above to
# keep the package small, so only the one language the interface offers besides its own
# source language is copied in. Skipping this leaves Qt's chrome in English while the
# rest of the interface is Chinese.
$qtTranslationSource = Join-Path (Join-Path $QtRoot "translations") "qtbase_zh_CN.qm"
if (-not (Test-Path -LiteralPath $qtTranslationSource)) { throw "Qt translation for Simplified Chinese is missing: $qtTranslationSource" }
$translationPath = Join-Path $outputPath "translations"
[IO.Directory]::CreateDirectory($translationPath) | Out-Null
Copy-Item -LiteralPath $qtTranslationSource -Destination (Join-Path $translationPath "qtbase_zh_CN.qm")
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/licenses") -Destination $outputPath -Recurse
$sdkSbomPath = Join-Path $outputPath "licenses/qt-sbom"
[IO.Directory]::CreateDirectory($sdkSbomPath) | Out-Null
foreach ($module in @("qtbase", "qtimageformats", "qtmultimedia")) {
    $sdkSbom = Join-Path $QtRoot "sbom/$module-$qtVersion.spdx.json"
    if (Test-Path -LiteralPath $sdkSbom) { Copy-Item -LiteralPath $sdkSbom -Destination $sdkSbomPath }
}
Copy-Item -LiteralPath (Join-Path $projectRoot "schema") -Destination $outputPath -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/使用说明.txt") -Destination $outputPath
Copy-Item -LiteralPath (Join-Path $projectRoot "packaging/THIRD-PARTY-NOTICES.md") -Destination $outputPath
foreach ($notice in @("LICENSE", "LICENSING.md", "COMMERCIAL-LICENSE.md", "NOTICE")) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $notice) -Destination $outputPath
}
$packagedGuide = Join-Path $outputPath "AGENT-CLI.md"
$guideText = [IO.File]::ReadAllText($packagedGuide).Replace("../skills/", "skills/").Replace("../schema/", "schema/").Replace("../README.md", "https://github.com/Inginnng/EditHere").Replace("USER-GUIDE.md", "https://github.com/Inginnng/EditHere/blob/codex/native/docs/USER-GUIDE.md").Replace("AGENT-CLI.en.md", "https://github.com/Inginnng/EditHere/blob/codex/native/docs/AGENT-CLI.en.md")
[IO.File]::WriteAllText($packagedGuide, $guideText, [Text.UTF8Encoding]::new($false))
$packagedVideoGuide = Join-Path $outputPath "VIDEO-ANNOTATION.md"
$videoGuideText = [IO.File]::ReadAllText($packagedVideoGuide).Replace("../schema/", "schema/").Replace("VIDEO-ANNOTATION.en.md", "https://github.com/Inginnng/EditHere/blob/codex/native/docs/VIDEO-ANNOTATION.en.md")
if ($ValidationDirectory) {
    $videoGuideText = $videoGuideText.Replace("../artifacts/video-acceptance/video-acceptance.json", "validation/video-acceptance.json").Replace("../artifacts/video-acceptance/connector-acceptance.json", "validation/connector-acceptance.json").Replace("../artifacts/video-independent-reader/report.md", "validation/independent-ai/report.md").Replace("../artifacts/video-independent-reader/result.json", "validation/independent-ai/result.json")
}
[IO.File]::WriteAllText($packagedVideoGuide, $videoGuideText, [Text.UTF8Encoding]::new($false))
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
