// ArchiveWriter.cpp

#include "StdAfx.h"

#include "ArchiveWriter.h"

#include "../../Common/Defs.h"
#include "../../Common/IntToString.h"
#include "../../Common/Wildcard.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/FileName.h"
#include "../../Windows/TimeUtils.h"

#include "../../7zip/Common/FileStreams.h"
#include "../../7zip/Common/MultiOutStream.h"

#include "PathSanitizer.h"
#include "SevenZipEngine.h"

using namespace NWindows;
using namespace NFile;

static UString LastErrorText(DWORD error)
{
  UString s = NError::MyFormatMessage(error);
  s.Trim();
  return s;
}

// "dir\.name.<pid>.tmp", the temporary name used for atomic writes
static FString MakeTempPath(const FString &path)
{
  const int slash = path.ReverseFind_PathSepar();
  FString s = path.Left((unsigned)(slash + 1));
  s += FTEXT('.');
  s += path.Ptr((unsigned)(slash + 1));
  s += FTEXT('.');
  char pid[16];
  ConvertUInt32ToHex(::GetCurrentProcessId(), pid);
  s += us2fs(UString(pid));
  s += FTEXT(".tmp");
  return s;
}

static FString GetDirPrefix(const FString &path)
{
  const int slash = path.ReverseFind_PathSepar();
  return path.Left((unsigned)(slash + 1));
}

static void TryDelete(const FString &path)
{
  NDir::DeleteFileAlways(path); // best effort
}

static bool MoveReplace(const FString &from, const FString &to)
{
  return BOOLToBool(::MoveFileExW(fs2us(from), fs2us(to), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED));
}

static bool EndsWith_NoCase(const UString &s, const char *suffix)
{
  const unsigned len = MyStringLen(suffix);
  return s.Len() >= len && StringsAreEqualNoCase_Ascii(s.RightPtr(len), suffix);
}

static void GetCurrentFileTime(FILETIME &ft)
{
  NTime::GetCurUtcFileTime(ft);
}


// Case-insensitive path -> item index lookup (the C# Dictionary with OrdinalIgnoreCase).
class CPathIndex
{
  const CObjectVector<CUpdateItem> *_items;
  CUIntVector _sorted;

  int Find(const UString &path, unsigned &insertPos) const
  {
    unsigned left = 0, right = _sorted.Size();
    while (left != right)
    {
      const unsigned mid = (left + right) / 2;
      const int cmp = MyStringCompareNoCase(path, (*_items)[_sorted[mid]].ArchivePath);
      if (cmp == 0)
      {
        insertPos = mid;
        return (int)_sorted[mid];
      }
      if (cmp < 0)
        right = mid;
      else
        left = mid + 1;
    }
    insertPos = left;
    return -1;
  }
public:
  CPathIndex(const CObjectVector<CUpdateItem> *items): _items(items) {}
  int Find(const UString &path) const
  {
    unsigned pos;
    return Find(path, pos);
  }
  // returns false if (path) was already there
  bool Insert(unsigned itemIndex, int &existing)
  {
    unsigned pos;
    existing = Find((*_items)[itemIndex].ArchivePath, pos);
    if (existing >= 0)
      return false;
    _sorted.Insert(pos, itemIndex);
    return true;
  }
};

// "add or replace" keeping the order of first insertion
static void AddOrReplace(CObjectVector<CUpdateItem> &list, CPathIndex &index, const CUpdateItem &item)
{
  list.Add(item);
  int existing;
  if (!index.Insert(list.Size() - 1, existing))
  {
    list[(unsigned)existing] = item;
    list.DeleteBack();
  }
}


static int CompareUInt32(const UInt32 *a, const UInt32 *b, void * /* param */)
{
  return MyCompare(*a, *b);
}

