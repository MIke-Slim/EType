param([Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
if (-not [IO.Path]::GetFullPath($ResultPath).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Result must stay inside the workspace.' }
$result=[ordered]@{passed=$false;unified_path=$null;removed_legacy=@();backup=$null;error=$null;rollback_errors=@()}
$removed=@();$oldShortcut=$null;$newShortcut=$null;$shortcutChanged=$false;$helpChanged=$false
try {
    $principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator permission is required.' }
    $installed=Get-Content -LiteralPath (Join-Path $workspace 'build/sentence-profile-install-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $target=[IO.Path]::GetFullPath($installed.path).TrimEnd('\');$root=Split-Path -Parent $target
    if (-not $installed.passed -or (Get-Content -LiteralPath (Join-Path $target 'etype-sentence-trial.id') -Raw) -ne 'EType.SentenceTrial' -or
        (Get-Content -LiteralPath (Join-Path $root 'etype-installation.id') -Raw).Trim() -ne 'EType.WindowsInputMethod') { throw 'Managed unified input service is required.' }
    for ($part=$target; $part; $part=Split-Path -Parent $part) {
        if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation path is not supported.' }
    }
    foreach ($arch in @('x64','x86')) {
        $checks=Get-Content -LiteralPath (Join-Path $workspace ('build/unified-mode-tests-'+$arch+'.txt')) -Raw
        if ($checks -notmatch 'tsf_checks=(\d+) failures=0' -or [int]$Matches[1] -lt 165) { throw 'Word/sentence switching and live translation checks must pass.' }
    }
    $evidence=Get-Content -LiteralPath (Join-Path $workspace 'build/build-test-evidence.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($file in $evidence.files.PSObject.Properties) {
        if ((Get-FileHash -LiteralPath (Join-Path $target $file.Name)).Hash.ToLower() -ne $file.Value) { throw 'Unified input service differs from tested build.' }
    }
    $legacy=@();$legacyId='{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}';$unifiedId='{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}'
    foreach ($pair in @(@([Microsoft.Win32.RegistryView]::Registry64,'x64','System32'),@([Microsoft.Win32.RegistryView]::Registry32,'x86','SysWOW64'))) {
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$pair[0])
        try {
            $newKey=$base.OpenSubKey('Software\Classes\CLSID\'+$unifiedId+'\InprocServer32')
            if (-not $newKey) { throw 'Unified registration is missing.' }
            try { if ($newKey.GetValue('') -ne (Join-Path $target ($pair[1]+'\EType.dll'))) { throw 'Unified registration belongs to another installation.' } } finally { $newKey.Dispose() }
            $oldKey=$base.OpenSubKey('Software\Classes\CLSID\'+$legacyId+'\InprocServer32')
            if ($oldKey) {
                try {
                    $dll=Join-Path $root ($pair[1]+'\EType.dll')
                    if ($oldKey.GetValue('') -ne $dll) { throw 'Legacy registration belongs to another installation.' }
                    $legacy+=,@{architecture=$pair[1];dll=$dll;registrar=(Join-Path $env:WINDIR ($pair[2]+'\regsvr32.exe'))}
                } finally { $oldKey.Dispose() }
            }
        } finally { $base.Dispose() }
    }
    $backup=Join-Path $workspace ('build/unified-profile-backup-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    $result.backup=$backup;$result.unified_path=$target
    Copy-Item -LiteralPath (Join-Path $target '使用说明.txt') -Destination (Join-Path $backup 'usage-before.txt')
    [IO.File]::WriteAllText((Join-Path $backup 'legacy-registration.json'),($legacy | ConvertTo-Json -Depth 5),(New-Object Text.UTF8Encoding($false)))
    $programs=[Environment]::GetFolderPath('CommonPrograms')
    $oldShortcut=Join-Path $programs 'EType 英文词汇输入法.lnk';$newShortcut=Join-Path $programs 'EType 单词与句子.lnk'
    $shell=New-Object -ComObject WScript.Shell
    if (Test-Path -LiteralPath $oldShortcut) {
        if ($shell.CreateShortcut($oldShortcut).TargetPath -ne (Join-Path $root 'EType.exe')) { throw 'Start-menu shortcut does not belong to this installation.' }
        if (Test-Path -LiteralPath $newShortcut) { throw 'Unified start-menu shortcut already exists; preserve it.' }
        Copy-Item -LiteralPath $oldShortcut -Destination (Join-Path $backup 'start-menu.lnk')
    } else { $oldShortcut=$null }
    foreach ($entry in $legacy) {
        $removed+=,$entry
        $process=Start-Process -FilePath $entry.registrar -ArgumentList @('/s','/u',('"'+$entry.dll+'"')) -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw ('Legacy unregister failed: '+$entry.architecture) }
        $result.removed_legacy+= $entry.architecture
    }
    if ($oldShortcut) {
        $shortcutChanged=$true
        $link=$shell.CreateShortcut($newShortcut)
        $link.TargetPath=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $link.Arguments='-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+(Join-Path $PSScriptRoot 'start-installed-development.ps1')+'" -InstallDir "'+$target+'"'
        $link.WorkingDirectory=$workspace;$link.IconLocation=(Join-Path $target 'EType.exe')+',0';$link.Description='EType 单词与句子 · Ctrl+Shift+空格切换';$link.Save()
        Remove-Item -LiteralPath $oldShortcut
    }
    $helpChanged=$true
    Copy-Item -LiteralPath (Join-Path $workspace 'docs/使用说明.txt') -Destination (Join-Path $target '使用说明.txt') -Force
    $result.passed=$true
} catch {
    $result.error=$_.Exception.Message
    foreach ($entry in $removed) {
        try {
            $process=Start-Process -FilePath $entry.registrar -ArgumentList @('/s',('"'+$entry.dll+'"')) -WindowStyle Hidden -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw 'Legacy registration restore failed.' }
        } catch { $result.rollback_errors+=$_.Exception.Message }
    }
    if ($shortcutChanged) {
        try { Copy-Item -LiteralPath (Join-Path $result.backup 'start-menu.lnk') -Destination $oldShortcut -Force; if (Test-Path -LiteralPath $newShortcut) { Remove-Item -LiteralPath $newShortcut } }
        catch { $result.rollback_errors+=$_.Exception.Message }
    }
    if ($helpChanged) {
        try { Copy-Item -LiteralPath (Join-Path $result.backup 'usage-before.txt') -Destination (Join-Path $target '使用说明.txt') -Force }
        catch { $result.rollback_errors+=$_.Exception.Message }
    }
} finally { [IO.File]::WriteAllText($ResultPath,($result | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false))) }
if (-not $result.passed) { exit 1 }
