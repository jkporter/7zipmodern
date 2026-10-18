<#
.SYNOPSIS
  Builds the native pieces of 7-Zip for Windows 11:
    * 7z.dll   - the unchanged 7-Zip engine (CPP\7zip\Bundles\Format7zF)
    * SevenZip.ShellExtension.dll - the Windows 11 File Explorer context menu (IExplorerCommand)
  Output goes to WinUI\artifacts\native\<arch>\ where the app project picks it up.

.EXAMPLE
  .\Build-Native.ps1                # x64
  .\Build-Native.ps1 -Arch arm64
#>
param(
    [ValidateSet('x64', 'arm64')] [string] $Arch = 'x64',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$root    = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$winui   = Join-Path $root 'WinUI'
$out     = Join-Path $winui "artifacts\native\$Arch"
$engine  = Join-Path $root 'CPP\7zip\Bundles\Format7zF'
$shell   = Join-Path $winui 'src\SevenZip.ShellExtension'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ desktop workload is required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$hostArch = if ($Arch -eq 'arm64') { 'x64_arm64' } else { 'x64' }

New-Item -ItemType Directory -Force $out | Out-Null

function Invoke-VcCommand([string] $dir, [string] $command) {
    $cmd = "call `"$vcvars`" $hostArch >nul && cd /d `"$dir`" && $command"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Native build failed in $dir" }
}

# 1. Engine
$platform = if ($Arch -eq 'arm64') { 'arm64' } else { 'x64' }
if ($Clean) { Remove-Item -Recurse -Force (Join-Path $engine $platform) -ErrorAction SilentlyContinue }
Invoke-VcCommand $engine "nmake /nologo PLATFORM=$platform"
Copy-Item (Join-Path $engine "$platform\7z.dll") $out -Force

# 2. Shell extension (Windows 11 modern context menu)
$obj = Join-Path $shell "obj\$Arch"
New-Item -ItemType Directory -Force $obj | Out-Null
$shellCmd = "cl /nologo /std:c++20 /EHsc /O2 /W4 /permissive- /DUNICODE /D_UNICODE /LD /MT " +
            "/Fo`"$obj\\`" ShellExtension.cpp /link /DEF:ShellExtension.def /OUT:`"$out\SevenZip.ShellExtension.dll`" /IMPLIB:`"$obj\ShellExtension.lib`" " +
            "shlwapi.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib user32.lib"
Invoke-VcCommand $shell $shellCmd

Write-Host "Native binaries in $out" -ForegroundColor Green
Get-ChildItem $out -Filter *.dll | Format-Table Name, Length
