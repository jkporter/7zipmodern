// PathSanitizer.h

#ifndef ZIP7_INC_PATH_SANITIZER_H
#define ZIP7_INC_PATH_SANITIZER_H

#include "../../Common/MyString.h"

/*
Turns archive item paths into safe Windows paths: no traversal ("..", rooted, drive or UNC
prefixes), no reserved device names, no characters Windows rejects, no trailing dots/spaces.
*/

namespace NPathSanitizer {

// Safe relative components; empty when nothing remains.
void GetSafeComponents(const UString &archivePath, UStringVector &components, bool allowAltStream = false);

UString CleanComponent(const UString &name, bool allowAltStream = false);

// Combines and verifies that the result stays inside (root). Returns false if it would escape.
bool CombineSafe(const FString &root, const UStringVector &components, FString &result);

// Finds a free "name (2).ext" next to an existing path.
FString MakeUnique(const FString &path);

}

// File name helpers (like .NET Path.GetFileName / GetExtension / GetFileNameWithoutExtension / GetDirectoryName).
UString GetFileNamePart(const UString &path);
UString GetExtensionPart(const UString &path); // ".gz", or "" if there is no extension
UString GetFileNameWithoutExtension(const UString &path);
UString GetDirectoryPart(const UString &path); // without the trailing separator

#endif
