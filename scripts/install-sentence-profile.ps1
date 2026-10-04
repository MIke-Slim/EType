param([Parameter(Mandatory=$true)][string]$SourceDir,
      [string]$InstallDir='C:\Program Files\EType',
      [Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
$result=[ordered]@{passed=$false;path=$null;registered=@();error=$null;rollback_errors=@()}
$registered=@()
try {
    $principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator permission is required.' }
    $workspace=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
    $source=[IO.Path]::GetFullPath($SourceDir).TrimEnd('\')
    $root=[IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
    if (-not $source.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFullPath($ResultPath).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Source and result must stay in the workspace.' }
    if ((Get-Content -LiteralPath (Join-Path $root 'etype-installation.id') -Raw).Trim() -ne 'EType.WindowsInputMethod') { throw 'Not a managed EType installation.' }
    for ($part=$root; $part; $part=Split-Path -Parent $part) {
        if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation directory is not supported.' }
    }
    $evidence=Get-Content -LiteralPath (Join-Path (Split-Path -Parent $source) 'build-test-evidence.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $identity=Get-Content -LiteralPath (Join-Path $source 'input-service-identity.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $evidence.passed -or -not $identity.sentence_trial -or $identity.clsid -ne '{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}' -or $identity.profile -ne '{D6B9B4A4-8D60-4F7E-A673-301E64C91158}') { throw 'Passed sentence-profile build is required.' }
    foreach ($file in $evidence.files.PSObject.Properties) {
        if ($file.Name.Contains('..') -or [IO.Path]::IsPathRooted($file.Name)) { throw 'Invalid evidence path.' }
        if ((Get-FileHash -LiteralPath (Join-Path $source $file.Name)).Hash.ToLower() -ne $file.Value) { throw ('Build changed after tests: '+$file.Name) }
    }
    foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)) {
        $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
        try {
            $key=$base.OpenSubKey('Software\Classes\CLSID\'+$identity.clsid+'\InprocServer32')
            if ($key) { $key.Dispose(); throw 'Sentence preview already registered. Unregister that preview before changing its path.' }
        } finally { $base.Dispose() }
    }
    $target=Join-Path $root ('sentence-trial-'+$evidence.files.'x64/EType.dll'.Substring(0,12))
    if (Test-Path -LiteralPath $target) { throw 'Sentence preview directory already exists; keep it for recovery.' }
    $result.path=$target
    # Use a fresh path as well as a fresh CLSID: LoadLibrary otherwise reuses
    # the old mapped image inside the user's still-running application.
    New-Item -ItemType Directory -Path $target | Out-Null
    $copyFiles=@($evidence.files.PSObject.Properties.Name)+@('registration.ps1','uninstall.ps1','install.ps1','使用说明.txt','assets/etype.ico')
    foreach ($relative in $copyFiles) {
        $incoming=Join-Path $source $relative
        $destination=Join-Path $target $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $incoming -Destination $destination
        if ((Get-FileHash -LiteralPath $incoming).Hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw ('Installed hash mismatch: '+$relative) }
    }
    [IO.File]::WriteAllText((Join-Path $target 'etype-sentence-trial.id'),'EType.SentenceTrial',(New-Object Text.UTF8Encoding($false)))
    $manifest=@{development_preview=$true;input_service=$identity;tested_build=$evidence;legacy_installation=$root}
    [IO.File]::WriteAllText((Join-Path $target 'package-manifest.json'),($manifest | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    foreach ($pair in @(@('SysWOW64','x86'),@('System32','x64'))) {
        $registrar=Join-Path $env:WINDIR ($pair[0]+'\regsvr32.exe')
        $dll=Join-Path $target ($pair[1]+'\EType.dll')
        $registered+=,@($registrar,$dll)
        $process=Start-Process -FilePath $registrar -ArgumentList @('/s',('"'+$dll+'"')) -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw ('Sentence-profile registration failed: '+$pair[1]) }
        $result.registered+= $pair[1]
    }
    $result.passed=$true
} catch {
    $result.error=$_.Exception.Message
    foreach ($pair in $registered) {
        try {
            $process=Start-Process -FilePath $pair[0] -ArgumentList @('/s','/u',('"'+$pair[1]+'"')) -WindowStyle Hidden -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw 'Registration rollback failed.' }
        } catch { $result.rollback_errors+=$_.Exception.Message }
    }
} finally { [IO.File]::WriteAllText($ResultPath,($result | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false))) }
if (-not $result.passed) { exit 1 }
