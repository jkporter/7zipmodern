// ArchiveNames.cpp

#include "StdAfx.h"

#include "ArchiveNames.h"

#include "../../Windows/FileFind.h"

using namespace NWindows;
using namespace NFile;

// .r00 / .z01 style multi-part pieces are opened through their first part, so they are not listed.
static const char * const k_ArcExts[] =
{
    "7z", "zip", "rar", "tar", "gz", "tgz", "bz2", "tbz", "tbz2", "xz", "txz", "zst", "tzst"
  , "lzma", "lz", "z", "taz", "cab", "arj", "lzh", "lha", "cpio", "rpm", "deb", "iso", "img"
  , "wim", "swm", "esd", "vhd", "vhdx", "vmdk", "vdi", "qcow", "qcow2", "dmg", "hfs", "xar"
  , "pkg", "squashfs", "cramfs", "apfs", "ext", "ext4", "fat", "ntfs", "mbr", "gpt", "chm"
  , "msi", "msix", "appx", "nupkg", "jar", "apk", "001"
};

/*
GetNamePos() and GetExtPos() follow the rules of the shell's PathFindFileName() and
PathFindExtension(), so the computed titles are what Explorer users saw before:
  - a separator counts only if a name follows it ("C:\" and "dir\" stay whole);
  - a space after the last dot means there is no extension ("notes.txt old").
*/

static unsigned GetNamePos(const wchar_t *s)
{
  unsigned pos = 0;
  for (unsigned i = 0; s[i] != 0; i++)
  {
    const wchar_t c = s[i];
    if (c == '\\' || c == '/' || c == ':')
    {
      const wchar_t next = s[i + 1];
      if (next != 0 && next != '\\' && next != '/')
        pos = i + 1;
    }
  }
  return pos;
}

// returns the position of the dot, or the length of (s), if there is no extension
static unsigned GetExtPos(const wchar_t *s)
{
  int dotPos = -1;
  unsigned i;
  for (i = 0; s[i] != 0; i++)
  {
    const wchar_t c = s[i];
    if (c == '\\' || c == ' ')
      dotPos = -1;
    else if (c == '.')
      dotPos = (int)i;
  }
  return dotPos < 0 ? i : (unsigned)dotPos;
}

static bool IsArchiveExt(const wchar_t *ext)
{
  if (*ext != '.')
    return false;
  ext++;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_ArcExts); i++)
    if (StringsAreEqualNoCase_Ascii(ext, k_ArcExts[i]))
      return true;
  return false;
}


bool IsArchivePath(const UString &path)
{
  return IsArchiveExt(path.Ptr(GetExtPos(path)));
}


UString GetExtractFolderName(const UString &arcPath)
{
  UString name (arcPath.Ptr(GetNamePos(arcPath)));
  for (unsigned i = 0; i < 2; i++)
  {
    const unsigned extPos = GetExtPos(name);
    if (!IsArchiveExt(name.Ptr(extPos)))
      break;
    name.DeleteFrom(extPos);
  }
  return name;
}


UString GetAddArchiveName(const UStringVector &paths)
{
  if (paths.IsEmpty())
    return UString("Archive");

  const UString &path = paths[0];

  if (paths.Size() == 1)
  {
    UString name (path.Ptr(GetNamePos(path)));
    const DWORD attrib = NFind::GetFileAttrib(us2fs(path));
    if (attrib != INVALID_FILE_ATTRIBUTES && NFind::NAttributes::IsDir(attrib))
      return name;
    name.DeleteFrom(GetExtPos(name));
    return name;
  }

  // several items: the name of their folder
  UString dir (path);
  const int slashPos = dir.ReverseFind(L'\\');
  if (slashPos >= 0)
    dir.DeleteFrom(slashPos);
  const UString parent (dir.Ptr(GetNamePos(dir)));
  if (parent.IsEmpty() || parent.Back() == ':')
    return UString("Archive");
  return parent;
}


UString ShortenMenuName(const UString &s)
{
  const unsigned kMaxLen = 40;
  const unsigned kPartLen = kMaxLen / 2 - 1;
  if (s.Len() <= kMaxLen)
    return s;
  UString res (s.Left(kPartLen));
  res += L"\x2026"; // horizontal ellipsis
  res += s.RightPtr(kPartLen);
  return res;
}
