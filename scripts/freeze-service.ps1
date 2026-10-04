$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$python = Join-Path $projectRoot 'build\local-ai\venv\Scripts\python.exe'
& $python -m PyInstaller --noconfirm --clean --onedir --windowed --name ETypeService --distpath build/frozen --workpath build/freeze-work --specpath build --paths scripts --collect-all kokoro_onnx --collect-all espeakng_loader --collect-all phonemizer --collect-all onnxruntime --collect-all soundfile --collect-all language_tags --collect-all csvw --collect-all segments --hidden-import _soundfile_data --hidden-import local_ai_lab --hidden-import local_ai scripts/service_entry.py
if ($LASTEXITCODE -ne 0) { throw 'Standalone local service build failed.' }
