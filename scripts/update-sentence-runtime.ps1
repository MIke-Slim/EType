param([Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
if (-not [IO.Path]::GetFullPath($ResultPath).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Result must stay in the workspace.' }
$result=[ordered]@{passed=$false;path=$null;backup=$null;updated=@();restart_required=@();runtime_check_error=$null;error=$null;rollback_errors=@()}
$updated=@();$rotated=@{};$backup=$null;$target=$null
try {
    $principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator permission is required.' }
    $installation=Get-Content -LiteralPath (Join-Path $workspace 'build/sentence-profile-install-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $target=[IO.Path]::GetFullPath($installation.path).TrimEnd('\');$root=Split-Path -Parent $target
    if (-not $installation.passed -or (Get-Content -LiteralPath (Join-Path $target 'etype-sentence-trial.id') -Raw) -ne 'EType.SentenceTrial' -or
        (Get-Content -LiteralPath (Join-Path $root 'etype-installation.id') -Raw).Trim() -ne 'EType.WindowsInputMethod') { throw 'Not the managed unified input service.' }
    for ($part=$target; $part; $part=Split-Path -Parent $part) {
        if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation path is not supported.' }
    }
    $source=Join-Path $workspace 'build/EType'
    $evidence=Get-Content -LiteralPath (Join-Path $workspace 'build/build-test-evidence.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $sentence=Get-Content -LiteralPath (Join-Path $workspace 'build/enter-sentence-ui.json.sentences.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $evidence.passed -or -not $sentence.passed) { throw 'Passed build and sentence UI checks are required.' }
    $liveChecks=@{}
    foreach ($arch in @('x64','x86')) {
        $checks=Get-Content -LiteralPath (Join-Path $workspace ('build/enter-mode-tests-'+$arch+'.txt')) -Raw -Encoding UTF8
        if ($checks -notmatch 'tsf_checks=\d+ failures=0') { throw 'Live Enter-key checks must pass.' }
        $liveChecks[$arch]=$checks.Trim()
    }
    $identity=Get-Content -LiteralPath (Join-Path $source 'input-service-identity.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($identity.clsid -ne '{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}') { throw 'Wrong input-service identity.' }
    foreach ($pair in @(@([Microsoft.Win32.RegistryView]::Registry64,'x64'),@([Microsoft.Win32.RegistryView]::Registry32,'x86'))) {
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$pair[0])
        try {
            $key=$base.OpenSubKey('Software\Classes\CLSID\'+$identity.clsid+'\InprocServer32')
            if (-not $key) { throw 'Unified registration is missing.' }
            try { if ($key.GetValue('') -ne (Join-Path $target ($pair[1]+'\EType.dll'))) { throw 'Registration belongs to another installation.' } } finally { $key.Dispose() }
        } finally { $base.Dispose() }
    }
    foreach ($file in $evidence.files.PSObject.Properties) {
        if ($file.Name.Contains('..') -or [IO.Path]::IsPathRooted($file.Name)) { throw 'Invalid build path.' }
        if ((Get-FileHash -LiteralPath (Join-Path $source $file.Name)).Hash.ToLower() -ne $file.Value) { throw 'Source changed after tests.' }
        if ($file.Name -notin @('EType.exe','x64/EType.dll','x86/EType.dll') -and (Get-FileHash -LiteralPath (Join-Path $target $file.Name)).Hash.ToLower() -ne $file.Value) { throw 'Unrelated installed data differs; preserve it.' }
    }
    $files=@('EType.exe','x64/EType.dll','x86/EType.dll','使用说明.txt','package-manifest.json')
    $backup=Join-Path $workspace ('build/sentence-runtime-backup-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
    foreach ($file in $files) {
        $saved=Join-Path $backup $file
        New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $target $file) -Destination $saved
    }
    $result.path=$target;$result.backup=$backup
    foreach ($file in @('EType.exe','x64/EType.dll','x86/EType.dll','使用说明.txt')) {
        $updated+=$file
        if ($file -ne '使用说明.txt') {
            # Windows can retain an image lock. Rename that file in its own
            # directory, preserving its mapped image, then install a new file.
            $existing=[IO.Path]::GetFullPath((Join-Path $target $file))
            $retained=$existing+'.before-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')
            if (-not $existing.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or
                -not $retained.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime paths must stay in the installation.' }
            Move-Item -LiteralPath $existing -Destination $retained
            $rotated[$file]=$retained
        }
        Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $target $file) -Force
        if ((Get-FileHash -LiteralPath (Join-Path $source $file)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $target $file)).Hash) { throw 'Installed hash mismatch.' }
    }
    $manifest=Get-Content -LiteralPath (Join-Path $target 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifest.tested_build=$evidence;$updated+='package-manifest.json'
    $manifest | Add-Member -NotePropertyName enter_key_validation -NotePropertyValue @{live=$liveChecks;sentence_ui=$sentence} -Force
    [IO.File]::WriteAllText((Join-Path $target 'package-manifest.json'),($manifest | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    $result.updated=$updated;$result.passed=$true
    try {
        $runtime=& (Join-Path $PSScriptRoot 'inspect-loaded-input-service.ps1') -InstallDir $target | ConvertFrom-Json
        $result.restart_required=@($runtime.restart_required | Select-Object pid,process,path)
    } catch { $result.runtime_check_error=$_.Exception.Message }
} catch {
    $result.error=$_.Exception.Message
    foreach ($file in $updated) {
        try {
            $destination=Join-Path $target $file
            if ($rotated.ContainsKey($file)) {
                if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination }
                Move-Item -LiteralPath $rotated[$file] -Destination $destination
            } elseif ((Get-FileHash -LiteralPath (Join-Path $backup $file)).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) {
                Copy-Item -LiteralPath (Join-Path $backup $file) -Destination $destination -Force
            }
        }
        catch { $result.rollback_errors+=$_.Exception.Message }
    }
} finally { [IO.File]::WriteAllText($ResultPath,($result | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false))) }
if (-not $result.passed) { exit 1 }
