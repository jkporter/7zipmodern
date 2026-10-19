// ArchiveSession.cpp

#include "StdAfx.h"

#include "ArchiveSession.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/PropVariant.h"

#include "../../7zip/Common/FileStreams.h"
#include "../../7zip/Common/StreamUtils.h"

#include "ExtractEngine.h"
#include "OpenCallback.h"
#include "PathSanitizer.h"
#include "SevenZipEngine.h"
#include "SharedInFileStream.h"

using namespace NWindows;
using namespace NFile;

static const UInt64 kMaxStartSearch = (UInt64)1 << 23;
static const size_t kHeaderSize = (size_t)1 << 16;

CArchiveSession::CArchiveSession():
    Format(NULL),
    Password_Defined(false),
    HeadersEncrypted(false),
    HasEncryptedItems(false),
    Parent(NULL),
    ZoneIdentifier_Defined(false)
{
}

CArchiveSession::~CArchiveSession()
{
  Close();
}

void CArchiveSession::ReleaseOuter()
{
  FOR_VECTOR (i, _outerArchives)
  {
    _outerArchives[i]->Close();
    _outerArchives[i]->Release();
  }
  _outerArchives.Clear();
}

void CArchiveSession::Close()
{
  NSynchronization::CCriticalSectionLock lock(Gate);
  if (Archive)
  {
    Archive->Close();
    Archive.Release();
  }
  ReleaseOuter();
}

bool CArchiveSession::CanUpdate() const
{
  return Format && Format->CanUpdate && !Parent && ContainerFormat.IsEmpty()
      && VolumePaths.Size() <= 1 && !Info.HasErrors();
}

static bool IsDigit(wchar_t c) { return c >= '0' && c <= '9'; }

static void AddCandidate(CRecordVector<const CArchiveFormat *> &result,
    CRecordVector<const CArchiveFormat *> &preArc, const CArchiveFormat *f)
{
  FOR_VECTOR (i, result)
    if (result[i] == f)
      return;
  FOR_VECTOR (i, preArc)
    if (preArc[i] == f)
      return;
  if (f->Is_Flag(NArcInfoFlags::kPreArc))
    preArc.Add(f);
  else
    result.Add(f);
}

void CArchiveSession::GetCandidates(const UString &fileName, const Byte *header, size_t headerSize,
    const CArchiveFormat *forced, CRecordVector<const CArchiveFormat *> &result)
{
  result.Clear();
  if (forced)
  {
    result.Add(forced);
    return;
  }

  const CObjectVector<CArchiveFormat> &all = NFormatRegistry::All();
  const UString ext = GetExtensionPart(fileName);
  CRecordVector<const CArchiveFormat *> preArc;

  const bool numbered = ext.Len() == 4 && IsDigit(ext[1]) && IsDigit(ext[2]) && IsDigit(ext[3]);
  if (numbered)
  {
    const CArchiveFormat *split = NFormatRegistry::Find(UString("Split"));
    if (split)
      result.Add(split);
  }

  if (ext.Len() > 1)
    FOR_VECTOR (i, all)
      if (all[i].MatchesExtension(ext))
        AddCandidate(result, preArc, &all[i]);

  FOR_VECTOR (i, all)
  {
    const CArchiveFormat &f = all[i];
    if (f.Is_Flag(NArcInfoFlags::kByExtOnlyOpen))
      continue;
    UInt32 isArc = k_IsArc_Res_NO;
    if (f.MatchesSignature(header, headerSize)
        || (NSevenZipEngine::ProbeIsArc(f.Index, header, headerSize, isArc) && isArc == k_IsArc_Res_YES))
      AddCandidate(result, preArc, &f);
  }

  FOR_VECTOR (i, all)
  {
    const CArchiveFormat &f = all[i];
    if (f.Is_Flag(NArcInfoFlags::kFindSignature) && !f.Is_Flag(NArcInfoFlags::kByExtOnlyOpen))
      AddCandidate(result, preArc, &f);
  }

  result += preArc;
}

static bool IsNotArchive(IInArchive *archive)
{
  NCOM::CPropVariant prop;
  if (archive->GetArchiveProperty(kpidErrorFlags, &prop) != S_OK)
    return false;
  UInt32 flags = 0;
  PropToUInt32(prop, flags);
  return (flags & kpv_ErrorFlags_IsNotArc) != 0;
}

