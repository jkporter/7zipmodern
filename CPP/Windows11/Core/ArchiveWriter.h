// ArchiveWriter.h

#ifndef ZIP7_INC_ARCHIVE_WRITER_H
#define ZIP7_INC_ARCHIVE_WRITER_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../Windows/PropVariant.h"

#include "ArchiveSession.h"
#include "CompressionSettings.h"
#include "OperationContext.h"
#include "UpdateCallback.h"

struct CCompressResult
{
  FStringVector OutputFiles; // the archive, or all volumes
  UInt64 InputBytes;
  UInt64 OutputBytes;
  UInt32 Files;
  UInt32 Skipped;

  CCompressResult(): InputBytes(0), OutputBytes(0), Files(0), Skipped(0) {}
  double GetRatio() const { return InputBytes == 0 ? 1 : (double)OutputBytes / (double)InputBytes; }
};

struct CRenamePair
{
  UString From; // old archive path prefix
  UString To;   // new prefix (renames a file or a whole folder)
};

struct CReplaceItem
{
  UInt32 Index;    // item in the archive
  FString Path;    // file on disk whose content replaces it (edited temp files)
};

// Changes to apply to an existing archive in one rewrite.
struct CUpdatePlan
{
  FStringVector Add;           // files/folders on disk to add
  UString TargetFolder;        // folder inside the archive that receives Add ("" = root)
  CRecordVector<UInt32> Delete; // items to remove; folders must already be expanded to their descendants
  CObjectVector<CRenamePair> Rename;
  CObjectVector<CReplaceItem> Replace;
  UStringVector NewFolders;    // empty folders to create
};

struct CEngineProp
{
  UString Name;
  NWindows::NCOM::CPropVariant Value;
};

typedef CObjectVector<CEngineProp> CEnginePropVector;

namespace NArchiveWriter {

// Creates a new archive (replacing any existing file only after success).
HRESULT Create(const FString &archivePath, const FStringVector &sources, const CCompressionSettings &settings,
    COperationContext &ctx, CCompressResult &result, UString &errorMessage);

/* Applies (plan) to an open archive, then closes the session and swaps the files atomically.
   (settings) == NULL: keep the archive's format with default options.
   On failure the session stays open and the archive is unchanged. */
HRESULT Update(CArchiveSession &session, const CUpdatePlan &plan, const CCompressionSettings *settings,
    COperationContext &ctx, CCompressResult &result, UString &errorMessage);

// ---- lower level (used by Create / Update / the benchmark) ----

HRESULT CreateFromItems(const FString &archivePath, CObjectVector<CUpdateItem> &items,
    const CCompressionSettings &settings, COperationContext &ctx, CCompressResult &result, UString &errorMessage);

HRESULT CollectSources(const FStringVector &sources, const UString &archivePrefix, const CCompressionSettings &settings,
    COperationContext &ctx, const FString &exclude, CObjectVector<CUpdateItem> &items);

UString EngineFormatName(const UString &format); // "gz" -> "gzip", "bz2" -> "bzip2"
void BuildProperties(const CCompressionSettings &s, const UString &engineFormat, bool updating, CEnginePropVector &props);
HRESULT ApplyProperties(IUnknown *handler, const CEnginePropVector &props);

}

#endif
