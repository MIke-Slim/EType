param([switch]$Validation)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $projectRoot 'tools\inno-setup\ISCC.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Run tools/bootstrap-installer.ps1 to obtain the verified compiler first.' }
if (-not (Test-Path -LiteralPath (Join-Path $projectRoot 'build\EType\package-manifest.json'))) { throw 'Run the complete tests and scripts/package.py before building an installer.' }
$packageRoot = Join-Path $projectRoot 'build\EType'
$manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.version -ne '0.1.1' -or -not $manifest.tested_build.passed) { throw 'The package is not a verified 0.1.1 build.' }
foreach ($file in $manifest.files.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $packageRoot $file.Name) -Algorithm SHA256).Hash.ToLower() -ne $file.Value.sha256) { throw ('Package file changed: '+$file.Name) }
}
$ownedPaths = @($manifest.files.PSObject.Properties.Name) + 'package-manifest.json'
foreach ($file in Get-ChildItem -LiteralPath $packageRoot -Recurse -File) {
    $relative = $file.FullName.Substring($packageRoot.Length+1).Replace('\','/')
    if ($relative -notin $ownedPaths) { throw ('Untracked package file: '+$relative) }
}
$defines = @('/DAppVersion=0.1.1')
if ($Validation) { $defines += '/DInstallerTestMode=1' }
if (-not $Validation) {
    $validationEvidence = Get-Content -LiteralPath (Join-Path $projectRoot 'build\installer-test-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $validationEvidence.passed) { throw 'Installer lifecycle tests must pass before release compilation.' }
    foreach ($item in @(@('build\installer-tests\EType-Setup-Validation.exe','installer_sha256'),@('installer\EType.iss','installer_source_sha256'),@('build\EType\package-manifest.json','package_manifest_sha256'))) {
        if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $item[0]) -Algorithm SHA256).Hash.ToLower() -ne $validationEvidence.($item[1])) { throw ('Changed since installer tests: '+$item[0]) }
    }
}
& $compiler @defines (Join-Path $projectRoot 'installer\EType.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
if (-not $Validation) {
    $output = Join-Path $projectRoot 'releases\EType-0.1.1-Setup.exe'
    $digest = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText($output+'.sha256', $digest+'  '+[IO.Path]::GetFileName($output)+[Environment]::NewLine, [Text.Encoding]::ASCII)
    $releaseFile = Get-Item -LiteralPath $output
    $releaseEvidence = [ordered]@{version='0.1.1';installer_sha256=$digest;bytes=$releaseFile.Length;file_version=$releaseFile.VersionInfo.FileVersion;signature=(Get-AuthenticodeSignature -LiteralPath $output).Status.ToString();installer_checks=$validationEvidence.checks;installer_source_sha256=$validationEvidence.installer_source_sha256;package_manifest_sha256=$validationEvidence.package_manifest_sha256;tested_scope='core, x64/x86 TSF components, preview, isolated installer lifecycle';production_global_install_verified=$false;build_evidence=$manifest.tested_build}
    [IO.File]::WriteAllText(($output -replace '\.exe$','.tests.json'),($releaseEvidence | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    Write-Output ('Installer: '+$output)
}
