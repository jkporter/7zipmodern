// ExtractOptions.cpp

#include "StdAfx.h"

#include "ExtractOptions.h"

#include "ArchiveFormat.h"
#include "ArchiveSession.h"
#include "PathSanitizer.h"

namespace NExtractPlanner {

FString GetSmartDestination(const CArchiveSession &session, const FString &parentFolder)
{
  const unsigned topLevel = session.Root.GetNumFolders() + session.Root.Files.Size();
  if (topLevel == 1)
    return parentFolder;
  FString path = parentFolder;
  if (!path.IsEmpty() && !IS_PATH_SEPAR(path.Back()))
    path.Add_PathSepar();
  path += us2fs(GetFolderNameFor(session.DisplayName));
  return path;
}

static bool IsDigit(wchar_t c) { return c >= '0' && c <= '9'; }

static bool EndsWith_NoCase(const UString &s, const char *suffix)
{
  const unsigned len = MyStringLen(suffix);
  return s.Len() >= len && StringsAreEqualNoCase_Ascii(s.RightPtr(len), suffix);
}

UString GetFolderNameFor(const UString &archiveName)
{
  UString name = GetFileNamePart(archiveName);
  for (unsigned i = 0; i < 3; i++)
  {
    const UString ext = GetExtensionPart(name);
    if (ext.IsEmpty())
      break;
    const bool known =
        (ext.Len() == 4 && IsDigit(ext[1]) && IsDigit(ext[2]) && IsDigit(ext[3]))
        || ext.IsEqualTo_Ascii_NoCase(".tar")
        || NFormatRegistry::LooksLikeArchive(name);
    if (!known)
      break;
    name = GetFileNameWithoutExtension(name);
  }
  if (EndsWith_NoCase(name, ".part1") || EndsWith_NoCase(name, ".part01"))
  {
    UString lower = name;
    lower.MakeLower_Ascii();
    const int pos = lower.Find(L".part", lower.Len() >= 7 ? lower.Len() - 7 : 0);
    if (pos >= 0)
      name.DeleteFrom((unsigned)pos);
  }
  if (name.IsEmpty())
    return UString("Extracted");
  return name;
}

}
