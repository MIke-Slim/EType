$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$package = Join-Path $projectRoot 'build\standalone\EType'
$url = 'http://127.0.0.1:49181'
$shutdown = @{Uri=($url+'/shutdown');Method='POST';Headers=@{'X-EType-Lab'='1'};ContentType='application/json';Body='{}';UseBasicParsing=$true;TimeoutSec=5}
$wasDevelopment = $false
$ownedWorker = $false
$passed = $false
try {
    try { $health = (Invoke-WebRequest ($url+'/health') -UseBasicParsing -TimeoutSec 2).Content | ConvertFrom-Json } catch { $health=$null }
    if ($health) {
        $developmentScript = Join-Path $projectRoot 'scripts\local_ai_lab.py'
        $workers = @(Get-CimInstance Win32_Process -Filter "Name='python.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($developmentScript) })
        if (-not $health.local_only -or $health.model -ne 'Qwen3-4B-Q4_K_M' -or -not $workers.Count) { throw 'Another service owns the production port; do not stop it.' }
        $wasDevelopment = $true
        Invoke-WebRequest @shutdown | Out-Null
        foreach ($worker in $workers) { Wait-Process -Id $worker.ProcessId -Timeout 15 -ErrorAction SilentlyContinue }
    }
    $audio = Join-Path $projectRoot 'build\standalone-autostart.wav'
    $ownedWorker = $true
    $app = Start-Process -FilePath (Join-Path $package 'EType.exe') -ArgumentList '--natural-speech-test',('"'+$audio+'"') -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    if (-not $app.WaitForExit(90000)) { throw 'Native cold start timed out.' }
    if ($app.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $audio)) { throw 'Native cold start did not produce speech.' }
    $health = (Invoke-WebRequest ($url+'/health') -UseBasicParsing -TimeoutSec 3).Content | ConvertFrom-Json
    if ($health.runtime -ne 'standalone') { throw 'Native request did not start the bundled service.' }
    $report = Join-Path $projectRoot 'build\standalone-native-sentence.json'
    foreach ($oldReport in @($report,($report+'.sentences.json'))) {
        if (Test-Path -LiteralPath $oldReport) { Remove-Item -LiteralPath $oldReport }
    }
    $ui = Start-Process -FilePath (Join-Path $package 'EType.exe') -ArgumentList '--sentence-selftest',('"'+$report+'"') -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
    if (-not $ui.WaitForExit(180000)) { $ui.Kill(); throw 'Standalone native sentence UI test timed out.' }
    $sentence = Get-Content -LiteralPath ($report+'.sentences.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($ui.ExitCode -ne 0 -or -not $sentence.passed) { throw 'Standalone native sentence UI failed.' }
    $passed=$true
} finally {
    if ($ownedWorker) {
        try {
            Invoke-WebRequest @shutdown | Out-Null
            $serviceExe = Join-Path $package 'runtime\ETypeService.exe'
            $workers = @(Get-CimInstance Win32_Process -Filter "Name='ETypeService.exe'" | Where-Object { $_.ExecutablePath -eq $serviceExe })
            foreach ($worker in $workers) { Wait-Process -Id $worker.ProcessId -Timeout 15 -ErrorAction SilentlyContinue }
        } catch { Write-Warning ('Test worker shutdown: '+$_.Exception.Message) }
    }
    if ($wasDevelopment) { & (Join-Path $projectRoot 'scripts\start-local-ai.ps1') }
    $result = [ordered]@{passed=$passed;native_auto_start_verified=$passed;native_sentence_ui_verified=$passed;development_service_restore_requested=$wasDevelopment;package_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $package 'package-manifest.json')).Hash.ToLower()}
    [IO.File]::WriteAllText((Join-Path $projectRoot 'build\standalone-autostart-results.json'),($result | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
}
Write-Output 'Standalone native cold start and sentence UI: PASS'
