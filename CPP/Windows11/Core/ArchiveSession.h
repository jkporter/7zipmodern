// ArchiveSession.h

#ifndef ZIP7_INC_ARCHIVE_SESSION_H
#define ZIP7_INC_ARCHIVE_SESSION_H

#include "../../Common/MyBuffer.h"
#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

#include "../../Windows/Synchronization.h"

#include "../../7zip/Archive/IArchive.h"

#include "ArchiveEntry.h"
#include "ArchiveFormat.h"
#include "OperationContext.h"

class CArchiveSession;

struct CArchiveOpenOptions
{
  const CArchiveFormat *Format;   // force a handler ("Open as..."); NULL = detect
  bool Password_Defined;
  UString Password;
  UString DisplayName;            // when the file is a temporary copy (nested archives); "" = file name
  const CArchiveSession *Parent;  // the archive this one was extracted from, or NULL
  UString PathInParent;

  CArchiveOpenOptions(): Format(NULL), Password_Defined(false), Parent(NULL) {}
};

/*
An open archive. All engine calls on one handler instance are serialised through Gate,
because handlers are not thread-safe. The destructor closes the archive (C# Dispose).
*/
class CArchiveSession
{
  CRecordVector<IInArchive *> _outerArchives; // owned references (Split container)

  HRESULT UnwrapContainer(COperationContext &ctx, UString &errorMessage);
  HRESULT Load(UString &errorMessage);
  void ReadInfo();
  void ReleaseOuter();

  Z7_CLASS_NO_COPY(CArchiveSession)
public:
  FString FilePath;
  UString DisplayName;
  const CArchiveFormat *Format;
  CMyComPtr<IInArchive> Archive;
  mutable NWindows::NSynchronization::CCriticalSection Gate;

  CObjectVector<CArchiveEntry> Entries;
  CArchiveFolder Root;
  CArchiveInfo Info;
  FStringVector VolumePaths;
  bool Password_Defined;
  UString Password;
  bool HeadersEncrypted;
  bool HasEncryptedItems;

  // The archive this one was opened from (nested archives), or NULL.
  const CArchiveSession *Parent;
  UString PathInParent;
  // Format of an outer container ("Split") when the archive was opened through one; "" otherwise.
  UString ContainerFormat;

  // Zone.Identifier (Mark of the Web) of the archive file, propagated to extracted files.
  bool ZoneIdentifier_Defined;
  CByteBuffer ZoneIdentifier;

  CArchiveSession();
  ~CArchiveSession();

  /* Returns S_OK, E_ABORT (cancelled, also when the password prompt was cancelled),
     k_HRESULT_WrongPassword (encrypted headers, wrong password), k_HRESULT_IsNotArc,
     or another error. On failure the session stays closed. */
  HRESULT Open(const FString &path, const CArchiveOpenOptions &options, COperationContext &ctx, UString &errorMessage);
  bool IsOpen() const { return Archive != NULL; }
  void Close(); // C# Dispose

  bool CanUpdate() const;

  // Every property the handler reports for one item (for the details pane).
  void GetItemProperties(UInt32 index, CPropertyItemVector &list) const;

  /* "backup.tar.gz" opens straight into the tar (one stream layer is just noise).
     CanUnwrapTarball(): the archive is a single-stream format (gzip, bzip2, xz, zstd, lzma, z, lz, lz4)
     with exactly one entry whose name ends with ".tar".
     UnwrapTarball() extracts that entry into (tempFolder) and opens it into (inner);
     inner.Parent = this, so keep this session alive while (inner) is open. */
  bool CanUnwrapTarball() const;
  HRESULT UnwrapTarball(const FString &tempFolder, CArchiveSession &inner, COperationContext &ctx, UString &errorMessage);

  // item property helpers (no Gate lock; the caller holds it if needed)
  bool GetString(UInt32 index, PROPID propID, UString &value) const;
  bool GetBool(UInt32 index, PROPID propID, bool &value) const;
  bool GetUInt64(UInt32 index, PROPID propID, UInt64 &value) const;
  bool GetTime(UInt32 index, PROPID propID, FILETIME &value) const;
  bool GetArcUInt64(PROPID propID, UInt64 &value) const;
  bool GetArcBool(PROPID propID, bool &value) const;
  bool GetArcString(PROPID propID, UString &value) const;

  // '/' -> '\', no leading or trailing separators
  static UString NormalizePath(const UString &path);

  // Candidate handlers, most likely first (extension, signature, signature search, executables last).
  static void GetCandidates(const UString &fileName, const Byte *header, size_t headerSize,
      const CArchiveFormat *forced, CRecordVector<const CArchiveFormat *> &result);
};

#endif
