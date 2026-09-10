param([switch]$Test, [switch]$UiTest)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler = Join-Path $framework 'csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.8 编译器不可用。请使用 Visual Studio 或 .NET SDK 构建 csproj。' }
$outDir = Join-Path $taskRoot 'bin'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$refs = @('System.dll','System.Core.dll','System.Drawing.dll','System.Windows.Forms.dll','System.Web.Extensions.dll','System.Xaml.dll','System.Xml.dll') | ForEach-Object { '/reference:' + (Join-Path $framework $_) }
$refs += @('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','UIAutomationClient.dll','UIAutomationTypes.dll') | ForEach-Object { '/reference:' + (Join-Path (Join-Path $framework 'WPF') $_) }
$sources = Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src') -Filter '*.cs' | ForEach-Object { $_.FullName }
$output = Join-Path $outDir 'Help2Design.Capture.exe'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 ('/win32manifest:' + (Join-Path $taskRoot 'src\app.manifest')) ('/resource:' + (Join-Path $taskRoot 'src\Theme.xaml') + ',Theme.xaml') ('/out:' + $output) @refs @sources
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
Copy-Item -LiteralPath (Join-Path $taskRoot 'src\App.config') -Destination ($output + '.config') -Force
Write-Output $output
if ($Test) {
  $evidence = Join-Path $taskRoot 'artifacts\self-test'
  New-Item -ItemType Directory -Path $evidence -Force | Out-Null
  $process = Start-Process -FilePath $output -ArgumentList @('--self-test', ('"' + $evidence + '"')) -WindowStyle Hidden -Wait -PassThru
  Get-Content -LiteralPath (Join-Path $evidence 'results.txt')
  if ($process.ExitCode -ne 0) { throw '自检失败。' }
}
if ($UiTest) {
  foreach ($mode in @('ui-test', 'startup-test')) {
    $evidence = Join-Path $taskRoot ('artifacts\' + $mode + '-v2')
    $process = Start-Process -FilePath $output -ArgumentList @(('--' + $mode), ('"' + $evidence + '"')) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000)) { $process.Kill(); throw "$mode 超时。" }
    $resultName = if ($mode -eq 'ui-test') { 'ui-results.txt' } else { 'startup-results.txt' }
    $result = Get-Content -LiteralPath (Join-Path $evidence $resultName)
    $result
    if ($result -match '^FAIL' -or $process.ExitCode -ne 0) { throw "$mode 失败。" }
  }
}
