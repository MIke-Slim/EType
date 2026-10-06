param([switch]$Validation,[string]$CompilerPath)
$ErrorActionPreference='Stop'
$onlineRoot=Split-Path -Parent $PSScriptRoot
if(-not $CompilerPath){$CompilerPath=Join-Path $onlineRoot 'tools/inno-setup/ISCC.exe'}
$onlinePackage=Join-Path $onlineRoot 'build/online-release/package'
$onlineManifest=Get-Content -LiteralPath (Join-Path $onlinePackage 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($onlineManifest.version -ne '0.3.0' -or $onlineManifest.variant -ne 'online-release'){throw 'Wrong online release payload'}
foreach($onlineEntry in $onlineManifest.files.PSObject.Properties){
    if((Get-FileHash -LiteralPath (Join-Path $onlinePackage $onlineEntry.Name) -Algorithm SHA256).Hash.ToLower() -ne $onlineEntry.Value){throw ('Changed release payload: '+$onlineEntry.Name)}
}
$onlineDefines=@('/DOnlineEdition=1','/DAppVersion=0.3.0',('/DPackageRoot='+$onlinePackage))
if($Validation){$onlineDefines+='/DInstallerTestMode=1'}
if(-not $Validation){
    $onlineEvidence=Get-Content -LiteralPath (Join-Path $onlineRoot 'build/online-installer-test-results.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if(-not $onlineEvidence.passed){throw 'Online installer lifecycle checks have not passed'}
    if($onlineEvidence.installer_source_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $onlineRoot 'installer/EType.iss')).Hash.ToLower()) {throw 'Installer changed after validation'}
    if($onlineEvidence.package_manifest_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $onlinePackage 'package-manifest.json')).Hash.ToLower()) {throw 'Payload changed after validation'}
}
& $CompilerPath /Qp @onlineDefines (Join-Path $onlineRoot 'installer/EType.iss')
if($LASTEXITCODE -ne 0){throw 'Online installer compilation failed'}
if(-not $Validation){
    $onlineOutput=Join-Path $onlineRoot 'releases/EType-0.3.0-Online-Setup.exe'
    $onlineHash=(Get-FileHash -LiteralPath $onlineOutput -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText($onlineOutput+'.sha256',$onlineHash+'  '+[IO.Path]::GetFileName($onlineOutput)+[Environment]::NewLine,[Text.Encoding]::ASCII)
    Get-Item -LiteralPath $onlineOutput | Select-Object FullName,Length
}