namespace NArchiveWriter {

static HRESULT WriteArchive(const FString &path, bool volumeSize_Defined, UInt64 volumeSize, IOutArchive *outArchive,
    const CObjectVector<CUpdateItem> &items, const CCompressionSettings &settings, COperationContext &ctx,
    CArchiveSession *source, bool atomicRename, CCompressResult &result, UString &errorMessage)
{
  NDir::CreateComplexDir(GetDirPrefix(path));

  CUpdateCallback *callbackSpec = new CUpdateCallback;
  CMyComPtr<IArchiveUpdateCallback> callback = callbackSpec;
  callbackSpec->Init(&items, &ctx, settings.Password, source, settings.StoreCreationTime, settings.StoreAccessTime);

  CMyComPtr<IOutStream> outStream;
  COutFileStream *fileSpec = NULL;
  CMultiOutStream *volSpec = NULL;
  FString tempPath;

  if (volumeSize_Defined && volumeSize > 0)
  {
    if (volumeSize < 1024)
    {
      errorMessage = "Volumes must be at least 1 KB.";
      return E_INVALIDARG;
    }
    volSpec = new CMultiOutStream;
    outStream = volSpec;
    volSpec->Prefix = path;
    volSpec->Prefix.Add_Dot();
    CRecordVector<UInt64> sizes;
    sizes.Add(volumeSize);
    volSpec->Init(sizes);
    callbackSpec->OutVolSpec = volSpec;
  }
  else
  {
    tempPath = atomicRename ? MakeTempPath(path) : path;
    fileSpec = new COutFileStream;
    outStream = fileSpec;
    if (!fileSpec->Create_ALWAYS(tempPath))
    {
      const DWORD error = ::GetLastError();
      errorMessage = "Cannot create '";
      errorMessage += fs2us(tempPath);
      errorMessage += "': ";
      errorMessage += LastErrorText(error);
      return error == 0 ? E_FAIL : HRESULT_FROM_WIN32(error);
    }
    callbackSpec->OutFileSpec = fileSpec;
  }

  HRESULT hr = outArchive->UpdateItems(outStream, items.Size(), callback);

  UInt64 finalLength = 0;
  FStringVector outputs;
  if (fileSpec)
  {
    if (hr == S_OK)
      fileSpec->File.GetLength(finalLength);
    const HRESULT res = fileSpec->Close();
    if (hr == S_OK)
      hr = res;
  }
  else
  {
    if (hr == S_OK)
    {
      unsigned numVolumes = 0;
      hr = volSpec->FinalFlush_and_CloseFiles(numVolumes);
      finalLength = volSpec->GetSize();
      if (hr == S_OK)
      {
        volSpec->NeedDelete = false;
        for (unsigned i = 0; i < numVolumes; i++)
        {
          FString name = volSpec->Prefix;
          char s[16];
          ConvertUInt32ToString(i + 1, s);
          for (unsigned k = MyStringLen(s); k < 3; k++)
            name += FTEXT('0');
          name += us2fs(UString(s));
          outputs.Add(name);
        }
      }
    }
  }
  callbackSpec->OutFileSpec = NULL;
  callbackSpec->OutVolSpec = NULL;
  outStream.Release(); // CMultiOutStream deletes its volumes on release when NeedDelete is set

  if (hr != S_OK)
  {
    if (!tempPath.IsEmpty())
      TryDelete(tempPath);
    FormatHResultMessage(hr, UString("Compressing"), errorMessage);
    return hr;
  }

  if (!volSpec)
  {
    if (atomicRename && !MoveReplace(tempPath, path))
    {
      const DWORD error = ::GetLastError();
      TryDelete(tempPath);
      errorMessage = "Cannot replace '";
      errorMessage += fs2us(path);
      errorMessage += "': ";
      errorMessage += LastErrorText(error);
      return error == 0 ? E_FAIL : HRESULT_FROM_WIN32(error);
    }
    outputs.Add(path);
  }

  ctx.Report(true);
  result = CCompressResult();
  result.OutputFiles = outputs;
  result.OutputBytes = finalLength;
  FOR_VECTOR (i, items)
  {
    if (items[i].NewData)
      result.InputBytes += items[i].Size;
    if (!items[i].IsDirectory)
      result.Files++;
  }
  result.Skipped = callbackSpec->FilesSkipped;
  return S_OK;
}

static HRESULT CreateTarball(const FString &archivePath, CObjectVector<CUpdateItem> &items,
    const CCompressionSettings &settings, COperationContext &ctx, CCompressResult &result, UString &errorMessage)
{
  // tar.gz / tar.xz / tar.bz2: write the tar next to the target, then compress it as a single stream.
  const FString dir = GetDirPrefix(archivePath);
  UString tarName = GetFileNameWithoutExtension(fs2us(archivePath));
  if (!EndsWith_NoCase(tarName, ".tar"))
    tarName += ".tar";
  const FString tempTar = MakeTempPath(dir + us2fs(tarName));

  const char *streamFormat;
  if (settings.Format.IsEqualTo_Ascii_NoCase("tar.gz")) streamFormat = "gzip";
  else if (settings.Format.IsEqualTo_Ascii_NoCase("tar.xz")) streamFormat = "xz";
  else if (settings.Format.IsEqualTo_Ascii_NoCase("tar.bz2")) streamFormat = "bzip2";
  else
  {
    errorMessage = "Unsupported format: ";
    errorMessage += settings.Format;
    return E_NOTIMPL;
  }

  CCompressionSettings tarSettings = settings;
  tarSettings.Format = "tar";
  tarSettings.VolumeSize_Defined = false;
  tarSettings.Method.Empty();
  tarSettings.DictionarySize_Defined = false;
  tarSettings.WordSize_Defined = false;

  const CArchiveFormat *tarFormat = NFormatRegistry::Find(UString("tar"));
  if (!tarFormat)
  {
    errorMessage = "The engine has no 'tar' handler.";
    return E_NOTIMPL;
  }
  CMyComPtr<IOutArchive> tar;
  {
    const HRESULT res = NSevenZipEngine::CreateOutArchive(tarFormat->ClassId, tar);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("tar"), errorMessage);
      return res;
    }
  }
  UInt64 numFiles = 0;
  FOR_VECTOR (i, items)
    if (!items[i].IsDirectory)
      numFiles++;
  ctx.TotalFiles = numFiles;

  CCompressResult tarResult;
  HRESULT res = WriteArchive(tempTar, false, 0, tar, items, tarSettings, ctx, NULL, false, tarResult, errorMessage);
  if (res == S_OK)
  {
    NFind::CFileInfo fi;
    if (!fi.Find(tempTar))
    {
      res = GetLastError_noZero_HRESULT();
      FormatHResultMessage(res, fs2us(tempTar), errorMessage);
    }
    else
    {
      CObjectVector<CUpdateItem> single;
      CUpdateItem &item = single.AddNew();
      item.NewData = true;
      item.NewProps = true;
      item.ArchivePath = tarName;
      item.Size = fi.Size;
      item.Attributes = FILE_ATTRIBUTE_ARCHIVE;
      GetCurrentFileTime(item.Modified);
      item.Modified_Defined = true;
      item.SourcePath = tempTar;
      ctx.CompletedBytes = 0;
      ctx.OutputBytes = 0;
      CCompressionSettings streamSettings = settings;
      streamSettings.Format = streamFormat;
      CCompressResult final;
      res = CreateFromItems(archivePath, single, streamSettings, ctx, final, errorMessage);
      if (res == S_OK)
      {
        result = CCompressResult();
        result.OutputFiles = final.OutputFiles;
        result.InputBytes = tarResult.InputBytes;
        result.OutputBytes = final.OutputBytes;
        result.Files = tarResult.Files;
        result.Skipped = tarResult.Skipped;
      }
    }
  }
  TryDelete(tempTar);
  return res;
}

