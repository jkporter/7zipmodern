# 7-Zip for Windows 11 (WinUI 3)

A rebuild of the 7-Zip desktop experience as a Windows 11 app. The **compression engine is unchanged**:
`7z.dll` is built from `..\CPP\7zip\Bundles\Format7zF` exactly as upstream ships it. Everything a user
touches has been rebuilt: the File Manager (`7zFM`), the GUI front end (`7zG`) and the Explorer shell
extension (`7-zip.dll`).

```
┌──────────────────────────── SevenZip.App (C# / .NET 10 / WinUI 3, MSIX) ───────────────────────────┐
│ MainWindow: tabs in the title bar, Mica Alt      OperationWindow: compact progress for Explorer verbs│
│ Pages: Home · Archive browser · Create archive · Checksums · Benchmark · Settings                  │
│ Services: operations/taskbar/toasts · dialogs · Share · Credential Manager + Hello · efficiency mode │
└────────────────────────────────────────────┬────────────────────────────────────────────────────────┘
                                             │ project reference
┌────────────────────────────────────────────▼──────────────────────┐   ┌────────────────────────────┐
│ SevenZip.Core (C#, AOT/trim-safe)                                  │   │ SevenZip.ShellExtension.dll│
│ Source-generated COM bindings → IInArchive / IOutArchive / IHashers│   │ C++ IExplorerCommand       │
│ ArchiveSession · ExtractEngine · ArchiveWriter · HashEngine · Bench│   │ Win11 top-level menu;      │
└────────────────────────────────────────────┬──────────────────────┘   │ launches the app by AUMID  │
                                             │ CreateObject / GetHashers └────────────────────────────┘
                                   ┌─────────▼─────────┐
                                   │ 7z.dll (unchanged)│
                                   └───────────────────┘
```

## Layout

| Path | What it is |
|---|---|
| `src/SevenZip.Core` | Engine binding layer. `Interop/` holds the `[GeneratedComInterface]` declarations, which mirror `IStream.h`, `IArchive.h`, `IPassword.h` and `ICoder.h` vtable for vtable, plus PROPVARIANT and stream adapters. The rest is the archive API. |
| `src/SevenZip.App` | WinUI 3 app (MVVM with CommunityToolkit.Mvvm, settings cards from the Windows Community Toolkit). |
| `src/SevenZip.ShellExtension` | Windows 11 context menu handler (about 500 lines of C++ with no dependencies). |
| `tests/SevenZip.Core.Tests` | xUnit v3 tests that round-trip real archives through `7z.dll`. |
| `build/Build-Native.ps1` | Builds `7z.dll` (nmake) and the shell extension (cl) into `artifacts/native/<arch>/`. |
| `build/Generate-Assets.ps1` | Renders the icon, all MSIX tile scales and the multi-size `.ico`. |

## Build and run

Requirements: Visual Studio 2022/2026 with the C++ desktop workload and .NET 10 SDK (pinned by `global.json`).

```powershell
.\build\Build-Native.ps1                      # 7z.dll + SevenZip.ShellExtension.dll (x64; -Arch arm64 for ARM)
dotnet test tests\SevenZip.Core.Tests -p:Platform=x64

# Quick iteration, no Developer Mode needed (self-contained, unpackaged):
dotnet build src\SevenZip.App -p:Platform=x64 -p:Unpackaged=true
src\SevenZip.App\bin\x64\Debug\net10.0-windows10.0.26100.0\win-x64\SevenZip.exe

# Full experience (context menu, file associations, Share target, jump list) needs the MSIX:
dotnet build src\SevenZip.App -p:Platform=x64      # then deploy from Visual Studio, or sign + Add-AppxPackage
```

Command line (also how the Explorer extension talks to the app):
`SevenZip [open|extract|extract-here|extract-smart|test|compress|compress-7z|compress-zip|compress-share|hash|benchmark|settings] [--format NAME] [--algorithm NAME] paths… | @listfile`.
In the MSIX build the same commands are available as `7zw`.

## What changed, feature by feature

