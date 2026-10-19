PROG = SevenZip.exe
USE_WINUI = 1

LIBS = $(LIBS) gdi32.lib uuid.lib bcrypt.lib

CURRENT_OBJS = \
  $O\App.obj \
  $O\DialogService.obj \
  $O\LaunchRequest.obj \
  $O\OperationService.obj \
  $O\OperationViewModel.obj \
  $O\PlatformServices.obj \
  $O\SettingsService.obj \
  $O\ShellServices.obj \
  $O\ThemeHelper.obj \
  $O\WindowHost.obj \
  $O\MainWindow.obj \
  $O\OperationWindow.obj \
  $O\HomePage.obj \
  $O\SettingsPage.obj \
  $O\ArchivePage.obj \
  $O\ArchiveViewModel.obj \
  $O\ItemViewModel.obj \
  $O\ExtractOptionsPanel.obj \
  $O\ExtractOptionsModel.obj \
  $O\CompressPage.obj \
  $O\CompressViewModel.obj \
  $O\HashPage.obj \
  $O\HashViewModel.obj \
  $O\BenchmarkPage.obj \

WIN11_OBJS = \
  $O\Bindable.obj \
  $O\Xaml.obj \

WIN11_CTRL_OBJS = \
  $O\Converters.obj \
  $O\SettingsCard.obj \

CORE_OBJS = \
  $O\ArchiveEntry.obj \
  $O\ArchiveFormat.obj \
  $O\ArchiveSession.obj \
  $O\ArchiveWriter.obj \
  $O\BenchmarkRunner.obj \
  $O\CompressionSettings.obj \
  $O\ExtractEngine.obj \
  $O\ExtractOptions.obj \
  $O\HashEngine.obj \
  $O\OpenCallback.obj \
  $O\OperationContext.obj \
  $O\PathSanitizer.obj \
  $O\PropertyFormatter.obj \
  $O\SevenZipEngine.obj \
  $O\UpdateCallback.obj \

COMMON_OBJS = \
  $O\CommandLineParser.obj \
  $O\IntToString.obj \
  $O\MyString.obj \
  $O\MyVector.obj \
  $O\NewHandler.obj \
  $O\StringConvert.obj \
  $O\StringToInt.obj \
  $O\UTFConvert.obj \
  $O\Wildcard.obj \

WIN_OBJS = \
  $O\DLL.obj \
  $O\ErrorMsg.obj \
  $O\FileDir.obj \
  $O\FileFind.obj \
  $O\FileIO.obj \
  $O\FileLink.obj \
  $O\FileName.obj \
  $O\ProcessUtils.obj \
  $O\PropVariant.obj \
  $O\PropVariantConv.obj \
  $O\System.obj \
  $O\TimeUtils.obj \

7ZIP_COMMON_OBJS = \
  $O\FileStreams.obj \
  $O\MultiOutStream.obj \
  $O\StreamObjects.obj \
  $O\StreamUtils.obj \

C_OBJS = \
  $O\Alloc.obj \
  $O\Threads.obj \

!include "../Windows11.mak"


# The app runs against the engine built by CPP/7zip/Bundles/Format7zF.
# The assets are next to the exe, so ms-appx:///Assets/... works unpackaged too.

ENGINE_DLL = ..\..\7zip\Bundles\Format7zF\$O\7z.dll

$(PROGPATH): $O\7z.dll $O\Assets\7zip.ico

$O\7z.dll: $(ENGINE_DLL)
	copy /Y $(ENGINE_DLL) $@ >nul

$O\Assets\7zip.ico: Assets\7zip.ico
	if not exist "$O\Assets" mkdir "$O\Assets"
	copy /Y Assets\*.png "$O\Assets\" >nul
	copy /Y Assets\7zip.ico "$O\Assets\" >nul


# MSIX package (unsigned):  nmake PLATFORM=x64 msix
# Needs the shell extension (..\Explorer) built for the same platform, and
# makepri / makeappx from the Windows SDK on the PATH (VS developer prompt).
# Optional:
#   MSIX_VERSION=26.4.0.0          package version (default: Package.appxmanifest)
#   "MSIX_PUBLISHER=CN=Name, ..."  publisher; must match the signing certificate

SHELL_EXT_DLL = ..\Explorer\$O\SevenZip.ShellExtension.dll
MSIX_DIR = $O\msix
MSIX_FILE = $O\SevenZip.msix

# the package holds native binaries: stamp the architecture into <Identity>
MSIX_STAMP = -replace '<Identity ', '<Identity ProcessorArchitecture=\"$(PLATFORM)\" '
!IFDEF MSIX_VERSION
MSIX_STAMP = $(MSIX_STAMP) -creplace '\bVersion=\"[0-9.]*\"', 'Version=\"$(MSIX_VERSION)\"'
!ENDIF
!IFDEF MSIX_PUBLISHER
MSIX_STAMP = $(MSIX_STAMP) -creplace '\bPublisher=\".*?\"', 'Publisher=\"$(MSIX_PUBLISHER)\"'
!ENDIF

msix: $(MSIX_FILE)

$(MSIX_FILE): $(PROGPATH) $(SHELL_EXT_DLL) Package.appxmanifest
	if exist "$(MSIX_DIR)" rmdir /S /Q "$(MSIX_DIR)"
	mkdir "$(MSIX_DIR)\Assets"
	copy /Y $(PROGPATH) "$(MSIX_DIR)\" >nul
	copy /Y $O\7z.dll "$(MSIX_DIR)\" >nul
	copy /Y $O\Microsoft.WindowsAppRuntime.Bootstrap.dll "$(MSIX_DIR)\" >nul
	copy /Y $(SHELL_EXT_DLL) "$(MSIX_DIR)\" >nul
	copy /Y Assets\*.png "$(MSIX_DIR)\Assets\" >nul
	copy /Y Assets\7zip.ico "$(MSIX_DIR)\Assets\" >nul
	copy /Y ..\..\..\DOC\License.txt "$(MSIX_DIR)\" >nul
	powershell -NoProfile -Command "(Get-Content -Raw 'Package.appxmanifest') $(MSIX_STAMP) | Set-Content -Encoding utf8 '$(MSIX_DIR)\AppxManifest.xml'"
	makepri createconfig /cf "$O\priconfig.xml" /dq en-US /pv 10.0.0 /o
	rem one resources.pri for the flat package: drop the resource-pack split (<packaging>)
	powershell -NoProfile -Command "(Get-Content '$O\priconfig.xml') -notmatch 'packaging>|autoResourcePackage' | Set-Content '$O\priconfig.xml'"
	makepri new /pr "$(MSIX_DIR)" /cf "$O\priconfig.xml" /mn "$(MSIX_DIR)\AppxManifest.xml" /of "$(MSIX_DIR)\resources.pri" /o
	makeappx pack /d "$(MSIX_DIR)" /p $@ /o
