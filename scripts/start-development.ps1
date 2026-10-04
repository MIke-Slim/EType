$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'start-local-ai.ps1')
$deadline = (Get-Date).AddSeconds(70)
$ready = $false
while ((Get-Date) -lt $deadline) {
    try {
        $health = (Invoke-WebRequest -Uri 'http://127.0.0.1:49181/health' -UseBasicParsing -TimeoutSec 2).Content | ConvertFrom-Json
        if ($health.native_protocol -eq 1 -and $health.local_only) { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 300
}
if (-not $ready) { throw 'Local worker not ready. Check build/local-ai/preview.err.log.' }
$previewExe = Join-Path $projectRoot 'build\EType\EType.exe'
if (-not (Test-Path -LiteralPath $previewExe)) { throw 'Run the development build first.' }
Start-Process -FilePath $previewExe -WorkingDirectory (Split-Path -Parent $previewExe) -WindowStyle Normal
Write-Output 'Development preview started. Open the EType window and click the preview button.'