| Classic 7-Zip | Windows 11 app |
|---|---|
| Two-pane file manager (`7zFM`) that also browses the disk | Archive-focused **tabbed** window. File Explorer is the file manager, and 7-Zip complements it. Tabs live in the title bar, the window uses Mica Alt, and a breadcrumb bar handles navigation. |
| ListView report view | Explorer-style list with real shell icons and type names. Columns hide themselves as the window narrows. A details pane shows every handler property. |
| Filter the current folder | **Search the whole archive**, with each result's location. |
| Win32 dialogs (`CompressDialog`, `ExtractDialog`, `PasswordDialog`, `OverwriteDialog`, `MessagesDialog`) | A settings-card "Create archive" page and ContentDialogs. The Replace/Skip/Keep both prompt matches the Windows 11 copy dialog. Results show in InfoBars. |
| Modal progress dialog, one job at a time | Non-modal jobs listed in an **Activity** flyout, with pause, cancel and **taskbar progress**. A **toast** with "Show in folder" appears when the app isn't in front. |
| "Background" priority button | **Efficiency mode** (EcoQoS plus idle priority, the green leaf in Task Manager). |
| `IContextMenu` shell extension under "Show more options" | **`IExplorerCommand` in the Windows 11 top-level menu**, registered by the MSIX manifest with no registry writes. Archive commands appear only for archives, with computed names such as *Extract to "photos\"*. |
| "Compress and email" (Simple MAPI) | **"Compress and share"** through the Windows **Share** sheet. The app is also a **Share target** that compresses files shared to it. |
| File associations written to HKCR by the System options page | MSIX `FileTypeAssociation`s, plus a link to *Settings > Default apps*. |
| Settings in `HKCU\Software\7-Zip` | JSON in the package's LocalState folder, which Windows removes on uninstall. |
| Open file from archive extracts to `%TEMP%\7zO…` | Same idea, kept in a per-session temp folder that is cleaned on exit or after a crash. File changes are detected and the app offers **Update archive** in an InfoBar. |
| Drag & drop through `IDataObject` | WinUI drag and drop. Dragging out uses delayed rendering, so files are extracted only when dropped. Ctrl+C / Ctrl+V put real files on the clipboard. |
| "Delete files after compression" | Moves the originals to the **Recycle Bin** with `IFileOperation`, so the delete can be undone. |
| CRC/SHA submenu with a text result window | A **Checksums** page using the engine's own hashers (CRC32/64, SHA-1/256/384/512, SHA3-256, BLAKE2sp, XXH64, MD5), so results match classic 7-Zip. Paste a published hash to verify, or save a `sha256sum`-style file. |
| Benchmark dialog | A Benchmark page that round-trips LZMA2 through the real engine and verifies the output. |
| Win32 file and folder pickers | Windows App SDK pickers, which work unpackaged and elevated. |

### Windows features the classic app never used
- **Mark of the Web propagation is on by default.** Files extracted from a downloaded archive inherit its `Zone.Identifier`, so SmartScreen and Office Protected View keep working. A setting limits it to Office files or turns it off.
- **Windows Hello–gated saved passwords** (optional), stored in Credential Manager.
- **Single-instance tabs.** Opening archives from Explorer adds tabs to the running window through AppLifecycle redirection. Quick verbs such as Extract here run in their own small window, as `7zG` did.
- **Jump list** with tasks (Create archive, Verify checksums) and recent archives, fed by `SHAddToRecentDocs`.
- **Smart extraction folder.** A folder is created only when the archive doesn't already have exactly one top-level item, which avoids both `foo\foo\…` and loose files spilling out.
- **Tarballs open in one step.** `.tar.gz`, `.tar.xz` and similar open straight into the TAR. "Create archive" can also produce tar.gz, tar.xz and tar.bz2 directly.
- **Atomic writes.** New and updated archives are written to a temporary name and swapped in only after success.
- **Hardened extraction.** Path traversal, drive/UNC prefixes, reserved device names and trailing dots are neutralised. Symbolic links are created last and only when they resolve inside the destination.
- Long-path aware, per-monitor DPI v2, segment heap, UTF-8 code page, full keyboard and accessibility support from WinUI.

### Dropped, and why
- **The general-purpose file manager** (copy/move between disk folders, two panels, drive list, network browser, hard/symbolic link dialog, alternate-streams browser): File Explorer does this, and duplicating it is what made 7zFM feel dated.
- **Large memory pages** option: it needs `SeLockMemoryPrivilege`, which is rarely granted. The engine export remains available.
- **Language `.txt` files:** to be replaced by `.resw`/MRT resources (not yet done, see below).
- **Creating SFX (self-extracting) archives:** the unsigned-EXE delivery model clashes with SmartScreen and Smart App Control. Opening SFX archives still works.
- **Split/Combine file tools:** "Split into volumes" covers creation, and opening `.001` opens the whole set.

## Status

Verified on this machine (Windows 11 26200, x64):
- 18 engine tests pass: 7z/zip/tar/tar.gz/tar.xz/tar.bz2/wim round-trips, encrypted headers, wrong-password detection, add/rename/delete in place, split volumes, path sanitising, overwrite "keep both", Mark-of-the-Web propagation, known hash vectors and a benchmark round-trip.
- The unpackaged app launches. Home, archive browser, Create archive, Checksums and Settings render correctly. Single-instance redirection works. The `test`, `extract-smart` and `compress-7z` verbs work end to end. SHA-256 output matches `Get-FileHash`.
- The shell extension loads and enumerates the right commands for archive, folder and file selections in a test harness, and releases all its objects.
- The MSIX manifest validates with `makeappx pack`.

Not yet verified or not yet done:
- **Installing the MSIX.** This needs Developer Mode or a trusted signing certificate, so the Windows 11 context menu inside real File Explorer, the file associations, the Share target, toasts and the jump list have not been exercised end to end.
- Interactive flows that need a person clicking: the extract and password dialogs, drag-out to Explorer, edit-and-update, and the Share sheet.
- ARM64 build (the scripts support `-Arch arm64` / `-p:Platform=ARM64`, but neither has been run).
- Localization (all strings are English), column resizing, and an Explorer preview handler for archives.
