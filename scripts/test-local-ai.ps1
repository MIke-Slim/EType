param([string]$ToolchainPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$toolchain = if ($ToolchainPath) { Get-Item -LiteralPath $ToolchainPath } else {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tools') -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' | Sort-Object Name -Descending | Select-Object -First 1
}
if (-not $toolchain) { throw 'Specify -ToolchainPath for the LLVM MinGW toolchain.' }
$health = (Invoke-WebRequest -Uri 'http://127.0.0.1:49181/health' -TimeoutSec 5 -UseBasicParsing).Content | ConvertFrom-Json
if ($health.local_only -ne $true -or $health.native_protocol -ne 1) { throw 'Start the EType local AI service first.' }
$compiler = Join-Path $toolchain.FullName 'bin\x86_64-w64-mingw32-clang++.exe'
$flags = @('-std=c++17','-O2','-Wall','-Wextra','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-static','-Isrc','-mguard=cf')
& $compiler @flags src/core.cpp src/local_ai.cpp tests/local_bridge_tests.cpp -o build/local-bridge-tests.exe -lwinhttp -lwinmm -luser32
if ($LASTEXITCODE -ne 0) { throw 'Native bridge test compilation failed.' }
& $compiler @flags -municode src/core.cpp src/platform.cpp tests/speech_state_tests.cpp -o build/speech-state-tests.exe -lole32 -loleaut32 -luuid -lshell32 -luser32 -lgdi32 -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Speech state test compilation failed.' }
& build/local-ai/venv/Scripts/python.exe tests/local_ai_tests.py
if ($LASTEXITCODE -ne 0) { throw 'Python adapter tests failed.' }
& build/local-bridge-tests.exe | Tee-Object -FilePath build/local-bridge-test-results.txt
if ($LASTEXITCODE -ne 0) { throw 'Native bridge tests failed.' }
& build/speech-state-tests.exe build/EType/EType.exe | Tee-Object -FilePath build/speech-state-test-results.txt
if ($LASTEXITCODE -ne 0) { throw 'Speech state tests failed.' }
