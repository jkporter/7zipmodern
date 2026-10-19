PROG = SevenZipTest.exe
MY_CONSOLE = 1

CURRENT_OBJS = \
  $O\SevenZipTest.obj \

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
  $O\IntToString.obj \
  $O\MyString.obj \
  $O\MyVector.obj \
  $O\NewHandler.obj \
  $O\StringConvert.obj \
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

# The tests run against the real engine, built by CPP/7zip/Bundles/Format7zF.
ENGINE_DLL = ..\..\7zip\Bundles\Format7zF\$O\7z.dll

$(PROGPATH): $O\7z.dll

$O\7z.dll: $(ENGINE_DLL)
	copy /Y $(ENGINE_DLL) $@ >nul
