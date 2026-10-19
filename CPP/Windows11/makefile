# makefile for 7-Zip for Windows 11
#   nmake PLATFORM=x64      (or PLATFORM=arm64)
# builds the engine (7z.dll from CPP/7zip/Bundles/Format7zF) first,
# then the shell extension, the app and the engine tests.

DIRS = \
  Explorer\~ \
  App\~ \
  Test\~ \

# The ARM64 assembler code of 7z.dll (Asm/arm64/*.S) is built with clang-cl.
# Without it, use 7-Zip's NO_ASM_GNU option (C code for the LZMA decoder).
!IF "$(PLATFORM)" == "arm64" && !defined(NO_ASM_GNU)
!IF [clang-cl --version >nul 2>&1] != 0
!MESSAGE clang-cl was not found: 7z.dll is built with NO_ASM_GNU=1 (C LZMA decoder).
!MESSAGE Install the "C++ Clang tools for Windows" component of Visual Studio to use the ARM64 assembler.
ENGINE_FLAGS = NO_ASM_GNU=1
!ENDIF
!ENDIF

all: Engine $(DIRS)

Engine:
	cd ..\7zip\Bundles\Format7zF
	$(MAKE) -nologo $(TARGETS) $(ENGINE_FLAGS)
	cd ..\..\..\Windows11

$(DIRS):
!include "../7zip/SubBuild.mak"
