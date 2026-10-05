# Direct installation for this computer's test. No EXE installer is generated.
param([string]$InstallDir)
$ErrorActionPreference='Stop'
$onlineRoot=Split-Path -Parent $PSScriptRoot
$onlineSource=Join-Path $onlineRoot 'build/online-test/package'
if(-not (Test-Path -LiteralPath (Join-Path $onlineSource 'online-test-manifest.json'))){throw 'Prepare the independent online test directory first'}
if(-not $InstallDir){$InstallDir=Join-Path $env:LOCALAPPDATA 'Programs/EType-Online-Test-20261006'}
$onlineTarget=[IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
if(Test-Path -LiteralPath $onlineTarget){throw 'The test directory already exists. Choose a new path; old files will not be overwritten.'}
New-Item -ItemType Directory -Path $onlineTarget | Out-Null
Get-ChildItem -LiteralPath $onlineSource | Copy-Item -Destination $onlineTarget -Recurse
& (Join-Path $PSScriptRoot 'register-online-test.ps1') -InstallDir $onlineTarget
if($LASTEXITCODE -ne 0){throw 'Registration did not complete'}
$onlineLink=Join-Path ([Environment]::GetFolderPath('Desktop')) 'EType 在线测试版.lnk'
if(Test-Path -LiteralPath $onlineLink){throw 'A test shortcut already exists; it was not changed'}
$onlineShell=New-Object -ComObject WScript.Shell
$onlineShortcut=$onlineShell.CreateShortcut($onlineLink)
$onlineShortcut.TargetPath=Join-Path $onlineTarget 'EType.exe'
$onlineShortcut.WorkingDirectory=$onlineTarget
$onlineShortcut.IconLocation=Join-Path $onlineTarget 'EType.exe'
$onlineShortcut.Save()
Write-Output ('Online test installed: '+$onlineTarget)
