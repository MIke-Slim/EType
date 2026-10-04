param([switch]$Validation, [string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $projectRoot 'tools\inno-setup\ISCC.exe'
if ($CompilerPath) { $compiler = [IO.Path]::GetFullPath($CompilerPath) }
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Run tools/bootstrap-installer.ps1 to obtain the verified compiler first.' }
$packageRoot = Join-Path $projectRoot 'build\standalone\EType'
$manifestPath = Join-Path $packageRoot 'package-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.version -ne '0.2.0' -or -not $manifest.tested_build.passed -or -not $manifest.offline_models_included) { throw 'The package is not a verified standalone 0.2.0 build.' }
$serviceEvidence = Get-Content -LiteralPath (Join-Path $projectRoot 'build\standalone-service-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $serviceEvidence.passed -or $serviceEvidence.package_manifest_sha256 -ne (Get-FileHash -LiteralPath $manifestPath).Hash.ToLower()) { throw 'Standalone service tests are missing or stale.' }
$autoStart = Get-Content -LiteralPath (Join-Path $projectRoot 'build\standalone-autostart-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $autoStart.passed -or $autoStart.package_manifest_sha256 -ne (Get-FileHash -LiteralPath $manifestPath).Hash.ToLower()) { throw 'Native standalone cold-start evidence is missing or stale.' }
foreach ($file in $manifest.files.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $packageRoot $file.Name) -Algorithm SHA256).Hash.ToLower() -ne $file.Value.sha256) { throw ('Package file changed: '+$file.Name) }
}
$ownedPaths = @($manifest.files.PSObject.Properties.Name) + 'package-manifest.json'
foreach ($file in Get-ChildItem -LiteralPath $packageRoot -Recurse -File) {
    $relative = $file.FullName.Substring($packageRoot.Length+1).Replace('\','/')
    if ($relative -notin $ownedPaths) { throw ('Untracked package file: '+$relative) }
}
$defines = @('/DAppVersion=0.2.0',('/DPackageRoot='+$packageRoot))
if ($Validation) { $defines += '/DInstallerTestMode=1' }
if (-not $Validation) {
    $validationEvidence = Get-Content -LiteralPath (Join-Path $projectRoot 'build\installer-test-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $validationEvidence.passed) { throw 'Installer lifecycle tests must pass before release compilation.' }
    foreach ($item in @(@('build\installer-tests\EType-Setup-Validation.exe','installer_sha256'),@('installer\EType.iss','installer_source_sha256'),@('build\standalone\EType\package-manifest.json','package_manifest_sha256'))) {
        if ((Get-FileHash -LiteralPath (Join-Path $projectRoot $item[0]) -Algorithm SHA256).Hash.ToLower() -ne $validationEvidence.($item[1])) { throw ('Changed since installer tests: '+$item[0]) }
    }
}
& $compiler @defines (Join-Path $projectRoot 'installer\EType.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
if (-not $Validation) {
    $output = Join-Path $projectRoot 'releases\EType-0.2.0-Setup.exe'
    $digest = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText($output+'.sha256', $digest+'  '+[IO.Path]::GetFileName($output)+[Environment]::NewLine, [Text.Encoding]::ASCII)
    $releaseFile = Get-Item -LiteralPath $output
    $releaseEvidence = [ordered]@{version='0.2.0';installer_sha256=$digest;bytes=$releaseFile.Length;file_version=$releaseFile.VersionInfo.FileVersion;signature=(Get-AuthenticodeSignature -LiteralPath $output).Status.ToString();installer_checks=$validationEvidence.checks;installer_source_sha256=$validationEvidence.installer_source_sha256;package_manifest_sha256=$validationEvidence.package_manifest_sha256;tested_scope='core, x64/x86 TSF components, sentence preview, native cold start, standalone CPU/GPU service, isolated installer lifecycle';production_global_install_verified=$false;standalone_service=$serviceEvidence;native_cold_start=$autoStart;build_evidence=$manifest.tested_build}
    [IO.File]::WriteAllText(($output -replace '\.exe$','.tests.json'),($releaseEvidence | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
    Write-Output ('Installer: '+$output)
}
