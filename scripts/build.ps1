param([switch]$SkipDictionary, [string]$ToolchainPath, [switch]$SentenceTrialProfile)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$toolchain = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tools') -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' | Sort-Object Name -Descending | Select-Object -First 1
if ($ToolchainPath) { $toolchain = Get-Item -LiteralPath $ToolchainPath }
if (-not $toolchain) { throw 'Missing LLVM MinGW toolchain. Run tools/bootstrap.py first.' }
$package = Join-Path $projectRoot 'build\EType'
if (Test-Path -LiteralPath (Join-Path $projectRoot 'build\build-test-evidence.json')) { Remove-Item -LiteralPath (Join-Path $projectRoot 'build\build-test-evidence.json') -Force }
New-Item -ItemType Directory -Force $package,(Join-Path $package 'x64'),(Join-Path $package 'x86'),(Join-Path $package 'data'),(Join-Path $package 'licenses') | Out-Null
if (-not $SkipDictionary) {
    & python scripts/build_dictionary.py
    if ($LASTEXITCODE -ne 0) { throw 'Dictionary generation failed.' }
}
& python tests/dictionary_tests.py
if ($LASTEXITCODE -ne 0) { throw 'Dictionary regression tests failed.' }
$flags = @('-std=c++17','-O2','-Wall','-Wextra','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-static','-Isrc','-mguard=cf')
if ($SentenceTrialProfile) { $flags += '-DETYPE_SENTENCE_TRIAL_PROFILE' }
$serviceIdentity = if ($SentenceTrialProfile) {
    @{clsid='{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}';profile='{D6B9B4A4-8D60-4F7E-A673-301E64C91158}';sentence_trial=$true}
} else { @{clsid='{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}';profile='{8B12D042-C278-4C60-A886-03FC5D145E6C}';sentence_trial=$false} }
[IO.File]::WriteAllText((Join-Path $package 'input-service-identity.json'),($serviceIdentity | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
$libs = @('-lole32','-loleaut32','-luuid','-lshell32','-luser32','-lgdi32','-ladvapi32','-lwinhttp','-lwinmm')
foreach ($architecture in @('x64','x86')) {
    $target = if ($architecture -eq 'x64') { 'x86_64' } else { 'i686' }
    $compiler = Join-Path $toolchain.FullName ('bin\' + $target + '-w64-mingw32-clang++.exe')
    $resourceCompiler = Join-Path $toolchain.FullName ('bin\' + $target + '-w64-mingw32-windres.exe')
    $brandResource = Join-Path $projectRoot ('build\brand-'+$architecture+'.o')
    & $resourceCompiler -I assets -i src/brand.rc -O coff -o $brandResource
    if ($LASTEXITCODE -ne 0) { throw "Logo resource build failed: $architecture" }
    & $compiler @flags -shared src/core.cpp src/platform.cpp src/local_ai.cpp src/tsf.cpp src/inputscope_guids.cpp src/etype.def $brandResource -o (Join-Path $package ($architecture + '\EType.dll')) @libs
    if ($LASTEXITCODE -ne 0) { throw "Input service build failed: $architecture" }
    if ($architecture -eq 'x86') {
        & $compiler @flags -municode src/core.cpp tests/tsf_tests.cpp -o build/tsf-tests-x86.exe @libs
        if ($LASTEXITCODE -ne 0) { throw '32-bit TSF integration test build failed.' }
    }
}
$compiler = Join-Path $toolchain.FullName 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler @flags -municode -mwindows src/core.cpp src/platform.cpp src/local_ai.cpp src/app.cpp src/speech_guids.cpp build/brand-x64.o -o (Join-Path $package 'EType.exe') @libs -lcomctl32 -lsapi -lgdiplus
if ($LASTEXITCODE -ne 0) { throw 'Settings application build failed.' }
& $compiler @flags -municode src/core.cpp tests/core_tests.cpp -o build/core-tests.exe -luser32
if ($LASTEXITCODE -ne 0) { throw 'Core test build failed.' }
& $compiler @flags -municode src/core.cpp tests/tsf_tests.cpp -o build/tsf-tests.exe @libs
if ($LASTEXITCODE -ne 0) { throw 'TSF integration test build failed.' }
Copy-Item -LiteralPath data/dictionary.tsv,data/manifest.json -Destination (Join-Path $package 'data') -Force
Copy-Item -LiteralPath third_party/ECDICT-LICENSE -Destination (Join-Path $package 'licenses') -Force
if (Test-Path -LiteralPath tools/inno-setup/LICENSE.TXT) {
    Copy-Item -LiteralPath tools/inno-setup/LICENSE.TXT -Destination (Join-Path $package 'licenses\InnoSetup-LICENSE.txt') -Force
}
Copy-Item -LiteralPath (Join-Path $toolchain.FullName 'LICENSE.TXT') -Destination (Join-Path $package 'licenses\LLVM-LICENSE.txt') -Force
foreach ($notice in @('COPYING','COPYING.MinGW-w64-runtime.txt','COPYING.MinGW-w64.txt','COPYING.winpthreads.txt')) {
    Copy-Item -LiteralPath (Join-Path $toolchain.FullName ('x86_64-w64-mingw32\share\mingw32\'+$notice)) -Destination (Join-Path $package ('licenses\'+$notice)) -Force
}
Copy-Item -LiteralPath scripts/install.ps1,scripts/uninstall.ps1,scripts/registration.ps1,docs/使用说明.txt,docs/NOTICE.txt -Destination $package -Force
New-Item -ItemType Directory -Force (Join-Path $package 'assets') | Out-Null
Copy-Item -LiteralPath assets/etype-logo.png,assets/etype.ico -Destination (Join-Path $package 'assets') -Force
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
& (Join-Path $PSScriptRoot 'record-build-evidence.ps1')
Write-Output "Built: $package"