static void ReadZoneIdentifier(const FString &path, bool &defined, CByteBuffer &data)
{
  defined = false;
  data.Free();
  NIO::CInFile file;
  if (!file.Open(path + FTEXT(":Zone.Identifier")))
    return;
  UInt64 length;
  if (!file.GetLength(length) || length > ((UInt32)1 << 20))
    return;
  data.Alloc((size_t)length);
  size_t processed = 0;
  if (!file.ReadFull(data, (size_t)length, processed) || processed != (size_t)length)
  {
    data.Free();
    return;
  }
  defined = true;
}

static void SetWrongPasswordMessage(const UString &displayName, UString &errorMessage)
{
  errorMessage = "The password for '";
  errorMessage += displayName;
  errorMessage += "' is incorrect.";
}

HRESULT CArchiveSession::Open(const FString &path, const CArchiveOpenOptions &options,
    COperationContext &ctx, UString &errorMessage)
{
  errorMessage.Empty();
  Close();
  Entries.Clear();
  Root.Clear();
  Info.Clear();
  VolumePaths.Clear();
  ContainerFormat.Empty();
  Format = NULL;

  FString fullPath;
  if (!NDir::MyGetFullPathName(path, fullPath))
    fullPath = path;

  {
    UString engineError;
    const HRESULT res = NFormatRegistry::Load(engineError);
    if (res != S_OK)
    {
      errorMessage = engineError;
      return res;
    }
  }

  CSharedInFileStream *fileSpec = new CSharedInFileStream;
  CMyComPtr<IInStream> stream = fileSpec;
  if (!fileSpec->Open(fullPath))
  {
    const HRESULT res = GetLastError_noZero_HRESULT();
    errorMessage = "Cannot open '";
    errorMessage += fs2us(fullPath);
    errorMessage += "': ";
    errorMessage += NError::MyFormatMessage(res);
    return res;
  }

  CByteBuffer header(kHeaderSize);
  size_t headerLength = kHeaderSize;
  {
    const HRESULT res = ReadStream(stream, header, &headerLength);
    if (res != S_OK)
    {
      FormatHResultMessage(res, fs2us(fullPath), errorMessage);
      return res;
    }
  }

  COpenCallback *callbackSpec = new COpenCallback;
  CMyComPtr<IArchiveOpenCallback> callback = callbackSpec;
  callbackSpec->Init(fullPath, &ctx, options.Password_Defined, options.Password);
  callbackSpec->VolumePaths.Add(fullPath);

  const UString displayName = options.DisplayName.IsEmpty() ? GetFileNamePart(fs2us(fullPath)) : options.DisplayName;

  CRecordVector<const CArchiveFormat *> candidates;
  GetCandidates(displayName, header, headerLength, options.Format, candidates);

  HRESULT lastError = S_OK;
  UString lastErrorMessage;

  FOR_VECTOR (ci, candidates)
  {
    const CArchiveFormat *format = candidates[ci];
    if (ctx.IsCancelled())
      return E_ABORT;
    CMyComPtr<IInArchive> archive;
    if (NSevenZipEngine::CreateInArchive(format->ClassId, archive) != S_OK)
      continue;

    RINOK(stream->Seek(0, STREAM_SEEK_SET, NULL))
    UInt64 maxCheck = (format->Is_Flag(NArcInfoFlags::kFindSignature) || options.Format) ? kMaxStartSearch : 0;
    const HRESULT hr = archive->Open(stream, &maxCheck, callback);

    if (hr == E_ABORT || callbackSpec->PasswordCancelled)
    {
      archive->Close();
      return E_ABORT;
    }

    if (hr == S_OK && !IsNotArchive(archive))
    {
      FilePath = fullPath;
      DisplayName = displayName;
      Format = format;
      Archive = archive;
      Parent = options.Parent;
      PathInParent = options.PathInParent;
      VolumePaths = callbackSpec->VolumePaths;
      Password_Defined = callbackSpec->Password_Defined;
      Password = callbackSpec->Password;
      HeadersEncrypted = callbackSpec->PasswordRequested;
      ReadZoneIdentifier(fullPath, ZoneIdentifier_Defined, ZoneIdentifier);
      if (!ZoneIdentifier_Defined && options.Parent && options.Parent->ZoneIdentifier_Defined)
      {
        ZoneIdentifier_Defined = true;
        ZoneIdentifier = options.Parent->ZoneIdentifier;
      }

      HRESULT res = UnwrapContainer(ctx, errorMessage);
      if (res == S_OK)
        res = Load(errorMessage);
      if (res != S_OK)
        Close();
      return res;
    }

    archive->Close();
    if (callbackSpec->PasswordRequested)
    {
      SetWrongPasswordMessage(displayName, errorMessage);
      return k_HRESULT_WrongPassword;
    }
    if (hr != S_OK && hr != S_FALSE)
    {
      lastError = hr;
      FormatHResultMessage(hr, format->Name, lastErrorMessage);
    }
  }

  if (lastError != S_OK)
  {
    errorMessage = lastErrorMessage;
    return lastError;
  }
  errorMessage = "Cannot open '";
  errorMessage += displayName;
  errorMessage += "' as an archive.";
  return k_HRESULT_IsNotArc;
}