HRESULT CreateFromItems(const FString &archivePath, CObjectVector<CUpdateItem> &items,
    const CCompressionSettings &settings, COperationContext &ctx, CCompressResult &result, UString &errorMessage)
{
  errorMessage.Empty();
  if (settings.IsTarball())
    return CreateTarball(archivePath, items, settings, ctx, result, errorMessage);

  const CFormatCapabilities &caps = NFormatCapabilities::Get(settings.Format);
  if (caps.SingleFile)
  {
    int fileIndex = -1;
    unsigned numFiles = 0;
    FOR_VECTOR (i, items)
      if (!items[i].IsDirectory)
      {
        numFiles++;
        fileIndex = (int)i;
      }
    if (numFiles != 1)
    {
      errorMessage = caps.DisplayName;
      errorMessage += " can only hold one file. Choose a TAR-based format for folders or several files.";
      return E_INVALIDARG;
    }
    const CUpdateItem src = items[(unsigned)fileIndex];
    items.Clear();
    CUpdateItem &item = items.AddNew();
    item.NewData = true;
    item.NewProps = true;
    item.ArchivePath = GetFileNamePart(src.ArchivePath);
    item.Size = src.Size;
    item.Attributes = src.Attributes;
    item.Modified_Defined = src.Modified_Defined;
    item.Modified = src.Modified;
    item.SourcePath = src.SourcePath;
    item.SourceData = src.SourceData;
  }

  UInt64 numFiles = 0;
  FOR_VECTOR (i, items)
    if (!items[i].IsDirectory)
      numFiles++;
  ctx.TotalFiles = numFiles;

  const UString engineName = EngineFormatName(settings.Format);
  const CArchiveFormat *format = NFormatRegistry::Find(engineName);
  if (!format)
  {
    errorMessage = "The engine has no '";
    errorMessage += engineName;
    errorMessage += "' handler.";
    return E_NOTIMPL;
  }
  CMyComPtr<IOutArchive> outArchive;
  {
    const HRESULT res = NSevenZipEngine::CreateOutArchive(format->ClassId, outArchive);
    if (res != S_OK)
    {
      FormatHResultMessage(res, format->Name, errorMessage);
      return res;
    }
  }
  CEnginePropVector props;
  BuildProperties(settings, format->Name, false, props);
  {
    const HRESULT res = ApplyProperties(outArchive, props);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Compression options"), errorMessage);
      return res;
    }
  }
  return WriteArchive(archivePath, settings.VolumeSize_Defined, settings.VolumeSize, outArchive, items, settings,
      ctx, NULL, true, result, errorMessage);
}

