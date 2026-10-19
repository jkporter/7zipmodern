// UpdateCallback.h

#ifndef ZIP7_INC_UPDATE_CALLBACK_H
#define ZIP7_INC_UPDATE_CALLBACK_H

#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

#include "../../Windows/FileFind.h"

#include "../../7zip/Archive/IArchive.h"
#include "../../7zip/IPassword.h"

#include "ArchiveEntry.h"
#include "OperationContext.h"

class CArchiveSession;
class COutFileStream;
class CMultiOutStream;

// One item of the output archive: either copied from the old archive or new data from disk / memory.
struct CUpdateItem
{
  Int32 ArchiveIndex; // index in the existing archive, or -1 for new items
  bool NewData;
  bool NewProps;
  bool IsDirectory;
  bool Modified_Defined;
  bool Created_Defined;
  bool Accessed_Defined;
  UString ArchivePath;
  UInt64 Size;
  UInt32 Attributes;
  FILETIME Modified;
  FILETIME Created;
  FILETIME Accessed;
  FString SourcePath;
  // In-memory source (benchmark): used instead of SourcePath when not NULL. Not owned.
  const Byte *SourceData;

  CUpdateItem();

  static void FromDisk(const NWindows::NFile::NFind::CFileInfo &fi, const FString &fullPath,
      const UString &archivePath, CUpdateItem &item);
  static void Keep(const CArchiveEntry &e, CUpdateItem &item);
  static void Rename(const CArchiveEntry &e, const UString &newPath, CUpdateItem &item);
};


Z7_CLASS_IMP_COM_3(
  CUpdateCallback
  , IArchiveUpdateCallback
  , ICryptoGetTextPassword2
  , ICryptoGetTextPassword
)
  Z7_IFACE_COM7_IMP(IProgress)

  const CObjectVector<CUpdateItem> *_items;
  COperationContext *_ctx;
  UString _newPassword; // "" = none
  CArchiveSession *_source;
  bool _storeCTime;
  bool _storeATime;

  void UpdateOutputBytes();
public:
  // optional: the output stream, to report OutputBytes while writing
  COutFileStream *OutFileSpec;
  CMultiOutStream *OutVolSpec;
  UInt32 FilesSkipped;

  CUpdateCallback():
      _items(NULL), _ctx(NULL), _source(NULL), _storeCTime(false), _storeATime(false),
      OutFileSpec(NULL), OutVolSpec(NULL), FilesSkipped(0) {}

  void Init(const CObjectVector<CUpdateItem> *items, COperationContext *ctx, const UString &newPassword,
      CArchiveSession *source, bool storeCTime, bool storeATime);
};

#endif
