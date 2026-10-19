// UpdateCallback.cpp

#include "StdAfx.h"

#include "UpdateCallback.h"

#include "../../Common/ComTry.h"
#include "../../Common/Defs.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/PropVariant.h"

#include "../../7zip/Common/FileStreams.h"
#include "../../7zip/Common/MultiOutStream.h"
#include "../../7zip/Common/StreamObjects.h"

#include "ArchiveSession.h"

using namespace NWindows;

CUpdateItem::CUpdateItem():
    ArchiveIndex(-1),
    NewData(false),
    NewProps(false),
    IsDirectory(false),
    Modified_Defined(false),
    Created_Defined(false),
    Accessed_Defined(false),
    Size(0),
    Attributes(0),
    SourceData(NULL)
{
  Modified.dwLowDateTime = Modified.dwHighDateTime = 0;
  Created = Accessed = Modified;
}

void CUpdateItem::FromDisk(const NFile::NFind::CFileInfo &fi, const FString &fullPath,
    const UString &archivePath, CUpdateItem &item)
{
  item = CUpdateItem();
  item.NewData = true;
  item.NewProps = true;
  item.ArchivePath = archivePath;
  item.IsDirectory = fi.IsDir();
  item.Size = fi.IsDir() ? 0 : fi.Size;
  item.Attributes = fi.Attrib | (fi.IsDir() ? FILE_ATTRIBUTE_DIRECTORY : 0);
  item.Modified = fi.MTime;
  item.Created = fi.CTime;
  item.Accessed = fi.ATime;
  item.Modified_Defined = item.Created_Defined = item.Accessed_Defined = true;
  item.SourcePath = fullPath;
}

void CUpdateItem::Keep(const CArchiveEntry &e, CUpdateItem &item)
{
  item = CUpdateItem();
  item.ArchiveIndex = (Int32)e.Index;
  item.ArchivePath = e.Path;
  item.IsDirectory = e.IsDirectory;
  item.Size = e.Size;
}

void CUpdateItem::Rename(const CArchiveEntry &e, const UString &newPath, CUpdateItem &item)
{
  item = CUpdateItem();
  item.ArchiveIndex = (Int32)e.Index;
  item.NewProps = true;
  item.ArchivePath = newPath;
  item.IsDirectory = e.IsDirectory;
  item.Size = e.Size;
  item.Attributes = e.Attributes_Defined ? e.Attributes :
      (e.IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
  item.Modified_Defined = e.Modified_Defined;
  item.Modified = e.Modified;
  item.Created_Defined = e.Created_Defined;
  item.Created = e.Created;
  item.Accessed_Defined = e.Accessed_Defined;
  item.Accessed = e.Accessed;
}


void CUpdateCallback::Init(const CObjectVector<CUpdateItem> *items, COperationContext *ctx,
    const UString &newPassword, CArchiveSession *source, bool storeCTime, bool storeATime)
{
  _items = items;
  _ctx = ctx;
  _newPassword = newPassword;
  _source = source;
  _storeCTime = storeCTime;
  _storeATime = storeATime;
}

void CUpdateCallback::UpdateOutputBytes()
{
  if (OutFileSpec)
    _ctx->OutputBytes = OutFileSpec->ProcessedSize;
  else if (OutVolSpec)
    _ctx->OutputBytes = OutVolSpec->GetSize();
}

Z7_COM7F_IMF(CUpdateCallback::SetTotal(UInt64 total))
{
  _ctx->TotalBytes = total;
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CUpdateCallback::SetCompleted(const UInt64 *completeValue))
{
  if (completeValue)
    _ctx->CompletedBytes = *completeValue;
  UpdateOutputBytes();
  _ctx->Report();
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CUpdateCallback::GetUpdateItemInfo(UInt32 index, Int32 *newData, Int32 *newProps, UInt32 *indexInArchive))
{
  const CUpdateItem &item = (*_items)[index];
  if (newData) *newData = BoolToInt(item.NewData);
  if (newProps) *newProps = BoolToInt(item.NewProps);
  if (indexInArchive) *indexInArchive = item.ArchiveIndex < 0 ? (UInt32)(Int32)-1 : (UInt32)item.ArchiveIndex;
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  const CUpdateItem &item = (*_items)[index];
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPath: prop = item.ArchivePath; break;
    case kpidIsDir: prop = item.IsDirectory; break;
    case kpidSize: prop = item.Size; break;
    case kpidAttrib: prop = item.Attributes; break;
    case kpidMTime: if (item.Modified_Defined) prop = item.Modified; break;
    case kpidCTime: if (_storeCTime && item.Created_Defined) prop = item.Created; break;
    case kpidATime: if (_storeATime && item.Accessed_Defined) prop = item.Accessed; break;
    case kpidIsAnti: prop = false; break;
    default: break;
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream))
{
  COM_TRY_BEGIN
  *inStream = NULL;
  const CUpdateItem &item = (*_items)[index];
  RINOK(_ctx->Checkpoint())
  _ctx->SetCurrentItem(item.ArchivePath);
  _ctx->Report();
  if (item.IsDirectory)
    return S_OK;

  if (item.SourceData)
  {
    CBufInStream *spec = new CBufInStream;
    CMyComPtr<ISequentialInStream> stream = spec;
    spec->Init(item.SourceData, (size_t)item.Size);
    *inStream = stream.Detach();
    return S_OK;
  }

  CInFileStream *spec = new CInFileStream;
  CMyComPtr<ISequentialInStream> stream = spec;
  if (!spec->OpenShared(item.SourcePath, true))
  {
    // S_FALSE tells the handler to skip this file (7-Zip's "cannot open file" warning).
    const DWORD error = ::GetLastError();
    FilesSkipped++;
    UString message ("Cannot open file: ");
    UString detail = NError::MyFormatMessage(error);
    detail.Trim();
    message += detail;
    _ctx->AddMessage(NMessageSeverity::kWarning, message,
        item.SourcePath.IsEmpty() ? item.ArchivePath : fs2us(item.SourcePath));
    return S_FALSE;
  }
  *inStream = stream.Detach();
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CUpdateCallback::SetOperationResult(Int32 /* operationResult */))
{
  _ctx->CompletedFiles++;
  _ctx->Report();
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password))
{
  COM_TRY_BEGIN
  *passwordIsDefined = BoolToInt(!_newPassword.IsEmpty());
  return StringToBstr(_newPassword, password);
  COM_TRY_END
}

// Needed when re-packing encrypted solid blocks of the existing archive.
Z7_COM7F_IMF(CUpdateCallback::CryptoGetTextPassword(BSTR *password))
{
  COM_TRY_BEGIN
  *password = NULL;
  UString pwd;
  bool defined = false;
  if (_source && _source->Password_Defined)
  {
    pwd = _source->Password;
    defined = true;
  }
  else if (!_newPassword.IsEmpty())
  {
    pwd = _newPassword;
    defined = true;
  }
  if (!defined && _ctx->Prompts && _source)
    defined = (_ctx->Prompts->RequestPassword(_source->DisplayName, false, pwd) == S_OK);
  if (!defined)
    return E_ABORT;
  if (_source)
  {
    _source->Password = pwd;
    _source->Password_Defined = true;
  }
  return StringToBstr(pwd, password);
  COM_TRY_END
}
