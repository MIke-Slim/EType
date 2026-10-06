# Development compilation only. Does not register a TIP or produce an installer.
param([string]$ToolchainPath,[switch]$InstalledTest,[switch]$OnlineRelease)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
$toolchain=$null
if($ToolchainPath){$toolchain=Get-Item -LiteralPath $ToolchainPath}
elseif(Test-Path -LiteralPath 'tools'){$toolchain=Get-ChildItem tools -Directory -Filter 'llvm-mingw-*-ucrt-x86_64' | Sort-Object Name -Descending | Select-Object -First 1}
if(-not $toolchain){throw 'Missing development toolchain'}
$output=Join-Path $projectRoot 'build/online-lite/native'
if($InstalledTest){$output=Join-Path $projectRoot 'build/online-test/native'}
if($OnlineRelease){$output=Join-Path $projectRoot 'build/online-release/native'}
New-Item -ItemType Directory -Force $output,(Join-Path $output 'x64'),(Join-Path $output 'x86'),(Join-Path $output 'assets') | Out-Null
$flags=@('-std=c++17','-O2','-Wall','-Wextra','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-DETYPE_ONLINE','-static','-Isrc','-mguard=cf')
if($InstalledTest){$flags+='-DETYPE_ONLINE_TEST'}
if($OnlineRelease){$flags+='-DETYPE_ONLINE_RELEASE'}
$libs=@('-lole32','-loleaut32','-luuid','-lshell32','-luser32','-lgdi32','-ladvapi32','-lwinhttp','-lwinmm')
foreach($architecture in @('x64','x86')){
    $target=if($architecture -eq 'x64'){'x86_64'}else{'i686'}
    $compiler=Join-Path $toolchain.FullName ('bin/'+$target+'-w64-mingw32-clang++.exe')
    $resourceCompiler=Join-Path $toolchain.FullName ('bin/'+$target+'-w64-mingw32-windres.exe')
    $resource=Join-Path $output ('brand-'+$architecture+'.o')
    & $resourceCompiler -I assets -i src/brand.rc -O coff -o $resource
    if($LASTEXITCODE -ne 0){throw 'Brand compilation failed'}
    & $compiler @flags -shared src/core.cpp src/platform.cpp src/local_ai.cpp src/tsf.cpp src/inputscope_guids.cpp src/etype.def $resource -o (Join-Path $output ($architecture+'/EType.dll')) @libs
    if($LASTEXITCODE -ne 0){throw "Online TIP compilation failed: $architecture"}
}
$compiler=Join-Path $toolchain.FullName 'bin/x86_64-w64-mingw32-clang++.exe'
& $compiler @flags -municode -mwindows src/core.cpp src/platform.cpp src/local_ai.cpp src/app.cpp src/speech_guids.cpp (Join-Path $output 'brand-x64.o') -o (Join-Path $output 'EType.exe') @libs -lcomctl32 -lsapi -lgdiplus
if($LASTEXITCODE -ne 0){throw 'Online preview compilation failed'}
& $compiler @flags src/core.cpp src/local_ai.cpp tests/online_core_tests.cpp -o (Join-Path $output 'online-tests.exe') @libs
if($LASTEXITCODE -ne 0){throw 'Online test compilation failed'}
# Check the unchanged offline engine contracts against the preserved dictionary.
& $compiler -std=c++17 -O2 -static -Isrc -municode src/core.cpp tests/core_tests.cpp -o (Join-Path $output 'offline-core-tests.exe') -luser32
if($LASTEXITCODE -ne 0){throw 'Baseline regression compilation failed'}
& (Join-Path $output 'offline-core-tests.exe') data/dictionary.tsv
if($LASTEXITCODE -ne 0){throw 'Offline engine regression failed'}
Copy-Item assets/etype-logo.png,assets/etype.ico -Destination (Join-Path $output 'assets') -Force
Write-Output 'Online development build complete; installed 0.2.0 unchanged.'
