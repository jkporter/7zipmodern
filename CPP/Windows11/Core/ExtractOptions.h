// ExtractOptions.h

#ifndef ZIP7_INC_EXTRACT_OPTIONS_H
#define ZIP7_INC_EXTRACT_OPTIONS_H

#include "../../Common/MyString.h"

class CArchiveSession;

namespace NExtractPathMode
{
  enum EEnum
  {
    kFullPaths, // recreate the folder structure stored in the archive (relative to BasePath)
    kNoPaths    // flatten everything into the destination folder
  };
}

namespace NOverwriteMode
{
  enum EEnum
  {
    kAsk,
    kOverwrite,
    kSkip,
    kRenameNew,
    kRenameExisting
  };
}

// Mark-of-the-Web propagation (the "Zone.Id" option of 7-Zip, on by default here).
namespace NZonePropagation
{
  enum EEnum
  {
    kOff,
    kAll,
    kOfficeFilesOnly
  };
}

struct CExtractOptions
{
  FString Destination;
  NExtractPathMode::EEnum PathMode;
  UString BasePath; // folder inside the archive to treat as the root (the folder the user is browsing)
  NOverwriteMode::EEnum Overwrite;
  bool RestoreTimestamps;
  bool RestoreAttributes;
  bool ExtractAlternateStreams;
  bool RestoreSymbolicLinks;
  NZonePropagation::EEnum ZonePropagation;
  bool Password_Defined;
  UString Password;

  CExtractOptions():
      PathMode(NExtractPathMode::kFullPaths),
      Overwrite(NOverwriteMode::kAsk),
      RestoreTimestamps(true),
      RestoreAttributes(true),
      ExtractAlternateStreams(false),
      RestoreSymbolicLinks(true),
      ZonePropagation(NZonePropagation::kAll),
      Password_Defined(false)
      {}
};

struct CExtractResult
{
  FString Destination;
  UInt32 FilesProcessed;
  UInt32 Folders;
  UInt32 Skipped;
  UInt32 Errors;
  UInt64 Bytes;
  bool WrongPassword;
  // Distinct top-level items created in the destination (for "show in folder", drag-out).
  FStringVector TopLevelPaths;

  CExtractResult(): FilesProcessed(0), Folders(0), Skipped(0), Errors(0), Bytes(0), WrongPassword(false) {}
};

namespace NExtractPlanner {

/* "Smart" destination: if everything in the archive already lives under one top-level folder (or the
   archive holds a single file) extract straight into (parentFolder); otherwise
   create a folder named after the archive. Avoids both "foo\foo\..." and loose files spilling out. */
FString GetSmartDestination(const CArchiveSession &session, const FString &parentFolder);

// "photos.tar.gz" -> "photos", "data.7z.001" -> "data".
UString GetFolderNameFor(const UString &archiveName);

}

#endif
