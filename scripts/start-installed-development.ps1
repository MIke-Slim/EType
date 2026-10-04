param([string]$InstallDir = 'C:\Program Files\EType')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installedExe = Join-Path $InstallDir 'EType.exe'
if (-not (Test-Path -LiteralPath $installedExe -PathType Leaf)) { throw 'Installed EType executable not found.' }
& (Join-Path $PSScriptRoot 'start-local-ai.ps1')
$deadline = (Get-Date).AddSeconds(70)
$ready = $false
while ((Get-Date) -lt $deadline) {
    try {
        $health = (Invoke-WebRequest -Uri 'http://127.0.0.1:49181/health' -UseBasicParsing -TimeoutSec 2).Content | ConvertFrom-Json
        if ($health.native_protocol -eq 1 -and $health.local_only) { $ready=$true; break }
    } catch { }
    Start-Sleep -Milliseconds 300
}
if (-not $ready) { throw 'Local model not ready. Check build/local-ai/preview.err.log.' }
$activate = Start-Process -FilePath $installedExe -ArgumentList '--activate' -WorkingDirectory $InstallDir -WindowStyle Hidden -Wait -PassThru
if ($activate.ExitCode -ne 0) { throw 'EType system profile activation failed.' }
Start-Process -FilePath $installedExe -WorkingDirectory $InstallDir -WindowStyle Normal
try {
    $runtime = & (Join-Path $PSScriptRoot 'inspect-loaded-input-service.ps1') -InstallDir $InstallDir | ConvertFrom-Json
    if (@($runtime.restart_required).Count) {
        $names = @($runtime.restart_required | Select-Object -ExpandProperty process -Unique) -join ', '
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show(
            ('以下程序仍在使用旧版 EType：'+$names+"。`n`n请保存内容，完全退出这些程序后重新打开，才能显示新版的句子模式入口。仅切换输入法不会更新已加载的旧版。`n`n刚打开的 EType 设置窗口已是新版，可点击「直接试用」体验句子输入。"),
            'EType 更新提示', [Windows.Forms.MessageBoxButtons]::OK, [Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    }
} catch { Write-Warning ('Input-service runtime check failed: '+$_.Exception.Message) }
Write-Output 'Installed development version opened. Choose EType using Win+Space.'
