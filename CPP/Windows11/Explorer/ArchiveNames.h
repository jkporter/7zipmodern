// ArchiveNames.h

#ifndef ZIP7_INC_ARCHIVE_NAMES_H
#define ZIP7_INC_ARCHIVE_NAMES_H

#include "../../Common/MyString.h"

// The menu decides by extension only: it must be fast and it never loads 7z.dll into Explorer.
bool IsArchivePath(const UString &path);

// "photos" for "Extract to "photos\"" (strips up to two archive extensions: photos.tar.gz).
UString GetExtractFolderName(const UString &arcPath);

// "name" for "Add to "name.7z"": the item itself or, for several items, their folder.
UString GetAddArchiveName(const UStringVector &paths);

// Long names are shortened in the middle: "abc...xyz".
UString ShortenMenuName(const UString &s);

#endif
