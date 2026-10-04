param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$pythonPath = Join-Path $projectRoot 'build\local-ai\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    & $Python -m venv build/local-ai/venv
    if ($LASTEXITCODE -ne 0) { throw 'Python environment creation failed.' }
}
& $pythonPath -m pip install --disable-pip-version-check 'kokoro-onnx==0.5.0' 'soundfile==0.14.0' 'onnxruntime==1.24.4'
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
& $pythonPath scripts/local_ai_bootstrap.py
if ($LASTEXITCODE -ne 0) { throw 'Model download failed.' }
$modelRuntime = Join-Path $projectRoot 'build/local-ai/llama'
Expand-Archive -LiteralPath build/local-ai/llama-b11146-bin-win-cuda-12.4-x64.zip -DestinationPath $modelRuntime -Force
Expand-Archive -LiteralPath build/local-ai/cudart-llama-bin-win-cuda-12.4-x64.zip -DestinationPath $modelRuntime -Force
Write-Output 'Ready. Run scripts/start-local-ai.ps1; no installer has been generated.'