HRESULT Create(const FString &archivePath, const FStringVector &sources, const CCompressionSettings &settings,
    COperationContext &ctx, CCompressResult &result, UString &errorMessage)
{
  errorMessage.Empty();
  FString fullPath;
  if (!NDir::MyGetFullPathName(archivePath, fullPath))
    fullPath = archivePath;
  {
    const HRESULT res = NFormatRegistry::Load(errorMessage);
    if (res != S_OK)
      return res;
  }
  CObjectVector<CUpdateItem> items;
  const HRESULT res = CollectSources(sources, UString(), settings, ctx, fullPath, items);
  if (res != S_OK)
  {
    FormatHResultMessage(res, UString("Reading the files to compress"), errorMessage);
    return res;
  }
  return CreateFromItems(fullPath, items, settings, ctx, result, errorMessage);
}

static bool IsUnderTarget(const UString &path, const UString &target)
{
  if (target.IsEmpty())
    return true;
  UString prefix = target;
  while (!prefix.IsEmpty() && prefix.Back() == '\\')
    prefix.DeleteBack();
  prefix += L'\\';
  return path.IsPrefixedBy_NoCase(prefix);
}

static UString ApplyRename(const UString &path, const CObjectVector<CRenamePair> &renames)
{
  FOR_VECTOR (i, renames)
  {
    const CRenamePair &r = renames[i];
    if (path == r.From)
      return CArchiveSession::NormalizePath(r.To);
    UString prefix = r.From;
    prefix += L'\\';
    if (path.IsPrefixedBy(prefix))
    {
      UString s = CArchiveSession::NormalizePath(r.To);
      s += path.Ptr(r.From.Len());
      return s;
    }
  }
  return path;
}

