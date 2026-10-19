<#
.SYNOPSIS
  Builds, tests and packages 7-Zip for Windows 11 (CPP/Windows11) for one architecture.

  Runs the nmake build from a Visual Studio developer environment it sets up itself, so it works
  the same on a GitHub runner and on a developer machine. Output in -OutDir, named like the
  files of the upstream releases (7z2604-x64.exe):
    7z2604-win11-<arch>.msix   MSIX package (signed if -CertificatePath is given)
    7z2604-win11-<arch>.zip    unpackaged build (needs the Windows App Runtime 2.5)

.EXAMPLE
  pwsh .github/scripts/Build-Windows11.ps1 -Arch arm64
#>
param(
    [ValidateSet('x64', 'arm64')] [string] $Arch = 'x64',
    # Third part of the MSIX version (26.4.<Revision>.0); raise it to re-release the same engine version.
    [int] $Revision = 0,
    # MSIX publisher; must equal the subject of the signing certificate.
    [string] $Publisher,
    [string] $CertificatePath,
    [string] $CertificatePassword,
    [string] $OutDir,
    # NuGet global packages folder with the Windows App SDK and C++/WinRT packages.
    # Missing packages are installed with nuget.exe into a folder under -OutDir.
    [string] $NuGetPackages = $(if ($env:NUGET_PACKAGES) { $env:NUGET_PACKAGES } else { Join-Path $env:USERPROFILE '.nuget\packages' }),
    # Package source for that install (default: nuget.org).
    [string] $NuGetSource,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$win11 = Join-Path $root 'CPP\Windows11'
if (-not $OutDir) { $OutDir = Join-Path $root 'out' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

# ---- version ----
$ver = @{}
& (Join-Path $PSScriptRoot 'Get-7ZipVersion.ps1') -Source Repo -Root $root | ForEach-Object {
    $k, $v = $_ -split '=', 2; $ver[$k] = $v
}
$msixVersion = '{0}.{1}.{2}.0' -f $ver.major, $ver.minor, $Revision
$baseName = "7z$($ver.short)-win11-$Arch"
Write-Host "7-Zip $($ver.version) for Windows 11, $Arch, MSIX version $msixVersion"

# ---- Visual Studio developer environment ----
$hostArch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }
$vcvarsArch = if ($hostArch -eq $Arch) { $Arch } else { "${hostArch}_$Arch" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$component = if ($Arch -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$vs = & $vswhere -latest -products * -requires $component -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ tools for $Arch is required." }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'

function Invoke-Dev([string] $dir, [string] $command) {
    $cmdFile = Join-Path $OutDir "build-$Arch.cmd"
    Set-Content -Path $cmdFile -Encoding ascii -Value @(
        '@echo off'
        "call `"$vcvars`" $vcvarsArch >nul || exit /b 1"
        "cd /d `"$dir`" || exit /b 1"
        $command
    )
    Write-Host "> $command" -ForegroundColor Cyan
    & cmd.exe /d /c "`"$cmdFile`""
    if ($LASTEXITCODE -ne 0) { throw "Failed ($LASTEXITCODE): $command" }
}

# ---- NuGet packages (Windows App SDK, C++/WinRT) ----
# Windows11.mak names each package folder as  NAME_DIR = $(NUGET_PACKAGES)\<id>\<version>
$packages = [regex]::Matches((Get-Content -Raw (Join-Path $win11 'Windows11.mak')),
    '(?m)^(\w+_DIR) = \$\(NUGET_PACKAGES\)\\([\w.]+)\\([\d.]+)\s*$') | ForEach-Object {
    [pscustomobject] @{ Macro = $_.Groups[1].Value; Id = $_.Groups[2].Value; Version = $_.Groups[3].Value }
}
$nmakeMacros = @()
$missing = @($packages | Where-Object { -not (Test-Path (Join-Path $NuGetPackages "$($_.Id)\$($_.Version)")) })
if ($missing.Count -eq 0) {
    $nmakeMacros += "NUGET_PACKAGES=`"$NuGetPackages`""
} else {
    Write-Host "Installing NuGet packages: $($missing.Id -join ', ')"
    $nuget = (Get-Command nuget.exe -ErrorAction SilentlyContinue).Source
    if (-not $nuget) {
        $nuget = Join-Path $OutDir 'nuget.exe'
        if (-not (Test-Path $nuget)) {
            Invoke-WebRequest 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile $nuget
        }
    }
    $pkgDir = Join-Path $OutDir 'packages'
    # The metapackage pulls in the exact dependency versions Windows11.mak expects.
    $sdk = $packages | Where-Object Id -eq 'microsoft.windowsappsdk.runtime'
    $cppwinrt = $packages | Where-Object Id -eq 'microsoft.windows.cppwinrt'
    $sourceArgs = if ($NuGetSource) { @('-Source', $NuGetSource) } else { @() }
    & $nuget install Microsoft.WindowsAppSDK -Version $sdk.Version -OutputDirectory $pkgDir -NonInteractive @sourceArgs | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'nuget install Microsoft.WindowsAppSDK failed' }
    & $nuget install Microsoft.Windows.CppWinRT -Version $cppwinrt.Version -OutputDirectory $pkgDir -NonInteractive @sourceArgs | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'nuget install Microsoft.Windows.CppWinRT failed' }
    # nuget install uses <id>.<version> folders
    foreach ($p in $packages) {
        $dir = Join-Path $pkgDir "$($p.Id).$($p.Version)"
        if (-not (Test-Path $dir)) { throw "Package folder not found: $dir" }
        $nmakeMacros += "$($p.Macro)=`"$dir`""
    }
}
$macros = $nmakeMacros -join ' '

# ---- build and test ----
Invoke-Dev $win11 "nmake /nologo PLATFORM=$Arch $macros"

# arm64 hosts run x64 code under emulation; x64 hosts can't run arm64 code
$canRun = ($hostArch -eq $Arch) -or ($hostArch -eq 'arm64')
if ($SkipTests -or -not $canRun) {
    Write-Warning "Tests skipped ($Arch binaries on a $hostArch host)."
} else {
    & (Join-Path $win11 "Test\$Arch\SevenZipTest.exe")
    if ($LASTEXITCODE -ne 0) { throw "$LASTEXITCODE engine test(s) failed" }
}

# ---- MSIX ----
$publisherMacro = if ($Publisher) { "`"MSIX_PUBLISHER=$Publisher`"" } else { '' }
$appDir = Join-Path $win11 'App'
Remove-Item (Join-Path $appDir "$Arch\SevenZip.msix") -ErrorAction SilentlyContinue
Invoke-Dev $appDir "nmake /nologo PLATFORM=$Arch $macros MSIX_VERSION=$msixVersion $publisherMacro msix"
$msix = Join-Path $OutDir "$baseName.msix"
Copy-Item (Join-Path $appDir "$Arch\SevenZip.msix") $msix -Force
if ($CertificatePath) {
    Invoke-Dev $OutDir ("signtool sign /fd SHA256 /f `"$CertificatePath`" /p `"$CertificatePassword`" " +
        "/tr http://timestamp.digicert.com /td SHA256 `"$msix`"")
}

# ---- unpackaged build ----
$stage = Join-Path $OutDir "stage-$Arch"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$stage\Assets" | Out-Null
foreach ($f in 'SevenZip.exe', '7z.dll', 'Microsoft.WindowsAppRuntime.Bootstrap.dll') {
    Copy-Item (Join-Path $appDir "$Arch\$f") $stage
}
Copy-Item (Join-Path $appDir "$Arch\Assets\*") "$stage\Assets"
Copy-Item (Join-Path $root 'DOC\License.txt') $stage
$zip = Join-Path $OutDir "$baseName.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item -Recurse -Force $stage

Get-Item $msix, $zip | Format-Table Name, Length
