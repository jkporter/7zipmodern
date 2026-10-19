# Windows11.mak : common part of the makefiles in CPP/Windows11

# 7-Zip for Windows 11 requires Windows 10 (0x0A00) API declarations.
# C++/WinRT requires C++20 (coroutines); Core and Test use the same mode.
CFLAGS = $(CFLAGS) -DZ7_WIN32_WINNT_MIN=0x0A00 -std:c++20 -permissive-

!IFDEF USE_WINUI

# The Windows App SDK (WinUI 3) and C++/WinRT are used like the Windows SDK:
# they are referenced from where NuGet installs them, and nothing is copied
# into the source tree. The C++/WinRT projection headers are generated into
# $O\WinRT on the first build. Override NUGET_PACKAGES (or the *_DIR macros)
# if the packages are somewhere else.
#
# To install the packages:
#   nuget install Microsoft.WindowsAppSDK -Version 2.5.1
#   nuget install Microsoft.Windows.CppWinRT -Version 3.0.260520.1

!IFNDEF NUGET_PACKAGES
NUGET_PACKAGES = $(USERPROFILE)\.nuget\packages
!ENDIF

!IFNDEF CPPWINRT_DIR
CPPWINRT_DIR = $(NUGET_PACKAGES)\microsoft.windows.cppwinrt\3.0.260520.1
!ENDIF
!IFNDEF WASDK_RUNTIME_DIR
WASDK_RUNTIME_DIR = $(NUGET_PACKAGES)\microsoft.windowsappsdk.runtime\2.5.1
!ENDIF
!IFNDEF WASDK_FOUNDATION_DIR
WASDK_FOUNDATION_DIR = $(NUGET_PACKAGES)\microsoft.windowsappsdk.foundation\2.3.12
!ENDIF
!IFNDEF WASDK_WINUI_DIR
WASDK_WINUI_DIR = $(NUGET_PACKAGES)\microsoft.windowsappsdk.winui\2.3.9
!ENDIF
!IFNDEF WASDK_IXP_DIR
WASDK_IXP_DIR = $(NUGET_PACKAGES)\microsoft.windowsappsdk.interactiveexperiences\2.1.9
!ENDIF
!IFNDEF WEBVIEW2_DIR
WEBVIEW2_DIR = $(NUGET_PACKAGES)\microsoft.web.webview2\1.0.3719.77
!ENDIF

!IF "$(PLATFORM)" == "arm64"
WASDK_ARCH = arm64
!ELSE
WASDK_ARCH = x64
!ENDIF

WINRT_DIR = $O\WinRT
WINRT_STAMP = $(WINRT_DIR)\cppwinrt.txt

CFLAGS = $(CFLAGS) -bigobj \
  -I$(WINRT_DIR) \
  -I"$(WASDK_RUNTIME_DIR)\include" \
  -I"$(WASDK_FOUNDATION_DIR)\include" \
  -I"$(WASDK_WINUI_DIR)\include" \
  -I"$(WASDK_IXP_DIR)\include" \

LIBS = $(LIBS) windowsapp.lib \
  "$(WASDK_FOUNDATION_DIR)\lib\native\$(WASDK_ARCH)\Microsoft.WindowsAppRuntime.Bootstrap.lib" \

WASDK_BOOTSTRAP_DLL = $(WASDK_FOUNDATION_DIR)\runtimes\win-$(WASDK_ARCH)\native\Microsoft.WindowsAppRuntime.Bootstrap.dll

!ENDIF


OBJS = \
  $O\StdAfx.obj \
  $(CURRENT_OBJS) \
  $(WIN11_OBJS) \
  $(WIN11_CTRL_OBJS) \
  $(CORE_OBJS) \
  $(COMMON_OBJS) \
  $(WIN_OBJS) \
  $(7ZIP_COMMON_OBJS) \
  $(C_OBJS) \
  $O\resource.res \

!include "../../Build.mak"

{.}.cpp{$O}.obj::
	$(COMPLB)
{..}.cpp{$O}.obj::
	$(COMPLB)
{../Control}.cpp{$O}.obj::
	$(COMPLB)
{../Core}.cpp{$O}.obj::
	$(COMPLB)
{../../Common}.cpp{$O}.obj::
	$(COMPLB)
{../../Windows}.cpp{$O}.obj::
	$(COMPLB)
{../../7zip/Common}.cpp{$O}.obj::
	$(COMPLB)
{../../../C}.c{$O}.obj::
	$(CCOMPLB)

!IFDEF USE_WINUI

# The projection must exist before StdAfx.obj is compiled.
# Build.mak builds $O/asm just before the objects.
$O/asm: $(WINRT_STAMP)

$(WINRT_STAMP):
	if not exist "$(WINRT_DIR)" mkdir "$(WINRT_DIR)"
	"$(CPPWINRT_DIR)\bin\cppwinrt.exe" -base -input sdk \
	  -input "$(WASDK_FOUNDATION_DIR)\metadata" \
	  -input "$(WASDK_WINUI_DIR)\metadata" \
	  -input "$(WASDK_IXP_DIR)\metadata\10.0.18362.0" \
	  -input "$(WEBVIEW2_DIR)\lib\Microsoft.Web.WebView2.Core.winmd" \
	  -output $(WINRT_DIR)
	echo C++/WinRT projection > $(WINRT_STAMP)

$(PROGPATH): $O\Microsoft.WindowsAppRuntime.Bootstrap.dll

$O\Microsoft.WindowsAppRuntime.Bootstrap.dll: "$(WASDK_BOOTSTRAP_DLL)"
	copy /Y "$(WASDK_BOOTSTRAP_DLL)" $@ >nul

!ENDIF
