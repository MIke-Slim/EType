param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [Parameter(Mandatory=$true)][string]$InstallDir,
    [Parameter(Mandatory=$true)][string]$BackupDir,
    [Parameter(Mandatory=$true)][string]$ResultPath
)
$ErrorActionPreference = 'Stop'
$updated = @()
$result = [ordered]@{ passed=$false; installed_at=$InstallDir; backup=$BackupDir; updated=@(); error=$null; rollback_errors=@(); restart_required=@(); runtime_check_error=$null }
try {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator permission is required.' }
    $source = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\')
    $target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
    $backup = [IO.Path]::GetFullPath($BackupDir).TrimEnd('\')
    $workspace = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
    if (-not $source.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or
        -not $backup.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFullPath($ResultPath).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Source, backup and result must stay inside the workspace.' }
    if (Test-Path -LiteralPath $backup) { throw 'Use a new backup directory.' }
    if ((Get-Content -LiteralPath (Join-Path $target 'etype-installation.id') -Raw).Trim() -ne 'EType.WindowsInputMethod' -or
        -not (Test-Path -LiteralPath (Join-Path $target 'unins000.exe'))) { throw 'Target is not a managed EType installation.' }
    for ($part=$target; $part; $part=Split-Path -Parent $part) {
        if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installation cannot pass through a linked directory.' }
    }
    foreach ($pair in @(@([Microsoft.Win32.RegistryView]::Registry64,'x64'),@([Microsoft.Win32.RegistryView]::Registry32,'x86'))) {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$pair[0])
        try {
            $key = $base.OpenSubKey('Software\Classes\CLSID\{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}\InprocServer32')
            if (-not $key) { throw 'Existing input-service registration is missing.' }
            try {
                if ([IO.Path]::GetFullPath([string]$key.GetValue('')) -ne (Join-Path $target ($pair[1]+'\EType.dll'))) { throw 'Registered component does not belong to this installation.' }
            } finally { $key.Dispose() }
        } finally { $base.Dispose() }
    }
    $evidence = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $source) 'build-test-evidence.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $evidence.passed) { throw 'Passed build evidence is required.' }
    $identityPath=Join-Path $source 'input-service-identity.json'
    if (Test-Path -LiteralPath $identityPath) {
        $identity=Get-Content -LiteralPath $identityPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($identity.sentence_trial) { throw 'Use install-sentence-profile.ps1 for the separate sentence preview.' }
    }
    $files = @($evidence.files.PSObject.Properties.Name | Where-Object { $_ -ne 'input-service-identity.json' }) + 'package-manifest.json'
    foreach ($relative in $files) {
        if ($relative.Contains('..') -or [IO.Path]::IsPathRooted($relative)) { throw 'Invalid package path.' }
        $incoming = Join-Path $source $relative
        $existing = Join-Path $target $relative
        if (-not (Test-Path -LiteralPath $incoming -PathType Leaf) -or -not (Test-Path -LiteralPath $existing -PathType Leaf)) { throw ('Missing file: '+$relative) }
        if ($relative -ne 'package-manifest.json' -and (Get-FileHash -LiteralPath $incoming -Algorithm SHA256).Hash.ToLower() -ne $evidence.files.$relative) { throw ('Build changed after tests: '+$relative) }
        $handle = [IO.File]::Open($existing,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        $handle.Dispose()
    }
    foreach ($relative in $files) {
        $saved = Join-Path $backup $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $target $relative) -Destination $saved
    }
    foreach ($relative in $files) {
        $updated += $relative
        Copy-Item -LiteralPath (Join-Path $source $relative) -Destination (Join-Path $target $relative) -Force
        if ((Get-FileHash -LiteralPath (Join-Path $source $relative) -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath (Join-Path $target $relative) -Algorithm SHA256).Hash) { throw ('Installed hash mismatch: '+$relative) }
    }
    $result.passed=$true
    $result.updated=$updated
    # Copying files does not replace DLL images already mapped into applications.
    # Installation success and the running input-service version are separate facts.
    try {
        $runtime = & (Join-Path $PSScriptRoot 'inspect-loaded-input-service.ps1') -InstallDir $target | ConvertFrom-Json
        $result.restart_required = @($runtime.restart_required | Select-Object pid,process,path)
    } catch { $result.runtime_check_error=$_.Exception.Message }
} catch {
    $result.error=$_.Exception.Message
    foreach ($relative in $updated) {
        try { Copy-Item -LiteralPath (Join-Path $BackupDir $relative) -Destination (Join-Path $InstallDir $relative) -Force }
        catch { $result.rollback_errors += ($relative+': '+$_.Exception.Message) }
    }
} finally {
    [IO.File]::WriteAllText($ResultPath,($result | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
}
if (-not $result.passed) { exit 1 }