HRESULT Update(CArchiveSession &session, const CUpdatePlan &plan, const CCompressionSettings *settingsSrc,
    COperationContext &ctx, CCompressResult &result, UString &errorMessage)
{
  errorMessage.Empty();
  if (!session.IsOpen() || !session.CanUpdate())
  {
    errorMessage = session.Format ? session.Format->Name : UString("These");
    errorMessage += " archives can't be modified here.";
    return E_NOTIMPL;
  }

  // Keep the protection the archive already had.
  CCompressionSettings settings;
  if (settingsSrc)
    settings = *settingsSrc;
  else
    settings.Level = NCompressionLevel::kNormal;
  settings.Format = session.Format->Name;
  if (session.HasEncryptedItems && settings.Password.IsEmpty())
  {
    if (session.Password_Defined)
      settings.Password = session.Password;
    settings.EncryptFileNames = session.HeadersEncrypted;
  }

  CRecordVector<UInt32> deleted = plan.Delete;
  deleted.Sort(CompareUInt32, NULL);

  CObjectVector<CUpdateItem> collected;
  {
    const HRESULT res = CollectSources(plan.Add, plan.TargetFolder, settings, ctx, session.FilePath, collected);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Reading the files to add"), errorMessage);
      return res;
    }
  }
  FOR_VECTOR (i, plan.NewFolders)
  {
    CUpdateItem &item = collected.AddNew();
    item.NewData = true;
    item.NewProps = true;
    item.ArchivePath = CArchiveSession::NormalizePath(plan.NewFolders[i]);
    item.IsDirectory = true;
    item.Attributes = FILE_ATTRIBUTE_DIRECTORY;
    GetCurrentFileTime(item.Modified);
    item.Created = item.Modified;
    item.Modified_Defined = item.Created_Defined = true;
  }

  CObjectVector<CUpdateItem> added;
  CPathIndex addedIndex(&added);
  FOR_VECTOR (i, collected)
    AddOrReplace(added, addedIndex, collected[i]);
  CBoolVector addedRemoved;
  addedRemoved.ClearAndSetSize(added.Size());
  FOR_VECTOR (i, addedRemoved)
    addedRemoved[i] = false;

  CObjectVector<CUpdateItem> items;
  FOR_VECTOR (ei, session.Entries)
  {
    const CArchiveEntry &entry = session.Entries[ei];
    if (deleted.FindInSorted(entry.Index) >= 0)
      continue;

    bool replaced = false;
    FOR_VECTOR (ri, plan.Replace)
    {
      const CReplaceItem &r = plan.Replace[ri];
      if (r.Index != entry.Index)
        continue;
      NFind::CFileInfo fi;
      if (!fi.Find(r.Path))
      {
        const HRESULT res = GetLastError_noZero_HRESULT();
        errorMessage = "Cannot read '";
        errorMessage += fs2us(r.Path);
        errorMessage += "': ";
        errorMessage += NError::MyFormatMessage(res);
        return res;
      }
      CUpdateItem::FromDisk(fi, r.Path, entry.Path, items.AddNew());
      replaced = true;
      break;
    }
    if (replaced)
      continue;

    const UString renamed = ApplyRename(entry.Path, plan.Rename);

    const int ai = addedIndex.Find(renamed);
    if (ai >= 0 && !addedRemoved[(unsigned)ai])
    {
      const CUpdateItem &incoming = added[(unsigned)ai];
      bool replace = true;
      if (settings.UpdateMode == NUpdateMode::kUpdateAndAdd || settings.UpdateMode == NUpdateMode::kFreshen)
        replace = incoming.Modified_Defined && entry.Modified_Defined
            && ::CompareFileTime(&incoming.Modified, &entry.Modified) > 0;
      if (incoming.IsDirectory && entry.IsDirectory)
        replace = true;
      if (replace)
        continue;
      addedRemoved[(unsigned)ai] = true;
    }
    else if (settings.UpdateMode == NUpdateMode::kSynchronize && !plan.Add.IsEmpty()
        && IsUnderTarget(entry.Path, plan.TargetFolder))
      continue;

    if (renamed == entry.Path)
      CUpdateItem::Keep(entry, items.AddNew());
    else
      CUpdateItem::Rename(entry, renamed, items.AddNew());
  }

  FOR_VECTOR (i, added)
  {
    if (addedRemoved[i])
      continue;
    if (settings.UpdateMode == NUpdateMode::kFreshen)
    {
      bool exists = false;
      FOR_VECTOR (k, session.Entries)
        if (session.Entries[k].Path.IsEqualTo_NoCase(added[i].ArchivePath))
        {
          exists = true;
          break;
        }
      if (!exists)
        continue;
    }
    items.Add(added[i]);
  }

  UInt64 numFiles = 0;
  FOR_VECTOR (i, items)
    if (items[i].NewData && !items[i].IsDirectory)
      numFiles++;
  ctx.TotalFiles = numFiles;

  CMyComPtr<IOutArchive> outArchive;
  session.Archive.QueryInterface(IID_IOutArchive, &outArchive);
  if (!outArchive)
  {
    errorMessage = session.Format->Name;
    errorMessage += " handler can't write archives.";
    return E_NOTIMPL;
  }

  CEnginePropVector props;
  BuildProperties(settings, session.Format->Name, true, props);
  const FString finalPath = session.FilePath;
  const FString tempPath = MakeTempPath(finalPath);

  CCompressResult written;
  {
    NSynchronization::CCriticalSectionLock lock(session.Gate);
    HRESULT res = ApplyProperties(outArchive, props);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Compression options"), errorMessage);
      return res;
    }
    res = WriteArchive(tempPath, false, 0, outArchive, items, settings, ctx, &session, false, written, errorMessage);
    if (res != S_OK)
    {
      TryDelete(tempPath);
      return res;
    }
  }

  // Release the original before swapping it out.
  outArchive.Release();
  session.Close();
  if (!MoveReplace(tempPath, finalPath))
  {
    const DWORD error = ::GetLastError();
    errorMessage = "Cannot replace '";
    errorMessage += fs2us(finalPath);
    errorMessage += "': ";
    errorMessage += LastErrorText(error);
    TryDelete(tempPath);
    return error == 0 ? E_FAIL : HRESULT_FROM_WIN32(error);
  }
  result = CCompressResult();
  result.OutputFiles.Add(finalPath);
  result.InputBytes = written.InputBytes;
  result.OutputBytes = written.OutputBytes;
  result.Files = written.Files;
  result.Skipped = written.Skipped;
  return S_OK;
}

