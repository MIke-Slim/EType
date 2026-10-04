param([ValidateSet('Install','Uninstall')][string]$Action, [switch]$Quiet, [string]$InstallDir, [switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
trap {
    if (-not $Quiet -and -not $ValidateOnly) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show(('EType 操作未完成。'+[Environment]::NewLine+$_.Exception.Message),'EType') | Out-Null
    }
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
if (-not [Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { throw 'EType requires Windows x64.' }
if (-not $InstallDir) {
    $InstallDir = $PSScriptRoot
    if ((Split-Path -Leaf $InstallDir) -eq 'scripts') { $InstallDir = Join-Path (Split-Path -Parent $InstallDir) 'build\EType' }
}
$target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $target 'EType.exe') -PathType Leaf)) { throw 'Not an EType package directory.' }
$uninstaller = Join-Path $target 'unins000.exe'
if ($Action -eq 'Uninstall' -and (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
    if ($ValidateOnly) { Write-Output ('Validated managed uninstall: '+$target); exit 0 }
    $arguments = @('/NORESTART')
    if ($Quiet) { $arguments += '/VERYSILENT','/SUPPRESSMSGBOXES' }
    $process = Start-Process -FilePath $uninstaller -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}
if ($Action -eq 'Install') {
    foreach ($file in @('x64\EType.dll','x86\EType.dll','data\dictionary.tsv','data\manifest.json','package-manifest.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $file) -PathType Leaf)) { throw "Package file missing: $file" }
    }
}
$classId = '{DC168F35-18EA-4EC5-B391-C4430C3F3ED9}'
$identityPath = Join-Path $target 'input-service-identity.json'
if (Test-Path -LiteralPath $identityPath) {
    $identity = Get-Content -LiteralPath $identityPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($identity.clsid -notin @($classId,'{B61C1452-3E9A-4616-9EA3-18B4E5862CA4}')) { throw 'Unknown EType input-service identity.' }
    $classId = $identity.clsid
}
$classKey = 'Software\Classes\CLSID\'+$classId+'\InprocServer32'
$hadRegistration = $false
foreach ($entry in @(@([Microsoft.Win32.RegistryView]::Registry64,'x64'),@([Microsoft.Win32.RegistryView]::Registry32,'x86'))) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$entry[0])
    try {
        $key = $base.OpenSubKey($classKey)
        if ($key) {
            try {
                $registered = [string]$key.GetValue('')
                if ($registered -and [IO.Path]::GetFullPath($registered) -ne [IO.Path]::GetFullPath((Join-Path $target ($entry[1]+'\EType.dll')))) { throw 'EType is registered at another path. Uninstall it there first.' }
                if ($registered -and -not (Test-Path -LiteralPath $registered -PathType Leaf)) { throw 'A registered EType DLL is missing. Restore it before uninstalling.' }
                $hadRegistration = $hadRegistration -or [bool]$registered
            } finally { $key.Dispose() }
        }
    } finally { $base.Dispose() }
}
if ($ValidateOnly) { Write-Output ('Validated '+$Action+' directory: '+$target); exit 0 }
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not [Environment]::Is64BitProcess -or -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $hostPath = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (-not [Environment]::Is64BitProcess) { $hostPath = Join-Path $env:WINDIR 'Sysnative\WindowsPowerShell\v1.0\powershell.exe' }
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Action '+$Action+' -InstallDir "'+$target+'"'
    if ($Quiet) { $arguments += ' -Quiet' }
    $elevated = Start-Process -FilePath $hostPath -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}
$completed = @()
try {
    foreach ($entry in @(@('SysWOW64','x86'),@('System32','x64'))) {
        $dll = Join-Path $target ($entry[1]+'\EType.dll')
        if ($Action -eq 'Uninstall' -and -not (Test-Path -LiteralPath $dll -PathType Leaf)) { continue }
        $registrar = Join-Path $env:WINDIR ($entry[0]+'\regsvr32.exe')
        $arguments = @('/s',('"'+$dll+'"'))
        if ($Action -eq 'Uninstall') { $arguments = @('/s','/u',('"'+$dll+'"')) }
        $process = Start-Process -FilePath $registrar -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "EType $Action failed ($($process.ExitCode)): $dll" }
        $completed += ,@($registrar,$dll)
    }
} catch {
    if ($Action -eq 'Install' -and -not $hadRegistration) {
        foreach ($entry in $completed) {
            Start-Process -FilePath $entry[0] -ArgumentList @('/s','/u',('"'+$entry[1]+'"')) -WindowStyle Hidden -Wait | Out-Null
        }
    }
    throw
}
if ($Action -eq 'Uninstall') {
    $shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'EType 英文词汇输入法.lnk'
    if (Test-Path -LiteralPath $shortcut) {
        $shell = New-Object -ComObject WScript.Shell
        if ($shell.CreateShortcut($shortcut).TargetPath -eq (Join-Path $target 'EType.exe')) { Remove-Item -LiteralPath $shortcut -Force }
    }
}
# Legacy registration never recursively deletes or copies software files.
Write-Output ('EType '+$Action+' completed in place: '+$target)
if (-not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    $message = if ($Action -eq 'Install') { 'EType 已注册。请从 Windows 输入法菜单选择 EType。此目录是运行位置，请勿移动或删除；更换位置前请先卸载。' } else { 'EType 已取消注册。软件文件及个人偏好设置保留；需要时可自行删除旧版手动注册目录。' }
    [System.Windows.Forms.MessageBox]::Show($message,'EType') | Out-Null
}
