param([switch]$SkipDictionary)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$toolchain = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tools') -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolchain) { throw 'Missing LLVM MinGW toolchain. Run tools/bootstrap.py first.' }
$package = Join-Path $projectRoot 'build\EType'
New-Item -ItemType Directory -Force $package,(Join-Path $package 'x64'),(Join-Path $package 'x86'),(Join-Path $package 'data'),(Join-Path $package 'licenses') | Out-Null
if (-not $SkipDictionary) {
    & python scripts/build_dictionary.py
    if ($LASTEXITCODE -ne 0) { throw 'Dictionary generation failed.' }
}
$flags = @('-std=c++17','-O2','-Wall','-Wextra','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-static','-Isrc','-mguard=cf')
$libs = @('-lole32','-loleaut32','-luuid','-lshell32','-luser32','-lgdi32','-ladvapi32')
foreach ($architecture in @('x64','x86')) {
    $target = if ($architecture -eq 'x64') { 'x86_64' } else { 'i686' }
    $compiler = Join-Path $toolchain.FullName ('bin\' + $target + '-w64-mingw32-clang++.exe')
    & $compiler @flags -shared src/core.cpp src/platform.cpp src/tsf.cpp src/inputscope_guids.cpp src/etype.def -o (Join-Path $package ($architecture + '\EType.dll')) @libs
    if ($LASTEXITCODE -ne 0) { throw "Input service build failed: $architecture" }
    if ($architecture -eq 'x86') {
        & $compiler @flags -municode src/core.cpp tests/tsf_tests.cpp -o build/tsf-tests-x86.exe @libs
        if ($LASTEXITCODE -ne 0) { throw '32-bit TSF integration test build failed.' }
    }
}
$compiler = Join-Path $toolchain.FullName 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler @flags -municode -mwindows src/core.cpp src/platform.cpp src/app.cpp src/speech_guids.cpp -o (Join-Path $package 'EType.exe') @libs -lcomctl32 -lsapi -lgdiplus
if ($LASTEXITCODE -ne 0) { throw 'Settings application build failed.' }
& $compiler @flags -municode src/core.cpp tests/core_tests.cpp -o build/core-tests.exe -luser32
if ($LASTEXITCODE -ne 0) { throw 'Core test build failed.' }
& $compiler @flags -municode src/core.cpp tests/tsf_tests.cpp -o build/tsf-tests.exe @libs
if ($LASTEXITCODE -ne 0) { throw 'TSF integration test build failed.' }
Copy-Item -LiteralPath data/dictionary.tsv,data/manifest.json -Destination (Join-Path $package 'data') -Force
Copy-Item -LiteralPath third_party/ECDICT-LICENSE -Destination (Join-Path $package 'licenses') -Force
Copy-Item -LiteralPath (Join-Path $toolchain.FullName 'LICENSE.TXT') -Destination (Join-Path $package 'licenses\LLVM-LICENSE.txt') -Force
foreach ($notice in @('COPYING','COPYING.MinGW-w64-runtime.txt','COPYING.MinGW-w64.txt','COPYING.winpthreads.txt')) {
    Copy-Item -LiteralPath (Join-Path $toolchain.FullName ('x86_64-w64-mingw32\share\mingw32\'+$notice)) -Destination (Join-Path $package ('licenses\'+$notice)) -Force
}
Copy-Item -LiteralPath scripts/install.ps1,scripts/uninstall.ps1,docs/使用说明.txt,docs/NOTICE.txt -Destination $package -Force
& .\build\core-tests.exe (Join-Path $package 'data\dictionary.tsv') | Tee-Object -FilePath build/core-test-results.txt
if ($LASTEXITCODE -ne 0) { throw 'Core behavior tests failed.' }
& .\build\tsf-tests.exe (Join-Path $package 'x64\EType.dll') | Tee-Object -FilePath build/tsf-test-results.txt
if ($LASTEXITCODE -ne 0) { throw 'Windows TSF integration tests failed.' }
& .\build\tsf-tests-x86.exe (Join-Path $package 'x86\EType.dll') | Tee-Object -FilePath build/tsf-test-results-x86.txt
if ($LASTEXITCODE -ne 0) { throw '32-bit Windows TSF integration tests failed.' }
$uiReport = Join-Path $projectRoot 'build\ui-selftest.json'
$uiTest = Start-Process -FilePath (Join-Path $package 'EType.exe') -ArgumentList @('--ui-selftest',('"'+$uiReport+'"')) -WindowStyle Hidden -PassThru -Wait
if ($uiTest.ExitCode -ne 0) { throw 'Actual preview text-box tests failed.' }
Get-Content -LiteralPath $uiReport -Encoding UTF8
Write-Output "Built: $package"
