# Start the isolated development preview; never install/register an input method.
param([string]$PythonPath,[switch]$ShowPreview)
$ErrorActionPreference='Stop'
$onlineRoot=Split-Path -Parent $PSScriptRoot
$onlinePython=Join-Path $onlineRoot 'build/online-lite/venv/Scripts/python.exe'
$onlinePreview=Join-Path $onlineRoot 'build/online-lite/native/EType.exe'
if(-not (Test-Path -LiteralPath $onlinePreview)){throw 'Compile the development components with build-online-dev.ps1 first.'}
if(-not (Test-Path -LiteralPath $onlinePython)){
    if(-not $PythonPath){$PythonPath=(Get-Command python -ErrorAction Stop).Source}
    & $PythonPath -m venv (Join-Path $onlineRoot 'build/online-lite/venv')
    if($LASTEXITCODE -ne 0){throw 'Could not create the lightweight development environment'}
    & $onlinePython -m pip install -r (Join-Path $onlineRoot 'requirements-online.txt')
    if($LASTEXITCODE -ne 0){throw 'Could not install online service dependencies'}
}
$onlineReady=$false
try{$onlineHealth=Invoke-RestMethod 'http://127.0.0.1:49182/health' -TimeoutSec 2;$onlineReady=$onlineHealth.online_only -eq $true}catch{}
if(-not $onlineReady){
    $onlineScript=Join-Path $onlineRoot 'scripts/online_service.py'
    $onlineLog=Join-Path $onlineRoot 'build/online-lite'
    Start-Process -FilePath $onlinePython -ArgumentList @('-u',('"{0}"' -f $onlineScript)) -WorkingDirectory $onlineRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $onlineLog 'server.log') -RedirectStandardError (Join-Path $onlineLog 'server-error.log') | Out-Null
    $onlineDeadline=(Get-Date).AddSeconds(10)
    do{Start-Sleep -Milliseconds 200;try{$onlineHealth=Invoke-RestMethod 'http://127.0.0.1:49182/health' -TimeoutSec 1;$onlineReady=$onlineHealth.online_only -eq $true}catch{}}while(-not $onlineReady -and (Get-Date) -lt $onlineDeadline)
    if(-not $onlineReady){throw 'The isolated online development service could not start'}
}
$onlineWindowStyle=if($ShowPreview){'Normal'}else{'Hidden'}
Start-Process -FilePath $onlinePreview -WorkingDirectory (Split-Path -Parent $onlinePreview) -WindowStyle $onlineWindowStyle
Write-Output 'Online development preview started. Existing installed EType is unchanged.'
