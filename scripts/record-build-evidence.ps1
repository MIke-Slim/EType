$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$packageRoot = Join-Path $buildRoot 'EType'
$results = [ordered]@{}
foreach ($name in @('core-test-results.txt','tsf-test-results.txt','tsf-test-results-x86.txt')) {
    $result = Get-Content -LiteralPath (Join-Path $buildRoot $name) -Raw
    if ($result -notmatch 'failures=0\b') { throw "Tests did not pass: $name" }
    $results[$name] = $result.Trim()
}
$ui = Get-Content -LiteralPath (Join-Path $buildRoot 'ui-selftest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $ui.passed) { throw 'Actual preview test did not pass.' }
$hashes = [ordered]@{}
foreach ($relative in @('EType.exe','x64\EType.dll','x86\EType.dll','data\dictionary.tsv','data\manifest.json')) {
    $hashes[$relative.Replace('\','/')] = (Get-FileHash -LiteralPath (Join-Path $packageRoot $relative) -Algorithm SHA256).Hash.ToLower()
}
$evidence = [ordered]@{passed=$true;version='0.1.1';files=$hashes;results=$results;ui_selftest=$ui}
[IO.File]::WriteAllText((Join-Path $buildRoot 'build-test-evidence.json'), ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
