param([switch]$Quiet)
$ErrorActionPreference = 'Stop'
trap {
    if (-not $Quiet) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show(('EType 卸载未完成。' + [Environment]::NewLine + $_.Exception.Message),'EType') | Out-Null
    }
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
    if ($Quiet) { $arguments += ' -Quiet' }
    $elevated = Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}
$programRoot = [IO.Path]::GetFullPath([Environment]::GetFolderPath('ProgramFiles'))
$target = [IO.Path]::GetFullPath((Join-Path $programRoot 'EType'))
# Validate the exact target before any recursive deletion.
if ($target -ne (Join-Path $programRoot 'EType') -or (Split-Path -Parent $target) -ne $programRoot) { throw 'Unexpected uninstall target.' }
foreach ($entry in @(@('System32','x64'),@('SysWOW64','x86'))) {
    $dll = Join-Path $target ($entry[1] + '\EType.dll')
    if (Test-Path -LiteralPath $dll) {
        $process = Start-Process (Join-Path $env:WINDIR ($entry[0]+'\regsvr32.exe')) -ArgumentList @('/s','/u',('"'+$dll+'"')) -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "EType unregister failed: $dll" }
    }
}
$shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'EType 英文词汇输入法.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
try { if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force } }
catch { Write-Warning 'EType is unregistered, but some files are still used by open apps. Close those apps and rerun uninstall to remove the remaining files.' }
Write-Output 'EType unregistered. User preferences are retained.'
if (-not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show('EType 已取消注册。若仍有文件被打开的软件占用，请关闭这些软件后再次卸载。个人偏好设置保留。','EType') | Out-Null
}
