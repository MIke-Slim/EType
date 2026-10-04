param([Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
$workspace=Split-Path -Parent $PSScriptRoot
$principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator permission is required.' }
if (-not [IO.Path]::GetFullPath($ResultPath).StartsWith([IO.Path]::GetFullPath($workspace).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Result must stay in the workspace.' }
trap { [IO.File]::WriteAllText($ResultPath,(@{passed=$false;error=$_.Exception.Message} | ConvertTo-Json),(New-Object Text.UTF8Encoding($false))); exit 1 }
$installation=Get-Content -LiteralPath (Join-Path $workspace 'build/sentence-profile-install-result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$target=[IO.Path]::GetFullPath($installation.path).TrimEnd('\')
$root=Split-Path -Parent $target
if (-not $installation.passed -or (Get-Content -LiteralPath (Join-Path $target 'etype-sentence-trial.id') -Raw) -ne 'EType.SentenceTrial' -or
    (Get-Content -LiteralPath (Join-Path $root 'etype-installation.id') -Raw).Trim() -ne 'EType.WindowsInputMethod') { throw 'Not the installed sentence preview.' }
for ($part=$target; $part; $part=Split-Path -Parent $part) {
    if ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked installation path is not supported.' }
}
$source=Join-Path $workspace 'build/EType'
$evidence=Get-Content -LiteralPath (Join-Path $workspace 'build/build-test-evidence.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $evidence.passed) { throw 'Passed build evidence is required.' }
foreach ($file in $evidence.files.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $source $file.Name)).Hash.ToLower() -ne $file.Value) { throw 'Source changed after tests.' }
    if ($file.Name -ne 'EType.exe' -and (Get-FileHash -LiteralPath (Join-Path $target $file.Name)).Hash.ToLower() -ne $file.Value) { throw 'This operation only updates the launcher.' }
}
$exe=Join-Path $target 'EType.exe'
$backup=Join-Path $workspace ('build/sentence-launcher-backup-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'.exe')
Copy-Item -LiteralPath $exe -Destination $backup
$manifestPath=Join-Path $target 'package-manifest.json'
Copy-Item -LiteralPath $manifestPath -Destination ($backup+'.manifest.json')
try {
    Copy-Item -LiteralPath (Join-Path $source 'EType.exe') -Destination $exe -Force
    if ((Get-FileHash -LiteralPath $exe).Hash.ToLower() -ne $evidence.files.'EType.exe') { throw 'Installed launcher hash mismatch.' }
    $manifest=Get-Content -LiteralPath (Join-Path $target 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifest.tested_build=$evidence
    [IO.File]::WriteAllText((Join-Path $target 'package-manifest.json'),($manifest | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    [IO.File]::WriteAllText($ResultPath,(@{passed=$true;path=$target;backup=$backup} | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
} catch { Copy-Item -LiteralPath $backup -Destination $exe -Force; Copy-Item -LiteralPath ($backup+'.manifest.json') -Destination $manifestPath -Force; throw }
