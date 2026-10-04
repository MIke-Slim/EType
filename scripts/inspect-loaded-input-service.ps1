param([string]$InstallDir = 'C:\Program Files\EType', [string]$ResultPath = '')
$ErrorActionPreference = 'Stop'
# Read-only PE headers identify an already mapped image, even after its path was updated.
if (-not ('ETypeImageReader' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ETypeImageReader {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes, int size, out IntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    public static byte[] Read(int id, IntPtr address) {
        IntPtr process=OpenProcess(0x410,false,id);
        if(process==IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        try {
            byte[] bytes=new byte[4096]; IntPtr read;
            if(!ReadProcessMemory(process,address,bytes,bytes.Length,out read) || read.ToInt64()!=bytes.Length)
                throw new System.ComponentModel.Win32Exception();
            return bytes;
        } finally { CloseHandle(process); }
    }
}
'@
}
function Get-ImageSignature([byte[]]$Bytes) {
    $pe = [BitConverter]::ToInt32($Bytes,60)
    if ($pe -lt 64 -or $pe+84 -ge $Bytes.Length -or [BitConverter]::ToUInt32($Bytes,$pe) -ne 0x4550) { throw 'Invalid PE header.' }
    return ('{0:X8}:{1:X8}:{2:X8}' -f [BitConverter]::ToUInt32($Bytes,$pe+8),[BitConverter]::ToUInt32($Bytes,$pe+40),[BitConverter]::ToUInt32($Bytes,$pe+80))
}
$target = [IO.Path]::GetFullPath($InstallDir).TrimEnd('\')
$expected = @{}
foreach ($arch in @('x64','x86')) {
    $path = Join-Path $target ($arch+'\EType.dll')
    $expected[$arch] = Get-ImageSignature ([IO.File]::ReadAllBytes($path))
}
$loaded = @()
foreach ($process in Get-Process) {
    try { $modules = @($process.Modules) } catch { continue }
    foreach ($module in $modules) {
        if ($module.ModuleName -ine 'EType.dll') { continue }
        if ($module.FileName -ine (Join-Path $target 'x64\EType.dll') -and $module.FileName -ine (Join-Path $target 'x86\EType.dll')) { continue }
        $entry = [ordered]@{ pid=$process.Id; process=$process.ProcessName; path=$module.FileName; mapped_signature=$null; installed_signature=$null; stale=$null; error=$null }
        try {
            $arch = if ($module.FileName -match '\\x86\\') { 'x86' } else { 'x64' }
            $entry.installed_signature = $expected[$arch]
            $entry.mapped_signature = Get-ImageSignature ([ETypeImageReader]::Read($process.Id,$module.BaseAddress))
            $entry.stale = $entry.mapped_signature -ne $entry.installed_signature
        } catch { $entry.error = $_.Exception.Message }
        $loaded += [pscustomobject]$entry
    }
}
$result = [ordered]@{ installed_signatures=$expected; loaded=$loaded; restart_required=@($loaded | Where-Object { $_.stale -eq $true }) }
$json = $result | ConvertTo-Json -Depth 8
if ($ResultPath) { [IO.File]::WriteAllText($ResultPath,$json,(New-Object Text.UTF8Encoding($false))) }
$json