// ---- source enumeration ----

static bool IsExcluded(const FString &path, const FString &exclude)
{
  if (exclude.IsEmpty())
    return false;
  if (path.IsEqualTo_NoCase(exclude))
    return true;
  const UString p = fs2us(path);
  if (!EndsWith_NoCase(p, ".tmp"))
    return false;
  FString prefix = exclude;
  prefix.Add_Dot();
  if (IsString1PrefixedByString2_NoCase(path, prefix))
    return true;
  // our own temporary file for an atomic write of (exclude)
  FString tempPrefix = MakeTempPath(exclude);
  tempPrefix.DeleteFrom(tempPrefix.Len() - 4);
  {
    const int dot = tempPrefix.ReverseFind_Dot();
    if (dot >= 0)
      tempPrefix.DeleteFrom((unsigned)dot + 1);
  }
  return IsString1PrefixedByString2_NoCase(path, tempPrefix);
}

static bool IsDirLink(const FString &path)
{
  CByteBuffer buf;
  if (!NIO::GetReparseData(path, buf, NULL))
    return false;
  CReparseAttr attr;
  if (!attr.Parse(buf, buf.Size()))
    return false;
  return attr.IsSymLink_Win() || attr.IsMountPoint();
}

struct CDirToScan
{
  FString Dir;     // without the trailing separator
  UString Prefix;  // archive path of the folder
};

