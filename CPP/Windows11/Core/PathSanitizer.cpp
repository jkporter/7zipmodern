// PathSanitizer.cpp

#include "StdAfx.h"

#include "PathSanitizer.h"

#include "../../Common/Wildcard.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"

using namespace NWindows;
using namespace NFile;

namespace NPathSanitizer {

static const wchar_t * const kReserved[] =
{
  L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$",
  L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
  L"COM\x00B9", L"COM\x00B2", L"COM\x00B3",
  L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
  L"LPT\x00B9", L"LPT\x00B2", L"LPT\x00B3"
};

static bool IsReserved(const UString &stem)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kReserved); i++)
    if (stem.IsEqualTo_NoCase(kReserved[i]))
      return true;
  return false;
}

static bool IsAsciiLetter(wchar_t c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void GetSafeComponents(const UString &archivePath, UStringVector &components, bool allowAltStream)
{
  components.Clear();
  UString path = archivePath;
  path.Replace(L'/', L'\\');
  UStringVector parts;
  {
    UStringVector all;
    SplitPathToParts(path, all);
    FOR_VECTOR (i, all)
      if (!all[i].IsEmpty())
        parts.Add(all[i]);
  }
  FOR_VECTOR (i, parts)
  {
    const UString &part = parts[i];
    if (part.IsEqualTo(".") || part.IsEqualTo(".."))
      continue;
    // Drop a leading drive designator ("C:") or "\\?\" style prefixes.
    if (components.IsEmpty() && part.Len() == 2 && part[1] == ':' && IsAsciiLetter(part[0]))
      continue;
    if (components.IsEmpty() && part.IsEqualTo("?"))
      continue;
    const bool last = (i == parts.Size() - 1);
    const UString clean = CleanComponent(part, allowAltStream && last);
    if (!clean.IsEmpty())
      components.Add(clean);
  }
}

UString CleanComponent(const UString &nameSrc, bool allowAltStream)
{
  UString name = nameSrc;
  UString stream;
  if (allowAltStream)
  {
    const int colon = name.Find(L':');
    if (colon > 0)
    {
      stream = CleanComponent(name.Ptr((unsigned)colon + 1));
      name.DeleteFrom((unsigned)colon);
    }
  }

  UString result;
  for (unsigned i = 0; i < name.Len(); i++)
  {
    const wchar_t c = name[i];
    if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*')
      result += L'_';
    else
      result += c;
  }

  // Windows silently strips trailing dots and spaces which could merge two items.
  unsigned end = result.Len();
  while (end > 0 && (result[end - 1] == '.' || result[end - 1] == ' '))
    end--;
  if (end < result.Len())
  {
    if (end == 0 && (result.IsEqualTo(".") || result.IsEqualTo("..")))
      return UString();
    for (unsigned i = end; i < result.Len(); i++)
      result.ReplaceOneCharAtPos(i, '_');
  }

  UString stem = result;
  {
    const int dot = stem.Find(L'.');
    if (dot >= 0)
      stem.DeleteFrom((unsigned)dot);
    stem.TrimRight();
  }
  if (IsReserved(stem))
    result.InsertAtFront(L'_');
  if (!stream.IsEmpty())
  {
    result += L':';
    result += stream;
  }
  return result;
}

static void TrimEndingSeparator(FString &s)
{
  // keep "C:\" and "\"
  while (s.Len() > 3 && IS_PATH_SEPAR(s.Back()))
    s.DeleteBack();
}

bool CombineSafe(const FString &root, const UStringVector &components, FString &result)
{
  result.Empty();
  FString combined = root;
  FOR_VECTOR (i, components)
  {
    if (!combined.IsEmpty() && !IS_PATH_SEPAR(combined.Back()))
      combined.Add_PathSepar();
    combined += us2fs(components[i]);
  }
  FString full;
  if (!NDir::MyGetFullPathName(combined, full))
    return false;
  FString normalizedRoot;
  if (!NDir::MyGetFullPathName(root, normalizedRoot))
    return false;
  TrimEndingSeparator(normalizedRoot);
  if (!IS_PATH_SEPAR(normalizedRoot.Back()))
    normalizedRoot.Add_PathSepar();
  FString fullWithSepar = full;
  fullWithSepar.Add_PathSepar();
  if (IsString1PrefixedByString2_NoCase(full, normalizedRoot)
      || fullWithSepar.IsEqualTo_NoCase(normalizedRoot))
  {
    result = full;
    return true;
  }
  return false;
}

FString MakeUnique(const FString &path)
{
  if (!NFind::DoesFileOrDirExist(path))
    return path;
  const int slash = path.ReverseFind_PathSepar();
  const FString dir = path.Left((unsigned)(slash + 1));
  const FString name = path.Ptr((unsigned)(slash + 1));
  FString stem = name;
  FString ext;
  const int dot = name.ReverseFind_Dot();
  if (dot >= 0)
  {
    stem = name.Left((unsigned)dot);
    if ((unsigned)dot != name.Len() - 1)
      ext = name.Ptr((unsigned)dot);
  }
  for (UInt32 i = 2;; i++)
  {
    FString candidate = dir;
    candidate += stem;
    candidate += FTEXT(" (");
    candidate.Add_UInt32(i);
    candidate += FTEXT(")");
    candidate += ext;
    if (!NFind::DoesFileOrDirExist(candidate))
      return candidate;
  }
}

}

UString GetFileNamePart(const UString &path)
{
  const int slash = path.ReverseFind_PathSepar();
  return UString(path.Ptr((unsigned)(slash + 1)));
}

UString GetExtensionPart(const UString &path)
{
  const UString name = GetFileNamePart(path);
  const int dot = name.ReverseFind_Dot();
  if (dot < 0 || (unsigned)dot == name.Len() - 1)
    return UString();
  return UString(name.Ptr((unsigned)dot));
}

UString GetFileNameWithoutExtension(const UString &path)
{
  UString name = GetFileNamePart(path);
  const int dot = name.ReverseFind_Dot();
  if (dot >= 0)
    name.DeleteFrom((unsigned)dot);
  return name;
}

UString GetDirectoryPart(const UString &path)
{
  const int slash = path.ReverseFind_PathSepar();
  if (slash < 0)
    return UString();
  if (slash == 2 && path[1] == ':')
    return path.Left(3);
  return path.Left((unsigned)slash);
}
