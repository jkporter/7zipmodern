# makefile for 7-Zip for Windows 11
#   nmake PLATFORM=x64      (or PLATFORM=arm64)
# builds the engine (7z.dll from CPP/7zip/Bundles/Format7zF) first,
# then the shell extension, the app and the engine tests.

DIRS = \
  Explorer\~ \
  App\~ \
  Test\~ \

all: Engine $(DIRS)

Engine:
	cd ..\7zip\Bundles\Format7zF
	$(MAKE) -nologo $(TARGETS)
	cd ..\..\..\Windows11

$(DIRS):
!include "../7zip/SubBuild.mak"
