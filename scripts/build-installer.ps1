param([switch]$Validation, [string]$CompilerPath)
& (Join-Path $PSScriptRoot 'build-standalone-installer.ps1') -Validation:$Validation -CompilerPath $CompilerPath