// Split volumes (.001, .002...) present one combined stream; open the real archive inside it.
HRESULT CArchiveSession::UnwrapContainer(COperationContext &ctx, UString &errorMessage)
{
  if (!Format->Name.IsEqualTo_Ascii_NoCase("Split"))
    return S_OK;
  CMyComPtr<IInArchiveGetStream> getStream;
  Archive.QueryInterface(IID_IInArchiveGetStream, &getStream);
  if (!getStream)
    return S_OK;

  UString innerName;
  if (!GetString(0, kpidPath, innerName) || innerName.IsEmpty())
    innerName = GetFileNameWithoutExtension(fs2us(FilePath));

  CMyComPtr<ISequentialInStream> seq;
  if (getStream->GetStream(0, &seq) != S_OK || !seq)
    return S_OK;
  CMyComPtr<IInStream> inner;
  seq.QueryInterface(IID_IInStream, &inner);
  if (!inner)
    return S_OK;

  CByteBuffer header(kHeaderSize);
  size_t headerLength = kHeaderSize;
  if (ReadStream(inner, header, &headerLength) != S_OK)
    headerLength = 0;

  COpenCallback *callbackSpec = new COpenCallback;
  CMyComPtr<IArchiveOpenCallback> callback = callbackSpec;
  callbackSpec->Init(FilePath, &ctx, Password_Defined, Password);
  callbackSpec->DisableVolumes = true;

  CRecordVector<const CArchiveFormat *> candidates;
  GetCandidates(innerName, header, headerLength, NULL, candidates);

  FOR_VECTOR (ci, candidates)
  {
    const CArchiveFormat *format = candidates[ci];
    if (format->Name.IsEqualTo_Ascii_NoCase("Split"))
      continue;
    CMyComPtr<IInArchive> archive;
    if (NSevenZipEngine::CreateInArchive(format->ClassId, archive) != S_OK)
      continue;
    RINOK(inner->Seek(0, STREAM_SEEK_SET, NULL))
    UInt64 maxCheck = format->Is_Flag(NArcInfoFlags::kFindSignature) ? kMaxStartSearch : 0;
    const HRESULT hr = archive->Open(inner, &maxCheck, callback);
    if (hr == E_ABORT || callbackSpec->PasswordCancelled)
    {
      archive->Close();
      return E_ABORT;
    }
    if (hr == S_OK && !IsNotArchive(archive))
    {
      ContainerFormat = Format->Name;
      _outerArchives.Add(Archive.Detach());
      Archive = archive;
      Format = format;
      Password_Defined = callbackSpec->Password_Defined;
      Password = callbackSpec->Password;
      HeadersEncrypted = callbackSpec->PasswordRequested;
      return S_OK;
    }
    archive->Close();
    if (callbackSpec->PasswordRequested)
    {
      SetWrongPasswordMessage(DisplayName, errorMessage);
      return k_HRESULT_WrongPassword;
    }
  }
  return S_OK;
}

UString CArchiveSession::NormalizePath(const UString &pathSrc)
{
  UString path = pathSrc;
  path.Replace(L'/', L'\\');
  unsigned start = 0;
  while (start < path.Len() && path[start] == '\\')
    start++;
  path.DeleteFrontal(start);
  while (!path.IsEmpty() && path.Back() == '\\')
    path.DeleteBack();
  return path;
}

