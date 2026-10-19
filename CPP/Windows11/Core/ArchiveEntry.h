// ArchiveEntry.h

#ifndef ZIP7_INC_ARCHIVE_ENTRY_H
#define ZIP7_INC_ARCHIVE_ENTRY_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "PropertyFormatter.h"

// One item as reported by the archive handler.
struct CArchiveEntry
{
  UInt32 Index;
  UString Path; // normalised: '\' separators, never rooted, no trailing separator
  bool IsDirectory;
  bool IsEncrypted;
  bool IsAltStream;
  bool PackedSize_Defined;
  bool Modified_Defined;
  bool Created_Defined;
  bool Accessed_Defined;
  bool Attributes_Defined;
  bool Crc_Defined;
  UInt64 Size;
  UInt64 PackedSize;
  FILETIME Modified;
  FILETIME Created;
  FILETIME Accessed;
  UInt32 Attributes;
  UInt32 Crc;
  UString Method;
  UString Comment;
  UString SymbolicLink; // empty if the item is not a link

  CArchiveEntry();
  UString GetName() const;
};


struct CFolderMeasure
{
  UInt64 Size;
  UInt64 Packed;
  UInt32 Files;
  UInt32 Folders;

  CFolderMeasure(): Size(0), Packed(0), Files(0), Folders(0) {}
};

// Folder view over the flat item list. Folders that have no explicit entry are synthesised.
class CArchiveFolder
{
  CObjectVector<CArchiveFolder> _folders; // in insertion order; element addresses are stable
  CUIntVector _sorted;                    // indexes of _folders sorted by name (ordinal)

  int FindFolderIndex(const UString &name, unsigned &insertPos) const;
  Z7_CLASS_NO_COPY(CArchiveFolder)
public:
  UString Name;
  UString Path; // path inside the archive, "" for the root
  CArchiveFolder *Parent;
  const CArchiveEntry *Entry; // the explicit directory entry, when the archive stores one
  CRecordVector<const CArchiveEntry *> Files;

  CArchiveFolder(): Parent(NULL), Entry(NULL) {}

  unsigned GetNumFolders() const { return _folders.Size(); }
  const CArchiveFolder &GetFolder(unsigned index) const { return _folders[index]; }
  const CArchiveFolder *FindFolder(const UString &name) const; // NULL if missing

  // Resolves "a\b\c" relative to this folder. NULL if missing.
  const CArchiveFolder *Navigate(const UString &relativePath) const;

  // Appends the item indexes of this folder's entry, files and all descendants.
  void EnumerateEntriesRecursive(CRecordVector<UInt32> &indices) const;
  void EnumerateEntriesRecursive(CRecordVector<const CArchiveEntry *> &entries) const;

  CFolderMeasure Measure() const;
  bool GetLatestModified(FILETIME &ft) const;

  CArchiveFolder &GetOrAddFolder(const UString &name);
  void Clear();
  // (entries) must outlive the tree.
  void Build(const CObjectVector<CArchiveEntry> &entries, bool skipAltStreams);
};


// Archive-level information (format, sizes, integrity flags).
struct CArchiveInfo
{
  UString FormatName;
  bool PhysicalSize_Defined;
  bool HeadersSize_Defined;
  bool IsSolid_Defined;
  bool IsSolid;
  bool Blocks_Defined;
  bool Volumes_Defined;
  bool Offset_Defined;
  bool HeadersEncrypted;
  UInt64 PhysicalSize;
  UInt64 HeadersSize;
  UInt32 Blocks;
  UInt32 Volumes;
  Int64 Offset;
  UString Method;
  UString Comment;
  UInt32 ErrorFlags;   // kpv_ErrorFlags_*
  UInt32 WarningFlags;
  UString Error;
  UString Warning;
  // Every archive property the handler reports, for the Properties pane.
  CPropertyItemVector AllProperties;

  CArchiveInfo();
  void Clear();
  bool HasErrors() const { return ErrorFlags != 0 || !Error.IsEmpty(); }
  bool HasWarnings() const { return WarningFlags != 0 || !Warning.IsEmpty(); }

  // "Not an archive", "Headers error", ...
  static void DescribeErrorFlags(UInt32 flags, UStringVector &names)
    { NPropertyFormatter::DescribeErrorFlags(flags, names); }
};

#endif
