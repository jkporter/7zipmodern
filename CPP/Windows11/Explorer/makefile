PROG = SevenZip.ShellExtension.dll
DEF_FILE = ShellExtension.def

LIBS = $(LIBS) uuid.lib

CURRENT_OBJS = \
  $O\AppLaunch.obj \
  $O\ArchiveNames.obj \
  $O\DllExports.obj \
  $O\ExplorerCommand.obj \
  $O\MenuSettings.obj \

COMMON_OBJS = \
  $O\IntToString.obj \
  $O\MyString.obj \
  $O\MyVector.obj \
  $O\NewHandler.obj \
  $O\StringConvert.obj \

WIN_OBJS = \
  $O\DLL.obj \
  $O\FileDir.obj \
  $O\FileFind.obj \
  $O\FileIO.obj \
  $O\FileName.obj \
  $O\ProcessUtils.obj \
  $O\TimeUtils.obj \

!include "../Windows11.mak"
