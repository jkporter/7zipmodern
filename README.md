# 7-Zip for Windows 11

The [7-Zip](https://www.7-zip.org/) 26.04 source tree with a new Windows 11 front end: a WinUI 3 app written in
C++ in the style of the rest of 7-Zip. It is packaged as an MSIX and adds commands to the Windows 11 context
menu.

The compression engine is **unchanged**. `7z.dll` is built from upstream's `CPP\7zip\Bundles\Format7zF`, so
archives, formats and checksums behave exactly as they do in classic 7-Zip. What's new replaces the parts users
touch: the File Manager (`7zFM`), the GUI front end (`7zG`) and the Explorer shell extension.

Highlights:

- A tabbed, archive-focused window with tabs in the title bar, Mica, a breadcrumb bar and search across the whole archive
- Commands in the Windows 11 top-level context menu (`IExplorerCommand`), with names such as *Extract to "photos\"*
- Jobs that don't block the window, with taskbar progress, toasts and Efficiency mode
- "Compress and share" through the Windows Share sheet, which also accepts files shared to the app
- Mark of the Web carried onto extracted files, hardened path handling and atomic archive writes
- A Checksums page that uses the engine's own hashers, and a Benchmark page

The full feature-by-feature comparison, architecture and current status are in
[CPP/Windows11/README.md](CPP/Windows11/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `Asm/`, `C/`, `CPP/`, `DOC/` | The 7-Zip source exactly as released (`7z2604-src.tar.xz`) |
| `CPP/Windows11/` | The Windows 11 front end: WinUI app, engine binding layer, shell extension and tests |
| `.github/scripts/` | `Build-Windows11.ps1` (build, test and package) and `Get-7ZipVersion.ps1` |
| `.github/workflows/` | GitHub Actions: build and release, and following new 7-Zip releases |
| `.forgejo/workflows/` | Forgejo Actions copies of the same workflows |

Everything outside `CPP/Windows11` and the CI files is upstream 7-Zip, and the classic build described in
[DOC/readme.txt](DOC/readme.txt) still works.

## Building

You need Visual Studio 2022 or 2026 with the C++ desktop workload. The easiest route is the build script. It
sets up the Visual Studio environment itself, installs any missing NuGet packages, builds `7z.dll`, the app and
the shell extension, runs the tests and packages the result into `out\`:

```powershell
pwsh .github\scripts\Build-Windows11.ps1 -Arch x64
```

Use `-Arch arm64` for ARM. The script writes `out\7z2604-win11-<arch>.msix` (unsigned unless you pass
`-CertificatePath`) and an unpackaged `.zip`.

To build with nmake from a Visual Studio developer command prompt instead, and for the required package
versions, see [Build and run](CPP/Windows11/README.md#build-and-run).

Running the app needs the [Windows App Runtime](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads)
2.5. The context menu, file associations, Share target and notifications only work from the installed MSIX. An
unsigned package installs with Developer Mode on:

```powershell
Add-AppxPackage -AllowUnsigned out\7z2604-win11-x64.msix
```

## Releases

Releases follow [7-zip.org](https://www.7-zip.org/download.html), with the same version and tag (`26.04`) and
matching file names (`7z2604-win11-x64.msix`). A daily workflow watches the download page. When a new 7-Zip comes
out, it imports the new source over `Asm/`, `C/`, `CPP/` and `DOC/` and opens an "Update to 7-Zip XX.YY" pull
request. Merging that pull request builds, tests and publishes the release for x64 and arm64. The same workflows
run on Forgejo from `.forgejo/workflows`.

Signing, runner and revision settings are described in
[Releases](CPP/Windows11/README.md#releases-github-actions) and in the comments at the top of each workflow.

## License

7-Zip is Copyright (C) 1999-2026 Igor Pavlov and is free software under the GNU LGPL, with some code under the
BSD 3-clause license. The unRAR code carries an extra restriction: it may not be used to re-create the RAR
compression algorithm. See [DOC/License.txt](DOC/License.txt), [DOC/unRarLicense.txt](DOC/unRarLicense.txt) and
[DOC/readme.txt](DOC/readme.txt).

This is an unofficial project and is not affiliated with or endorsed by Igor Pavlov or the 7-Zip project.