HRESULT CArchiveSession::Load(UString &errorMessage)
{
  UInt32 count = 0;
  {
    const HRESULT res = Archive->GetNumberOfItems(&count);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Reading archive"), errorMessage);
      return res;
    }
  }

  Entries.ClearAndReserve(count);
  bool anyEncrypted = false;
  for (UInt32 i = 0; i < count; i++)
  {
    CArchiveEntry &e = Entries.AddNew();
    e.Index = i;
    UString path;
    GetString(i, kpidPath, path);
    if (path.IsEmpty())
    {
      if (count == 1)
        path = Format->GetDefaultItemName(DisplayName);
      else
      {
        path = "[";
        path.Add_UInt32(i);
        path += "]";
      }
    }
    e.Path = NormalizePath(path);
    GetBool(i, kpidEncrypted, e.IsEncrypted);
    anyEncrypted = anyEncrypted || e.IsEncrypted;
    GetBool(i, kpidIsDir, e.IsDirectory);
    GetUInt64(i, kpidSize, e.Size);
    e.PackedSize_Defined = GetUInt64(i, kpidPackSize, e.PackedSize);
    e.Modified_Defined = GetTime(i, kpidMTime, e.Modified);
    e.Created_Defined = GetTime(i, kpidCTime, e.Created);
    e.Accessed_Defined = GetTime(i, kpidATime, e.Accessed);
    UInt64 v;
    e.Attributes_Defined = GetUInt64(i, kpidAttrib, v);
    e.Attributes = (UInt32)v;
    e.Crc_Defined = GetUInt64(i, kpidCRC, v);
    e.Crc = (UInt32)v;
    GetString(i, kpidMethod, e.Method);
    GetBool(i, kpidIsAltStream, e.IsAltStream);
    GetString(i, kpidComment, e.Comment);
    GetString(i, kpidSymLink, e.SymbolicLink);
  }
  HasEncryptedItems = anyEncrypted || HeadersEncrypted;
  Root.Build(Entries, true);
  ReadInfo();
  return S_OK;
}

static void AddProp(CPropertyItemVector &list, const char *name, const UString &value)
{
  CPropertyItem &item = list.AddNew();
  item.Name = name;
  item.Value = value;
}

void CArchiveSession::ReadInfo()
{
  Info.Clear();
  CPropertyItemVector &all = Info.AllProperties;
  AddProp(all, "Path", fs2us(FilePath));
  {
    UString type = Format->Name;
    if (!ContainerFormat.IsEmpty())
    {
      type += " (inside ";
      type += ContainerFormat;
      type += ")";
    }
    AddProp(all, "Type", type);
  }
  if (VolumePaths.Size() > 1)
  {
    UString s;
    s.Add_UInt32(VolumePaths.Size());
    AddProp(all, "Volumes", s);
  }

  UInt32 numProps = 0;
  if (Archive->GetNumberOfArchiveProperties(&numProps) == S_OK)
  {
    for (UInt32 i = 0; i < numProps; i++)
    {
      CMyComBSTR name;
      PROPID propID;
      VARTYPE vt;
      if (Archive->GetArchivePropertyInfo(i, &name, &propID, &vt) != S_OK)
        continue;
      NCOM::CPropVariant prop;
      if (Archive->GetArchiveProperty(propID, &prop) == S_OK && prop.vt != VT_EMPTY)
      {
        CPropertyItem &item = all.AddNew();
        item.Name = NPropertyFormatter::GetPropertyDisplayName(propID, name);
        item.Value = NPropertyFormatter::FormatValue(propID, prop);
      }
    }
  }

  Info.FormatName = Format->Name;
  Info.PhysicalSize_Defined = GetArcUInt64(kpidPhySize, Info.PhysicalSize);
  if (!Info.PhysicalSize_Defined)
  {
    NFind::CFileInfo fi;
    if (fi.Find(FilePath))
    {
      Info.PhysicalSize = fi.Size;
      Info.PhysicalSize_Defined = true;
    }
  }
  Info.HeadersSize_Defined = GetArcUInt64(kpidHeadersSize, Info.HeadersSize);
  GetArcString(kpidMethod, Info.Method);
  Info.IsSolid_Defined = GetArcBool(kpidSolid, Info.IsSolid);
  UInt64 v;
  Info.Blocks_Defined = GetArcUInt64(kpidNumBlocks, v);
  Info.Blocks = (UInt32)v;
  Info.Volumes_Defined = GetArcUInt64(kpidNumVolumes, v);
  Info.Volumes = (UInt32)v;
  Info.Offset_Defined = GetArcUInt64(kpidOffset, v);
  Info.Offset = (Int64)v;
  GetArcString(kpidComment, Info.Comment);
  Info.HeadersEncrypted = HeadersEncrypted;
  if (GetArcUInt64(kpidErrorFlags, v))
    Info.ErrorFlags = (UInt32)v;
  if (GetArcUInt64(kpidWarningFlags, v))
    Info.WarningFlags = (UInt32)v;
  GetArcString(kpidError, Info.Error);
  GetArcString(kpidWarning, Info.Warning);
}

