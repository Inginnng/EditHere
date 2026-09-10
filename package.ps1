param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $taskRoot 'build.ps1') }
$distribution = Join-Path $taskRoot 'dist'
$portable = Join-Path $distribution 'Help2Design-Capture-0.2.0-win-x64'
New-Item -ItemType Directory -Path (Join-Path $portable 'schema') -Force | Out-Null
foreach ($name in @('Help2Design.Capture.exe', 'Help2Design.Capture.exe.config')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot ('bin\' + $name)) -Destination (Join-Path $portable $name) -Force
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination (Join-Path $portable 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'schema\feedback-v1.schema.json') -Destination (Join-Path $portable 'schema\feedback-v1.schema.json') -Force
$archive = Join-Path $distribution 'Help2Design-Capture-0.2.0-win-x64.zip'
Compress-Archive -LiteralPath $portable -DestinationPath $archive -Force
Get-Item -LiteralPath $archive | Select-Object FullName,Length
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List
