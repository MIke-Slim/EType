param([Parameter(Mandatory=$true)][string]$InstallDir)
$ErrorActionPreference='Stop'
$onlineTarget=[IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$onlineManifest=Get-Content -LiteralPath (Join-Path $onlineTarget 'online-test-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$onlineClass='{2508C9AF-571F-45AC-8709-5CD3C0B14366}'
if($onlineManifest.variant -ne 'online-test' -or $onlineManifest.clsid -ne $onlineClass){throw 'Wrong test package identity'}
foreach($onlineFile in @('EType.exe','x64/EType.dll','x86/EType.dll','runtime/python/pythonw.exe','runtime/online_service.py')){
    if(-not (Test-Path -LiteralPath (Join-Path $onlineTarget $onlineFile) -PathType Leaf)){throw "Missing test component: $onlineFile"}
}
$onlineKey='Software\Classes\CLSID\'+$onlineClass+'\InprocServer32'
foreach($onlineView in @(@([Microsoft.Win32.RegistryView]::Registry64,'x64'),@([Microsoft.Win32.RegistryView]::Registry32,'x86'))){
    $onlineBase=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$onlineView[0])
    try{$onlineExisting=$onlineBase.OpenSubKey($onlineKey)
        if($onlineExisting){try{$onlineRegistered=[string]$onlineExisting.GetValue('');if($onlineRegistered -and $onlineRegistered -ne (Join-Path $onlineTarget ($onlineView[1]+'\EType.dll'))){throw 'A different online test version is registered; do not overwrite it'}}finally{$onlineExisting.Dispose()}}
    }finally{$onlineBase.Dispose()}
}
$onlinePrincipal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $onlinePrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
    $onlineHost=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $onlineArguments='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -InstallDir "'+$onlineTarget+'"'
    $onlineElevated=Start-Process -FilePath $onlineHost -Verb RunAs -WindowStyle Hidden -ArgumentList $onlineArguments -Wait -PassThru
    if($onlineElevated.ExitCode -ne 0){throw "Online registration failed: $($onlineElevated.ExitCode)"}
    exit 0
}
$onlineCompleted=@()
try{
    foreach($onlineArchitecture in @(@('SysWOW64','x86'),@('System32','x64'))){
        $onlineRegistrar=Join-Path $env:WINDIR ($onlineArchitecture[0]+'\regsvr32.exe')
        $onlineDll=Join-Path $onlineTarget ($onlineArchitecture[1]+'\EType.dll')
        $onlineProcess=Start-Process -FilePath $onlineRegistrar -ArgumentList @('/s',('"'+$onlineDll+'"')) -WindowStyle Hidden -Wait -PassThru
        if($onlineProcess.ExitCode -ne 0){throw "Online registration failed: $($onlineArchitecture[1]) exit $($onlineProcess.ExitCode)"}
        $onlineCompleted+=,@($onlineRegistrar,$onlineDll)
    }
}catch{
    foreach($onlineEntry in $onlineCompleted){Start-Process -FilePath $onlineEntry[0] -ArgumentList @('/s','/u',('"'+$onlineEntry[1]+'"')) -WindowStyle Hidden -Wait | Out-Null}
    throw
}
@{registered=$true;clsid=$onlineClass;profile=$onlineManifest.profile;path=$onlineTarget;time=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $onlineTarget 'registration-result.json') -Encoding UTF8
Write-Output 'Independent online test profile registered; offline profiles unchanged.'