void CArchiveSession::GetItemProperties(UInt32 index, CPropertyItemVector &list) const
{
  list.Clear();
  NSynchronization::CCriticalSectionLock lock(Gate);
  if (!Archive)
    return;
  UInt32 numProps = 0;
  if (Archive->GetNumberOfProperties(&numProps) != S_OK)
    return;
  for (UInt32 i = 0; i < numProps; i++)
  {
    CMyComBSTR name;
    PROPID propID;
    VARTYPE vt;
    if (Archive->GetPropertyInfo(i, &name, &propID, &vt) != S_OK)
      continue;
    NCOM::CPropVariant prop;
    if (Archive->GetProperty(index, propID, &prop) == S_OK && prop.vt != VT_EMPTY)
    {
      CPropertyItem &item = list.AddNew();
      item.Name = NPropertyFormatter::GetPropertyDisplayName(propID, name);
      item.Value = NPropertyFormatter::FormatValue(propID, prop);
    }
  }
}

bool CArchiveSession::CanUnwrapTarball() const
{
  if (!Format || Entries.Size() != 1)
    return false;
  static const char * const kStreamFormats[] = { "gzip", "bzip2", "xz", "zstd", "lzma", "z", "lz", "lz4" };
  bool isStream = false;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kStreamFormats); i++)
    if (Format->Name.IsEqualTo_Ascii_NoCase(kStreamFormats[i]))
      isStream = true;
  if (!isStream)
    return false;
  const UString name = Entries[0].GetName();
  return name.Len() >= 4 && StringsAreEqualNoCase_Ascii(name.RightPtr(4), ".tar");
}

HRESULT CArchiveSession::UnwrapTarball(const FString &tempFolder, CArchiveSession &inner,
    COperationContext &ctx, UString &errorMessage)
{
  errorMessage.Empty();
  if (!CanUnwrapTarball())
    return S_FALSE;
  const CArchiveEntry &entry = Entries[0];
  CRecordVector<UInt32> indices;
  indices.Add(entry.Index);
  CExtractOptions options;
  options.Destination = tempFolder;
  options.ZonePropagation = NZonePropagation::kOff;
  CExtractResult result;
  RINOK(NExtractEngine::Extract(*this, &indices, options, ctx, result, errorMessage))

  UStringVector components;
  NPathSanitizer::GetSafeComponents(entry.Path, components);
  if (components.IsEmpty())
    return E_FAIL;
  FString tarPath = tempFolder;
  if (!tarPath.IsEmpty() && !IS_PATH_SEPAR(tarPath.Back()))
    tarPath.Add_PathSepar();
  tarPath += us2fs(components.Back());

  CArchiveOpenOptions openOptions;
  openOptions.DisplayName = GetFileNamePart(fs2us(FilePath));
  openOptions.Parent = this;
  openOptions.PathInParent = entry.Path;
  return inner.Open(tarPath, openOptions, ctx, errorMessage);
}

// ---- property helpers ----

bool CArchiveSession::GetString(UInt32 index, PROPID propID, UString &value) const
{
  value.Empty();
  NCOM::CPropVariant prop;
  if (Archive->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return PropToString(prop, value);
}

bool CArchiveSession::GetBool(UInt32 index, PROPID propID, bool &value) const
{
  value = false;
  NCOM::CPropVariant prop;
  if (Archive->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return PropToBool(prop, value);
}

bool CArchiveSession::GetUInt64(UInt32 index, PROPID propID, UInt64 &value) const
{
  value = 0;
  NCOM::CPropVariant prop;
  if (Archive->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return PropToUInt64(prop, value);
}

bool CArchiveSession::GetTime(UInt32 index, PROPID propID, FILETIME &value) const
{
  NCOM::CPropVariant prop;
  value.dwLowDateTime = value.dwHighDateTime = 0;
  if (Archive->GetProperty(index, propID, &prop) != S_OK)
    return false;
  return PropToFileTime(prop, value);
}

bool CArchiveSession::GetArcUInt64(PROPID propID, UInt64 &value) const
{
  value = 0;
  NCOM::CPropVariant prop;
  if (Archive->GetArchiveProperty(propID, &prop) != S_OK)
    return false;
  return PropToUInt64(prop, value);
}

bool CArchiveSession::GetArcBool(PROPID propID, bool &value) const
{
  value = false;
  NCOM::CPropVariant prop;
  if (Archive->GetArchiveProperty(propID, &prop) != S_OK)
    return false;
  return PropToBool(prop, value);
}

bool CArchiveSession::GetArcString(PROPID propID, UString &value) const
{
  value.Empty();
  NCOM::CPropVariant prop;
  if (Archive->GetArchiveProperty(propID, &prop) != S_OK)
    return false;
  return PropToString(prop, value);
}