HRESULT CollectSources(const FStringVector &sources, const UString &archivePrefixSrc, const CCompressionSettings &settings,
    COperationContext &ctx, const FString &exclude, CObjectVector<CUpdateItem> &items)
{
  items.Clear();
  CPathIndex index(&items);
  const UString archivePrefix = CArchiveSession::NormalizePath(archivePrefixSrc);
  const bool fullPaths = (settings.PathMode == NCompressPathMode::kFull);

  FOR_VECTOR (si, sources)
  {
    RINOK(ctx.Checkpoint())
    FString full;
    if (!NDir::MyGetFullPathName(sources[si], full))
      full = sources[si];
    const unsigned rootLen = NName::GetRootPrefixSize(full);
    const FString root = full.Left(rootLen);
    while (full.Len() > rootLen && IS_PATH_SEPAR(full.Back()))
      full.DeleteBack();

    UString name;
    if (fullPaths)
      name = fs2us(full.Ptr(rootLen));
    else
    {
      name = GetFileNamePart(fs2us(full));
      if (name.IsEmpty())
      {
        name = fs2us(root);
        while (!name.IsEmpty() && (name.Back() == '\\' || name.Back() == ':'))
          name.DeleteBack();
      }
    }
    UString archiveName = name;
    if (!archivePrefix.IsEmpty())
    {
      archiveName = archivePrefix;
      archiveName += L'\\';
      archiveName += name;
    }

    NFind::CFileInfo fi;
    if (!fi.Find(full))
    {
      ctx.AddMessage(NMessageSeverity::kWarning, UString("The item no longer exists."), fs2us(full));
      continue;
    }
    if (!fi.IsDir())
    {
      if (!IsExcluded(full, exclude))
      {
        CUpdateItem item;
        CUpdateItem::FromDisk(fi, full, archiveName, item);
        AddOrReplace(items, index, item);
      }
      continue;
    }

    if (!name.IsEmpty())
    {
      // Full-path mode adds parent folders so the structure is explicit.
      if (fullPaths)
      {
        UStringVector parts;
        SplitPathToParts(name, parts);
        UString rel;
        for (unsigned i = 0; i + 1 < parts.Size(); i++)
        {
          if (i != 0)
            rel += L'\\';
          rel += parts[i];
          UString p = rel;
          if (!archivePrefix.IsEmpty())
          {
            p = archivePrefix;
            p += L'\\';
            p += rel;
          }
          if (index.Find(p) >= 0)
            continue;
          NFind::CFileInfo dirInfo;
          const FString dirPath = root + us2fs(rel);
          if (dirInfo.Find(dirPath))
          {
            CUpdateItem item;
            CUpdateItem::FromDisk(dirInfo, dirPath, p, item);
            AddOrReplace(items, index, item);
          }
        }
      }
      CUpdateItem item;
      CUpdateItem::FromDisk(fi, full, archiveName, item);
      AddOrReplace(items, index, item);
    }

    CObjectVector<CDirToScan> stack;
    {
      CDirToScan &d = stack.AddNew();
      d.Dir = full;
      d.Prefix = name.IsEmpty() ? archivePrefix : archiveName;
    }

    while (!stack.IsEmpty())
    {
      RINOK(ctx.Checkpoint())
      const CDirToScan cur = stack.Back();
      stack.DeleteBack();

      CObjectVector<NFind::CFileInfo> children;
      bool failed = false;
      {
        NFind::CEnumerator enumerator;
        FString dirPrefix = cur.Dir;
        if (!IS_PATH_SEPAR(dirPrefix.Back()))
          dirPrefix.Add_PathSepar();
        enumerator.SetDirPrefix(dirPrefix);
        for (;;)
        {
          NFind::CFileInfo child;
          bool found;
          if (!enumerator.Next(child, found))
          {
            failed = true;
            break;
          }
          if (!found)
            break;
          children.Add(child);
        }
      }
      if (failed)
      {
        ctx.AddMessage(NMessageSeverity::kWarning, LastErrorText(::GetLastError()), fs2us(cur.Dir));
        continue;
      }

      FOR_VECTOR (ci, children)
      {
        const NFind::CFileInfo &child = children[ci];
        UString childPath = cur.Prefix;
        if (!childPath.IsEmpty())
          childPath += L'\\';
        childPath += fs2us(child.Name);
        FString childFull = cur.Dir;
        if (!IS_PATH_SEPAR(childFull.Back()))
          childFull.Add_PathSepar();
        childFull += child.Name;
        if (child.IsDir())
        {
          CUpdateItem item;
          CUpdateItem::FromDisk(child, childFull, childPath, item);
          AddOrReplace(items, index, item);
          const bool isLink = child.HasReparsePoint() && IsDirLink(childFull);
          if (!isLink || settings.FollowLinks)
          {
            CDirToScan &d = stack.AddNew();
            d.Dir = childFull;
            d.Prefix = childPath;
          }
        }
        else if (!IsExcluded(childFull, exclude))
        {
          CUpdateItem item;
          CUpdateItem::FromDisk(child, childFull, childPath, item);
          AddOrReplace(items, index, item);
        }
      }
    }
  }
  return S_OK;
}

// ---- engine properties ----

UString EngineFormatName(const UString &format)
{
  if (format.IsEqualTo_Ascii_NoCase("gz"))
    return UString("gzip");
  if (format.IsEqualTo_Ascii_NoCase("bz2"))
    return UString("bzip2");
  UString s = format;
  s.MakeLower_Ascii();
  return s;
}

static void AddProp(CEnginePropVector &props, const char *name, const NCOM::CPropVariant &value)
{
  CEngineProp &p = props.AddNew();
  p.Name = name;
  p.Value = value;
}

static void AddProp(CEnginePropVector &props, const UString &name, const UString &value)
{
  CEngineProp &p = props.AddNew();
  p.Name = name;
  p.Value = value;
}

static UString BytesString(UInt64 v)
{
  UString s;
  s.Add_UInt64(v);
  s += L'b';
  return s;
}

