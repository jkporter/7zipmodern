// ArchiveFormat.h

#ifndef ZIP7_INC_ARCHIVE_FORMAT_H
#define ZIP7_INC_ARCHIVE_FORMAT_H

#include "../../Common/MyBuffer.h"
#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../7zip/Archive/IArchive.h"

// A format handler registered inside 7z.dll (queried live, never hard-coded).
struct CArchiveFormat
{
  UInt32 Index;
  UString Name;
  GUID ClassId;
  // Extensions without dots, lower case, in the order the handler lists them.
  UStringVector Extensions;
  // Parallel to Extensions; "*" means none (e.g. "tgz" -> ".tar").
  UStringVector AddExtensions;
  bool CanUpdate;
  UInt32 Flags; // NArcInfoFlags::k*
  CObjectVector<CByteBuffer> Signatures;
  UInt32 SignatureOffset;

  bool Is_Flag(UInt32 flag) const { return (Flags & flag) != 0; }
  UString GetDefaultExtension() const;
  bool MatchesExtension(const UString &extension) const; // with or without the dot
  bool MatchesSignature(const Byte *header, size_t size) const;
  // Name of the single item in a stream archive (e.g. "backup.tgz" -> "backup.tar").
  UString GetDefaultItemName(const UString &archiveFileName) const;
};

/* The list is loaded from the engine on first use and never changes afterwards,
   so (const CArchiveFormat *) pointers stay valid for the life of the process. */
namespace NFormatRegistry {

HRESULT Load(UString &errorMessage); // optional: reports why the engine could not be loaded
const CObjectVector<CArchiveFormat> &All(); // empty if the engine is missing
const CArchiveFormat *Find(const UString &name); // case-insensitive, NULL if missing
// All extensions the engine can open (distinct, sorted); used for file pickers and associations.
void GetAllExtensions(UStringVector &extensions);
bool LooksLikeArchive(const UString &path);

}

#endif
