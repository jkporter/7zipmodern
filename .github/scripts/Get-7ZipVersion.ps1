<#
.SYNOPSIS
  Prints 7-Zip version information as name=value lines (the format of $GITHUB_OUTPUT).

  -Source Repo      the version of the source tree (C/7zVersion.h)
  -Source Upstream  the newest release listed on https://www.7-zip.org/download.html

  Output names:
    version  26.04      (the upstream tag and this repo's release tag)
    short    2604       (as in upstream file names: 7z2604-x64.exe)
    major, minor, build
    date     2026-10-05 (Upstream only)
    src_url  URL of 7zXXXX-src.tar.xz (Upstream only)

.EXAMPLE
  pwsh .github/scripts/Get-7ZipVersion.ps1 -Source Upstream >> $env:GITHUB_OUTPUT
#>
param(
    [ValidateSet('Repo', 'Upstream')] [string] $Source = 'Repo',
    [string] $Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $DownloadPage = 'https://www.7-zip.org/download.html',
    # Betas are skipped unless this is set.
    [switch] $IncludeBeta
)

$ErrorActionPreference = 'Stop'

function Write-Version([int] $major, [int] $minor, [int] $build, [hashtable] $extra) {
    $version = '{0}.{1:D2}' -f $major, $minor
    "version=$version"
    "short=$($version -replace '\.', '')"
    "major=$major"
    "minor=$minor"
    "build=$build"
    if ($extra) { foreach ($k in $extra.Keys) { "$k=$($extra[$k])" } }
}

if ($Source -eq 'Repo') {
    $text = Get-Content -Raw (Join-Path $Root 'C/7zVersion.h')
    $get = {
        param($name)
        if ($text -notmatch "#define\s+$name\s+(\d+)") { throw "$name not found in C/7zVersion.h" }
        [int] $Matches[1]
    }
    Write-Version (& $get 'MY_VER_MAJOR') (& $get 'MY_VER_MINOR') (& $get 'MY_VER_BUILD') $null
    return
}

# Upstream: the page lists releases newest first, each as a heading
#   Download 7-Zip 26.04 (2026-10-05) for Windows
# followed by a table whose links include 7z2604-src.tar.xz.
$html = (Invoke-WebRequest -Uri $DownloadPage -UseBasicParsing).Content
$headings = [regex]::Matches($html, 'Download 7-Zip (\d+)\.(\d+)( beta)? \((\d{4}-\d{2}-\d{2})\)')
if ($headings.Count -eq 0) { throw "No release headings found on $DownloadPage" }

foreach ($h in $headings) {
    if ($h.Groups[3].Success -and -not $IncludeBeta) { continue }
    $major = [int] $h.Groups[1].Value
    $minor = [int] $h.Groups[2].Value
    $short = '{0}{1:D2}' -f $major, $minor
    # first source tarball link after this heading
    $rest = $html.Substring($h.Index)
    $link = [regex]::Match($rest, 'href="([^"]*7z' + $short + '-src\.tar\.xz)"')
    if (-not $link.Success) { throw "No 7z$short-src.tar.xz link found for 7-Zip $major.$minor" }
    $srcUrl = ([Uri]::new([Uri] $DownloadPage, $link.Groups[1].Value)).AbsoluteUri
    Write-Version $major $minor 0 ([ordered]@{ date = $h.Groups[4].Value; src_url = $srcUrl })
    return
}
throw "No stable release found on $DownloadPage"
