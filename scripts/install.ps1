# Installs only EType. Existing input methods and the user's default are preserved.
param([switch]$Quiet)
$ErrorActionPreference = 'Stop'
trap {
    if (-not $Quiet) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show(('EType 安装未完成。' + [Environment]::NewLine + $_.Exception.Message),'EType') | Out-Null
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
$source = $PSScriptRoot
if ((Split-Path -Leaf $source) -eq 'scripts') { $source = Join-Path (Split-Path -Parent $source) 'build\EType' }
$target = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'EType'
foreach ($file in @('EType.exe','x64\EType.dll','x86\EType.dll','data\dictionary.tsv','data\manifest.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $file))) { throw "Package file missing: $file" }
}
New-Item -ItemType Directory -Force $target | Out-Null
# Refuse to overwrite a running installation. Switch away and close apps first.
if ([IO.Path]::GetFullPath($source) -ne [IO.Path]::GetFullPath($target)) {
    Copy-Item -LiteralPath (Join-Path $source 'EType.exe'),(Join-Path $source 'install.ps1'),(Join-Path $source 'uninstall.ps1'),(Join-Path $source '使用说明.txt'),(Join-Path $source 'NOTICE.txt') -Destination $target -Force
    foreach ($folder in @('x64','x86','data','licenses')) {
        New-Item -ItemType Directory -Force (Join-Path $target $folder) | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $source $folder) -File | Copy-Item -Destination (Join-Path $target $folder) -Force
    }
}
function Register-EType([string]$registrar,[string]$dll) {
    $process = Start-Process -FilePath $registrar -ArgumentList @('/s',('"' + $dll + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "EType registration failed ($($process.ExitCode)): $dll" }
}
try {
    Register-EType (Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe') (Join-Path $target 'x86\EType.dll')
    Register-EType (Join-Path $env:WINDIR 'System32\regsvr32.exe') (Join-Path $target 'x64\EType.dll')
} catch {
    foreach ($entry in @(@('SysWOW64','x86'),@('System32','x64'))) {
        Start-Process (Join-Path $env:WINDIR ($entry[0]+'\regsvr32.exe')) -ArgumentList @('/s','/u',('"'+(Join-Path $target ($entry[1]+'\EType.dll'))+'"')) -WindowStyle Hidden -Wait | Out-Null
    }
    throw
}
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'EType 英文词汇输入法.lnk'))
$shortcut.TargetPath = Join-Path $target 'EType.exe'
$shortcut.WorkingDirectory = $target
$shortcut.Save()
Write-Output "Installed EType: $target"
if (-not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show('EType 已安装。请在系统输入法列表中选择 EType；已打开的软件可能需要重新打开。原有输入法和默认设置保持不变。','EType') | Out-Null
}