void BuildProperties(const CCompressionSettings &s, const UString &engineFormat, bool updating, CEnginePropVector &props)
{
  props.Clear();
  UString fmt = engineFormat;
  fmt.MakeLower_Ascii();
  const bool is7z = fmt.IsEqualTo("7z");
  const bool isZip = fmt.IsEqualTo("zip");
  const bool isXz = fmt.IsEqualTo("xz");
  const bool isBZip2 = fmt.IsEqualTo("bzip2");
  const bool levelAware = is7z || isZip || fmt.IsEqualTo("gzip") || isBZip2 || isXz;

  if (levelAware && (!updating || s.Level != NCompressionLevel::kNormal || !s.Method.IsEmpty()))
    AddProp(props, "x", NCOM::CPropVariant((UInt32)s.Level));

  if (s.Level != NCompressionLevel::kStore || !is7z)
  {
    const bool ppmd = s.Method.IsEqualTo_Ascii_NoCase("PPMd");
    const UString prefix (is7z ? "0" : "");
    if (!s.Method.IsEmpty() && (is7z || isZip))
      AddProp(props, UString(is7z ? "0" : "m"), s.Method);
    if (s.DictionarySize_Defined && (is7z || isZip || isXz || isBZip2))
      AddProp(props, prefix + (ppmd ? L"mem" : L"d"), BytesString(s.DictionarySize));
    if (s.WordSize_Defined && (is7z || isZip))
    {
      CEngineProp &p = props.AddNew();
      p.Name = prefix + (ppmd ? L"o" : L"fb");
      p.Value = (UInt32)s.WordSize;
    }
  }

  if (is7z && s.SolidBlockSize_Defined)
  {
    UString v;
    if (s.SolidBlockSize == 0)
      v = "off";
    else if (s.SolidBlockSize == (UInt64)(Int64)-1)
      v = "on";
    else
      v = BytesString(s.SolidBlockSize);
    AddProp(props, UString("s"), v);
  }

  if (s.Threads_Defined && (is7z || isZip || isXz || isBZip2))
    AddProp(props, "mt", NCOM::CPropVariant((UInt32)(s.Threads < 1 ? 1 : s.Threads)));

  if (s.MemoryLimitPercent_Defined && levelAware)
  {
    UInt32 mem = s.MemoryLimitPercent;
    if (mem < 5) mem = 5;
    if (mem > 100) mem = 100;
    UString v;
    v.Add_UInt32(mem);
    v += L'%';
    AddProp(props, UString("memuse"), v);
  }

  if (!s.Password.IsEmpty())
  {
    if (is7z && s.EncryptFileNames)
      AddProp(props, UString("he"), UString("on"));
    if (isZip)
      AddProp(props, UString("em"), s.ZipEncryption.IsEmpty() ? UString("AES256") : s.ZipEncryption);
  }

  if (is7z)
  {
    if (s.StoreCreationTime) AddProp(props, UString("tc"), UString("on"));
    if (s.StoreAccessTime) AddProp(props, UString("ta"), UString("on"));
  }

  // "Parameters" box: tokens separated by ' ', ',' or ';', each "name" or "name=value"
  UString token;
  const UString &extra = s.ExtraParameters;
  for (unsigned i = 0;; i++)
  {
    const wchar_t c = extra[i];
    if (c == 0 || c == ' ' || c == ',' || c == ';' || c == '\t')
    {
      if (!token.IsEmpty())
      {
        const int eq = token.Find(L'=');
        CEngineProp &p = props.AddNew();
        if (eq < 0)
          p.Name = token;
        else
        {
          p.Name = token.Left((unsigned)eq);
          p.Value = UString(token.Ptr((unsigned)eq + 1));
        }
        token.Empty();
      }
      if (c == 0)
        break;
    }
    else
      token += c;
  }
}

HRESULT ApplyProperties(IUnknown *handler, const CEnginePropVector &props)
{
  if (props.IsEmpty())
    return S_OK;
  CMyComPtr<ISetProperties> setter;
  handler->QueryInterface(IID_ISetProperties, (void **)&setter);
  if (!setter)
    return S_OK;
  CRecordVector<const wchar_t *> names;
  // shallow copies: (props) keeps owning the BSTRs
  CRecordVector<PROPVARIANT> raw;
  FOR_VECTOR (i, props)
  {
    names.Add(props[i].Name);
    raw.Add(props[i].Value);
  }
  return setter->SetProperties(&names[0], &raw[0], names.Size());
}

}
