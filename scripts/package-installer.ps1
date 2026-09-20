param(
    [string]$PackageDirectory='',
    [string]$NsisCompiler='',
    [string]$OutputDirectory='dist'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(!$PackageDirectory){
    $version=[IO.File]::ReadAllText((Join-Path $projectRoot 'build/version.txt')).Trim()
    $PackageDirectory=Join-Path $projectRoot "dist/EditHere-$version-win-x64"
}
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$version=[IO.File]::ReadAllText((Join-Path $package 'version.txt')).Trim()
if($version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid package version.'}
foreach($file in @('EditHere.exe','edithere-cli.exe','integrate.ps1','skills/edithere/SKILL.md','manifest.json')){
    if(!(Test-Path -LiteralPath (Join-Path $package $file) -PathType Leaf)){throw "Package file missing: $file"}
}
# Refuse packaging a folder that has changed since the portable manifest was generated.
foreach($entry in ([IO.File]::ReadAllText((Join-Path $package 'manifest.json')) | ConvertFrom-Json)){
    $file=[IO.Path]::GetFullPath((Join-Path $package $entry.path))
    if(!$file.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe package manifest path.'}
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $entry.sha256){throw "Package hash mismatch: $file"}
}
if(!$NsisCompiler){
    $command=Get-Command makensis.exe -ErrorAction SilentlyContinue
    if($command){$NsisCompiler=$command.Source}
    else { $NsisCompiler=Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe' }
}
if(!(Test-Path -LiteralPath $NsisCompiler)){throw 'Install NSIS 3.x or supply -NsisCompiler <path to makensis.exe>.'}
$output=[IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if(!$output.StartsWith($projectRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be inside the project.'}
[IO.Directory]::CreateDirectory($output) | Out-Null
$installer=Join-Path $output "EditHere-$version-win-x64-setup.exe"
if(Test-Path -LiteralPath $installer){throw 'Installer already exists. Use a fresh OutputDirectory.'}
$work=Join-Path ([IO.Path]::GetTempPath()) ('edithere-installer-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work) | Out-Null
$remove=Join-Path $work 'remove-files.nsh'
function NsisPath([string]$value){return $value.Replace('$','$$').Replace('"','$\"')}
$lines=[Collections.Generic.List[string]]::new()
foreach($file in (Get-ChildItem -LiteralPath $package -Recurse -File)){
    $relative=$file.FullName.Substring($package.Length+1)
    $lines.Add('Delete "$INSTDIR\'+(NsisPath $relative)+'"')
}
foreach($directory in (Get-ChildItem -LiteralPath $package -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)){
    $relative=$directory.FullName.Substring($package.Length+1)
    $lines.Add('RMDir "$INSTDIR\'+(NsisPath $relative)+'"')
}
[IO.File]::WriteAllLines($remove,$lines,[Text.UTF8Encoding]::new($true))
& $NsisCompiler '/V2' '/WX' '/INPUTCHARSET' 'UTF8' "/DAPP_VERSION=$version" "/DPROJECT_ROOT=$projectRoot" "/DPACKAGE_DIR=$package" "/DOUTPUT_FILE=$installer" "/DREMOVE_INCLUDE=$remove" (Join-Path $projectRoot 'packaging/windows/edithere.nsi')
if($LASTEXITCODE){throw 'Installer compilation failed.'}
$hash=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($installer+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($installer)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Host "Installer: $installer"
Write-Host "SHA256: $hash"
