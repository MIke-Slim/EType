# Obtain a signed, pinned compiler in portable mode. No file associations or shortcuts.
$ErrorActionPreference = 'Stop'
$toolRoot = $PSScriptRoot
$compilerRoot = Join-Path $toolRoot 'inno-setup'
$download = Join-Path $toolRoot 'innosetup-6.7.3.exe'
$url = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe'
if (-not (Test-Path -LiteralPath $download)) {
    Invoke-WebRequest -Uri $url -OutFile $download
}
$signature = Get-AuthenticodeSignature -LiteralPath $download
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys B.V.') {
    throw 'The official Inno Setup compiler download did not pass signature verification.'
}
if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot 'ISCC.exe'))) {
    $compilerInstall = Start-Process -FilePath $download -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/PORTABLE=1','/NOICONS','/TASKS=""',('/DIR="'+$compilerRoot+'"')) -WindowStyle Hidden -Wait -PassThru
    if ($compilerInstall.ExitCode -ne 0) { throw 'Portable compiler extraction failed.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot 'ISCC.exe'))) { throw 'Compiler is missing after extraction.' }
$record = [ordered]@{version='6.7.3';url=$url;sha256=(Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLower();signature=$signature.Status.ToString();publisher=$signature.SignerCertificate.Subject;portable=$true}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $toolRoot 'installer-toolchain.json') -Encoding UTF8
Write-Output ('Ready: '+(Join-Path $compilerRoot 'ISCC.exe'))
