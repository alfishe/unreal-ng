param(
    [string]$PackageDirectory = 'build/packages/UnrealNG-Suite',
    [ValidateSet('x64', 'arm64')][string]$Architecture = 'x64',
    [switch]$RequireMSVCRuntime
)

$ErrorActionPreference = 'Stop'

function Assert-PeArchitecture([string]$FilePath, [uint16]$ExpectedMachine) {
    $reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($FilePath))
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid executable: $FilePath" }
        [void]$reader.BaseStream.Seek(0x3C, [System.IO.SeekOrigin]::Begin)
        $header = $reader.ReadUInt32()
        [void]$reader.BaseStream.Seek($header, [System.IO.SeekOrigin]::Begin)
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE header: $FilePath" }
        $machine = $reader.ReadUInt16()
        if ($machine -ne $ExpectedMachine) {
            throw "Wrong architecture in ${FilePath}: $($machine.ToString('X4')), expected $($ExpectedMachine.ToString('X4'))"
        }
    } finally {
        $reader.Dispose()
    }
}

$package = (Resolve-Path $PackageDirectory).Path
if (!(Test-Path "$package/platforms/qwindows.dll")) {
    throw 'Missing Windows Qt platform plugin'
}
if ($RequireMSVCRuntime) {
    foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll') {
        if (!(Test-Path "$package/$dll")) { throw "Missing MSVC runtime: $dll" }
    }
}
$expectedMachine = if ($Architecture -eq 'arm64') { 0xAA64 } else { 0x8664 }
foreach ($app in 'unreal-qt', 'unreal-screen-viewer', 'unreal-videowall', 'unreal-mcp-bridge') {
    Assert-PeArchitecture "$package/$app.exe" $expectedMachine
}
Get-ChildItem $package -Recurse -Filter '*.dll' | ForEach-Object {
    Assert-PeArchitecture $_.FullName $expectedMachine
}
Write-Host "Packaged executables and DLLs have the expected $Architecture architecture"

# Suppress Windows loader/crash dialogs so missing DLLs produce an exit code.
Add-Type @'
using System.Runtime.InteropServices;
public static class PackageErrorMode {
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);
}
'@
$previousErrorMode = [PackageErrorMode]::SetErrorMode(0x8003)
$previousPath = $env:PATH
try {
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES = '1'
    Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
    # A portable package must also launch without Qt/MSYS2 on PATH.
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $process = Start-Process "$package/unreal-qt.exe" -ArgumentList '--help' -PassThru -NoNewWindow
    if (!$process.WaitForExit(30000)) {
        $process.Kill()
        throw 'unreal-qt did not exit within 30 seconds'
    }
    if ($process.ExitCode -ne 0) {
        throw "unreal-qt failed with exit code $($process.ExitCode.ToString('X8'))"
    }
    Write-Host 'Packaged unreal-qt launch passed'
} finally {
    $env:PATH = $previousPath
    [void][PackageErrorMode]::SetErrorMode($previousErrorMode)
}
