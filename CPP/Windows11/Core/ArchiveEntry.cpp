// ArchiveEntry.cpp

#include "StdAfx.h"

#include "ArchiveEntry.h"

#include "../../Common/Wildcard.h"

#include "../../Windows/TimeUtils.h"

CArchiveEntry::CArchiveEntry():
    Index(0),
    IsDirectory(false),
    IsEncrypted(false),
    IsAltStream(false),
    PackedSize_Defined(false),
    Modified_Defined(false),
    Created_Defined(false),
    Accessed_Defined(false),
    Attributes_Defined(false),
    Crc_Defined(false),
    Size(0),
    PackedSize(0),
    Attributes(0),
    Crc(0)
{
  Modified.dwLowDateTime = Modified.dwHighDateTime = 0;
  Created = Accessed = Modified;
}

UString CArchiveEntry::GetName() const
{
  const int i = Path.ReverseFind(L'\\');
  return UString(Path.Ptr((unsigned)(i + 1)));
}


int CArchiveFolder::FindFolderIndex(const UString &name, unsigned &insertPos) const
{
  unsigned left = 0, right = _sorted.Size();
  while (left != right)
  {
    const unsigned mid = (left + right) / 2;
    const int cmp = wcscmp(name, _folders[_sorted[mid]].Name);
    if (cmp == 0)
    {
      insertPos = mid;
      return (int)_sorted[mid];
    }
    if (cmp < 0)
      right = mid;
    else
      left = mid + 1;
  }
  insertPos = left;
  return -1;
}

const CArchiveFolder *CArchiveFolder::FindFolder(const UString &name) const
{
  unsigned pos;
  const int index = FindFolderIndex(name, pos);
  return index < 0 ? NULL : &_folders[(unsigned)index];
}

CArchiveFolder &CArchiveFolder::GetOrAddFolder(const UString &name)
{
  unsigned pos;
  const int index = FindFolderIndex(name, pos);
  if (index >= 0)
    return _folders[(unsigned)index];
  CArchiveFolder &folder = _folders.AddNew();
  folder.Name = name;
  if (Path.IsEmpty())
    folder.Path = name;
  else
  {
    folder.Path = Path;
    folder.Path += L'\\';
    folder.Path += name;
  }
  folder.Parent = this;
  _sorted.Insert(pos, _folders.Size() - 1);
  return folder;
}

const CArchiveFolder *CArchiveFolder::Navigate(const UString &relativePath) const
{
  const CArchiveFolder *current = this;
  UStringVector parts;
  SplitPathToParts(relativePath, parts);
  FOR_VECTOR (i, parts)
  {
    if (parts[i].IsEmpty())
      continue;
    current = current->FindFolder(parts[i]);
    if (!current)
      return NULL;
  }
  return current;
}

void CArchiveFolder::EnumerateEntriesRecursive(CRecordVector<const CArchiveEntry *> &entries) const
{
  if (Entry)
    entries.Add(Entry);
  FOR_VECTOR (i, Files)
    entries.Add(Files[i]);
  FOR_VECTOR (i, _folders)
    _folders[i].EnumerateEntriesRecursive(entries);
}

void CArchiveFolder::EnumerateEntriesRecursive(CRecordVector<UInt32> &indices) const
{
  if (Entry)
    indices.Add(Entry->Index);
  FOR_VECTOR (i, Files)
    indices.Add(Files[i]->Index);
  FOR_VECTOR (i, _folders)
    _folders[i].EnumerateEntriesRecursive(indices);
}

CFolderMeasure CArchiveFolder::Measure() const
{
  CFolderMeasure m;
  FOR_VECTOR (i, Files)
  {
    const CArchiveEntry &f = *Files[i];
    m.Size += f.Size;
    if (f.PackedSize_Defined)
      m.Packed += f.PackedSize;
    m.Files++;
  }
  FOR_VECTOR (i, _folders)
  {
    const CFolderMeasure sub = _folders[i].Measure();
    m.Size += sub.Size;
    m.Packed += sub.Packed;
    m.Files += sub.Files;
    m.Folders += sub.Folders + 1;
  }
  return m;
}

bool CArchiveFolder::GetLatestModified(FILETIME &ft) const
{
  bool defined = false;
  ft.dwLowDateTime = ft.dwHighDateTime = 0;
  if (Entry && Entry->Modified_Defined)
  {
    ft = Entry->Modified;
    defined = true;
  }
  FOR_VECTOR (i, Files)
  {
    const CArchiveEntry &f = *Files[i];
    if (f.Modified_Defined && (!defined || ::CompareFileTime(&f.Modified, &ft) > 0))
    {
      ft = f.Modified;
      defined = true;
    }
  }
  return defined;
}

void CArchiveFolder::Clear()
{
  _folders.Clear();
  _sorted.Clear();
  Files.Clear();
  Entry = NULL;
}

void CArchiveFolder::Build(const CObjectVector<CArchiveEntry> &entries, bool skipAltStreams)
{
  Clear();
  FOR_VECTOR (k, entries)
  {
    const CArchiveEntry &entry = entries[k];
    if (skipAltStreams && entry.IsAltStream)
      continue;
    UStringVector all, parts;
    SplitPathToParts(entry.Path, all);
    FOR_VECTOR (i, all)
      if (!all[i].IsEmpty())
        parts.Add(all[i]);
    if (parts.IsEmpty())
      continue;
    CArchiveFolder *folder = this;
    for (unsigned i = 0; i + 1 < parts.Size(); i++)
      folder = &folder->GetOrAddFolder(parts[i]);
    if (entry.IsDirectory)
      folder->GetOrAddFolder(parts.Back()).Entry = &entry;
    else
      folder->Files.Add(&entry);
  }
}


CArchiveInfo::CArchiveInfo()
{
  Clear();
}

void CArchiveInfo::Clear()
{
  FormatName.Empty();
  PhysicalSize_Defined = HeadersSize_Defined = IsSolid_Defined = IsSolid = false;
  Blocks_Defined = Volumes_Defined = Offset_Defined = HeadersEncrypted = false;
  PhysicalSize = HeadersSize = 0;
  Blocks = Volumes = 0;
  Offset = 0;
  Method.Empty();
  Comment.Empty();
  ErrorFlags = WarningFlags = 0;
  Error.Empty();
  Warning.Empty();
  AllProperties.Clear();
}
