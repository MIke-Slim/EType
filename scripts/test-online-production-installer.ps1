# Exercise only a fresh, dedicated test installation; preserve existing EType variants.
$ErrorActionPreference='Stop'
$onlineRoot=Split-Path -Parent $PSScriptRoot
$onlineInstaller=Join-Path $onlineRoot 'releases/EType-0.3.0-Online-Setup.exe'
$onlineTarget=[IO.Path]::GetFullPath((Join-Path $onlineRoot ('build/online-release/正式 安装 验证-'+[Guid]::NewGuid().ToString('N').Substring(0,8))))
$onlineBoundary=[IO.Path]::GetFullPath((Join-Path $onlineRoot 'build/online-release'))+[IO.Path]::DirectorySeparatorChar
if(-not $onlineTarget.StartsWith($onlineBoundary,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $onlineTarget)){throw 'Production test target is not a fresh directory inside the build workspace'}
$onlineRecords=@()
function OnlineCheck([string]$name,[bool]$passed){
    $script:onlineRecords+=@{name=$name;passed=$passed}
    if(-not $passed){throw ('Production installer test failed: '+$name)}
}
function OnlineSnapshot {
    $result=@{}
    foreach($class in @('{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}','{2508C9AF-571F-45AC-8709-5CD3C0B14366}')){
        foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
            $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
            try{$key=$base.OpenSubKey('Software\Classes\CLSID\'+$class+'\InprocServer32');try{$result[$class+$view]=if($key){[string]$key.GetValue('')}else{''}}finally{if($key){$key.Dispose()}}}finally{$base.Dispose()}
        }
    }
    return $result
}
$onlineBefore=OnlineSnapshot
$onlinePassed=$false
try{
    $installArgs=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+$onlineTarget+'"'),('/LOG="'+(Join-Path $onlineRoot 'build/online-release/production-install.log')+'"'))
    $installed=Start-Process -FilePath $onlineInstaller -Verb RunAs -WindowStyle Hidden -ArgumentList $installArgs -Wait -PassThru
    OnlineCheck 'formal_installer_accepts_custom_chinese_space_path' ($installed.ExitCode -eq 0)
    foreach($arch in @('x64','x86')){
        & (Join-Path $onlineRoot ('build/online-release/registration-tests-'+$arch+'.exe')) $onlineTarget
        OnlineCheck ('actual_windows_registration_'+$arch) ($LASTEXITCODE -eq 0)
    }
    $payload=Get-Content -LiteralPath (Join-Path $onlineTarget 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $match=$true
    foreach($file in $payload.files.PSObject.Properties){if((Get-FileHash -LiteralPath (Join-Path $onlineTarget $file.Name)).Hash.ToLower() -ne $file.Value){$match=$false}}
    OnlineCheck 'formal_installed_payload_matches_all_hashes' $match
    $script:onlineInstalledBytes=(Get-ChildItem -LiteralPath $onlineTarget -Recurse -File | Measure-Object Length -Sum).Sum
    $report=Join-Path $onlineRoot 'build/online-release/production-ui.json'
    $ui=Start-Process -FilePath (Join-Path $onlineTarget 'EType.exe') -WindowStyle Hidden -ArgumentList @('--online-selftest',('"'+$report+'"')) -PassThru
    $ui.WaitForExit();$ui.Refresh()
    $uiResult=Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
    OnlineCheck 'formal_installed_word_sentence_and_cold_start' ($ui.ExitCode -eq 0 -and $uiResult.passed)
    $speechFile=Join-Path $onlineRoot 'build/online-release/production-speech.mp3'
    $speech=Start-Process -FilePath (Join-Path $onlineTarget 'EType.exe') -WindowStyle Hidden -ArgumentList @('--natural-speech-test',('"'+$speechFile+'"')) -PassThru
    $speech.WaitForExit();$speech.Refresh()
    OnlineCheck 'formal_installed_online_speech' ($speech.ExitCode -eq 0 -and (Get-Item -LiteralPath $speechFile).Length -gt 100)
    $sentinel=Join-Path $onlineTarget 'user-added-note.txt'
    [IO.File]::WriteAllText($sentinel,'Retain this user file during uninstall.')
    $uninstall=Start-Process -FilePath (Join-Path $onlineTarget 'unins000.exe') -Verb RunAs -WindowStyle Hidden -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $onlineRoot 'build/online-release/production-uninstall.log')+'"')) -Wait -PassThru
    OnlineCheck 'formal_uninstall_stops_running_worker_and_completes' ($uninstall.ExitCode -eq 0)
    OnlineCheck 'formal_uninstall_preserves_user_added_file' (Test-Path -LiteralPath $sentinel)
    OnlineCheck 'formal_uninstall_removes_owned_payload' (-not (Test-Path -LiteralPath (Join-Path $onlineTarget 'EType.exe')) -and -not (Test-Path -LiteralPath (Join-Path $onlineTarget 'runtime/python/pythonw.exe')))
    $classPath='HKLM:\SOFTWARE\Classes\CLSID\{7BD6247C-64A2-4AA9-B702-C731413CD0A2}'
    OnlineCheck 'formal_uninstall_removes_own_registration' (-not (Test-Path $classPath))
    $onlineAfter=OnlineSnapshot
    $unchanged=$true;foreach($key in $onlineBefore.Keys){if($onlineBefore[$key] -ne $onlineAfter[$key]){$unchanged=$false}}
    OnlineCheck 'offline_and_online_test_registrations_unchanged' $unchanged
    $onlinePassed=$true
}finally{
    $evidence=@{passed=$onlinePassed;checks=$onlineRecords;installed_bytes=$script:onlineInstalledBytes;installer_sha256=(Get-FileHash -LiteralPath $onlineInstaller).Hash.ToLower();test_path=$onlineTarget}
    $evidence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $onlineRoot 'build/online-release/production-results.json') -Encoding UTF8
}
Write-Output ('Production installer checks passed: '+$onlineRecords.Count)
