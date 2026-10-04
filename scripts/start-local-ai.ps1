$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = Join-Path $projectRoot 'build\local-ai\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    throw 'Run scripts/setup-local-ai.ps1 first.'
}
$webRequest = @{ Uri = 'http://127.0.0.1:49181/health'; TimeoutSec = 2; UseBasicParsing = $true }
try { $ready = (Invoke-WebRequest @webRequest).Content | ConvertFrom-Json } catch { $ready = $null }
if ($ready -and $ready.local_only -eq $true -and $ready.model -eq 'Qwen3-4B-Q4_K_M') {
    Write-Output 'Already running: http://127.0.0.1:49181'
    return
}
$previewScript = Join-Path $PSScriptRoot 'local_ai_lab.py'
$previewProcess = Start-Process -FilePath $pythonPath -ArgumentList @('"' + $previewScript + '"') -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $projectRoot 'build\local-ai\preview.out.log') -RedirectStandardError (Join-Path $projectRoot 'build\local-ai\preview.err.log') -PassThru
$previewProcess.Id | Set-Content -LiteralPath (Join-Path $projectRoot 'build\local-ai\preview.pid')
Write-Output 'Starting local preview: http://127.0.0.1:49181'
Write-Output 'Use the close-service button in the preview to release model resources.'
