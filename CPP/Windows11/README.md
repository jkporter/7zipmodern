# 7-Zip for Windows 11 (WinUI 3, C++)

A rebuild of the 7-Zip desktop experience as a Windows 11 app. The **compression engine is unchanged**:
`7z.dll` is built from `CPP\7zip\Bundles\Format7zF` exactly as upstream ships it. Everything a user
touches has been rebuilt: the File Manager (`7zFM`), the GUI front end (`7zG`) and the Explorer shell
extension (`7-zip.dll`).

```
┌───────────────────── App/  SevenZip.exe (C++/WinRT, WinUI 3, MSIX or unpackaged) ─────────────────────┐
│ MainWindow: tabs in the title bar, Mica Alt      OperationWindow: compact progress for Explorer verbs  │
│ Pages: Home · Archive browser · Create archive · Checksums · Benchmark · Settings (.xaml resources)   │
│ Services: operations/taskbar/toasts · dialogs · Share · Credential Manager + Hello · efficiency mode  │
├───────────────────────────── *.h, Control/  (NWindows11 wrappers for WinUI) ─────────────────────────┤
│ XAML loader · bindable view models · commands · dispatcher · value converters · settings-card styles  │
└──────────────────────────────────────────────┬────────────────────────────────────────────────────────┘
                                               │ linked objects
┌──────────────────────────────────────────────▼─────────────────────┐   ┌────────────────────────────┐
│ Core/ (C++, no WinRT)                                               │   │ Explorer/                  │
│ IArchive.h · ICoder.h · IPassword.h · CPP/Windows · CPP/7zip/Common │   │ SevenZip.ShellExtension.dll│
│ ArchiveSession · ExtractEngine · ArchiveWriter · HashEngine · Bench │   │ IExplorerCommand, Win11    │
└──────────────────────────────────────────────┬─────────────────────┘   │ menu; launches the app     │
                                               │ CreateObject / GetHashers └────────────────────────────┘
                                     ┌─────────▼─────────┐
                                     │ 7z.dll (unchanged)│
                                     └───────────────────┘
```

## Layout

The code follows the conventions of `CPP/Windows` and `CPP/7zip/UI`: a `StdAfx.h` per folder, `ZIP7_INC_*`
include guards, 7-Zip strings and containers (`UString`, `CObjectVector`, `CMyComPtr`), `Z7_COM7F_IMF` COM
classes, HRESULT error handling, and nmake makefiles that list object files per source folder. Nothing from
the 7-Zip tree is re-declared: the engine interfaces, PROPVARIANT, the file streams and the file/string helpers
come from `CPP/7zip`, `CPP/Windows` and `CPP/Common` and are compiled into each project.

| Path | What it is |
|---|---|
| `makefile`, `Windows11.mak` | Top-level build and the shared nmake rules each project includes (like `CPP/7zip/7zip.mak`). `Windows11.mak` also generates the C++/WinRT projection into `$O\WinRT`. |
| `*.h`, `*.cpp` | `NWindows11` wrappers for WinUI, the way `CPP/Windows` wraps Win32: `WinUI.h` (projection includes), `Application.h`, `Xaml.h` (loads `.xaml` resources with XamlReader), `Bindable.h` (`{Binding}` to C++ view models through ICustomPropertyProvider), `Command.h`, `Dispatcher.h`, `HString.h`. |
| `Control/` | `NWindows11::NControl`: value converters and the Settings-style card styles that replace the Windows Community Toolkit. |
| `Core/` | Engine binding layer: loads `7z.dll`; opens, extracts, tests, creates and updates archives; hashes; benchmarks. Plain Win32/7-Zip C++. Operations are synchronous and report progress and prompts through 7-Zip style callback interfaces. |
| `App/` | The WinUI 3 app (`SevenZip.exe`). Markup stays in `.xaml` files that `resource.rc` compiles in as `XAML` resources, much like dialog templates; the code finds controls by `x:Name`. |
| `Explorer/` | Windows 11 context menu handler (`SevenZip.ShellExtension.dll`), structured like `CPP/7zip/UI/Explorer`. |
| `Test/` | `SevenZipTest.exe`: console tests that round-trip real archives through `7z.dll`. |
| `App/Assets/Generate-Assets.ps1` | Renders the icon, all MSIX tile scales and the multi-size `.ico`. |

## Build and run

Requirements: Visual Studio 2022/2026 with the C++ desktop workload, and these NuGet packages in the NuGet
package folder (`%USERPROFILE%\.nuget\packages`, or set `NUGET_PACKAGES`): `Microsoft.WindowsAppSDK` 2.5.1
(with its dependencies, including `Microsoft.Web.WebView2`) and `Microsoft.Windows.CppWinRT` 3.0.260520.1.
They are used in place, like the Windows SDK; nothing is copied into the tree. Running unpackaged needs the
Windows App Runtime 2.5.

From a Visual Studio developer command prompt:

```
cd CPP\Windows11
nmake PLATFORM=x64           rem 7z.dll, shell extension, app and tests (PLATFORM=arm64 for ARM)
Test\x64\SevenZipTest.exe    rem engine tests
App\x64\SevenZip.exe         rem unpackaged app (7z.dll and Assets are copied next to it)

cd App
nmake PLATFORM=x64 msix      rem unsigned App\x64\SevenZip.msix (sign it to install)
```

The full experience (context menu, file associations, Share target, notifications, jump list) needs the
installed MSIX. As in the rest of 7-Zip's nmake build there is no header dependency tracking: after changing a
header, delete the affected `x64\*.obj` (and `x64\*.pch` after changing `StdAfx.h` or `WinUI.h`).

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

Verified on this machine (Windows 11 26200, x64), C++ build:
- `nmake PLATFORM=x64` in `CPP/Windows11` builds everything with zero warnings under 7-Zip's `-Wall -WX` flags.
- 19 engine tests pass: 7z/zip/tar/tar.gz/tar.xz/tar.bz2/wim round-trips, encrypted headers, wrong-password detection, add/rename/delete in place, split volumes, path sanitising, overwrite "keep both", Mark-of-the-Web propagation, known hash vectors, a benchmark round-trip and count wording.
- The unpackaged app launches. Home and Settings render, settings persist (and the C# app's `settings.json` still loads), and single-instance redirection works: a second launch opens its archive as a tab in the first window. The `test`, `extract`, `extract-here`, `extract-smart` (with the overwrite prompt), `compress-7z` and `compress-zip` verbs work end to end.
- The XAML of the archive, extract, create-archive, checksum and benchmark pages loads and binds in a test harness.
- The shell extension enumerates the same commands as the original for archive, folder and file selections in a test harness, launches the app with the same command lines, and releases all its objects.
- `nmake msix` packs an unsigned MSIX.

Not yet verified or not yet done:
- **Installing the MSIX.** This needs Developer Mode or a trusted signing certificate, so the Windows 11 context menu inside real File Explorer, the file associations, the Share target, toasts, the jump list and the Windows Hello password vault have not been exercised end to end.
- Interactive flows that need a person clicking: the password dialog, drag-out to Explorer, edit-and-update, copy/paste, and the Share sheet.
- ARM64 build (`nmake PLATFORM=arm64` is supported by the makefiles but has not been run).
- Localization (all strings are English), column resizing, and an Explorer preview handler for archives.
