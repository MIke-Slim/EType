# Install the package at its own path; managed installs use their EXE uninstaller.
param([switch]$Quiet, [string]$InstallDir, [switch]$ValidateOnly)
& (Join-Path $PSScriptRoot 'registration.ps1') -Action Install -Quiet:$Quiet -InstallDir $InstallDir -ValidateOnly:$ValidateOnly
exit $LASTEXITCODE
