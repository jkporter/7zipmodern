// ExtractEngine.cpp

#include "StdAfx.h"

#include "ExtractEngine.h"

#include "../../Common/ComTry.h"
#include "../../Common/Defs.h"
#include "../../Common/MyCom.h"
#include "../../Common/Wildcard.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/FileName.h"

#include "../../7zip/Archive/IArchive.h"
#include "../../7zip/Common/FileStreams.h"
#include "../../7zip/IPassword.h"

#include "PathSanitizer.h"

using namespace NWindows;
using namespace NFile;

static const char * const kOfficeExtensions[] =
{
  "doc", "docx", "docm", "dot", "dotx", "dotm", "xls", "xlsx", "xlsm", "xlsb", "xlt", "xltx",
  "xltm", "xlam", "ppt", "pptx", "pptm", "pot", "potx", "potm", "ppam", "pps", "ppsx", "ppsm",
  "sldx", "sldm", "rtf", "pub", "vsd", "vsdx", "vsdm", "one", "mdb", "accdb"
};

static bool IsOfficeFile(const UString &path)
{
  UString ext = GetExtensionPart(path);
  if (ext.IsEmpty())
    return false;
  ext.DeleteFrontal(1);
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kOfficeExtensions); i++)
    if (ext.IsEqualTo_Ascii_NoCase(kOfficeExtensions[i]))
      return true;
  return false;
}

static UString LastErrorMessage()
{
  const DWORD error = ::GetLastError();
  UString s = NError::MyFormatMessage(error);
  s.Trim();
  return s;
}

struct CDeferredDir
{
  FString Path;
  bool MTime_Defined;
  bool CTime_Defined;
  bool Attrib_Defined;
  FILETIME MTime;
  FILETIME CTime;
  UInt32 Attrib;
};

struct CDeferredLink
{
  FString Path;
  UString Target;
};

Z7_CLASS_IMP_COM_2(
  CExtractCallback
  , IArchiveExtractCallback
  , ICryptoGetTextPassword
)
  Z7_IFACE_COM7_IMP(IProgress)

  CArchiveSession *_session;
  const CExtractOptions *_options;
  COperationContext *_ctx;
  bool _test;
  FString _root; // without the trailing separator
  UStringVector _baseComponents;
  CObjectVector<CDeferredDir> _directories;
  CObjectVector<CDeferredLink> _symlinks;
  FStringVector _topLevel;

  NOverwriteMode::EEnum _overwrite;
  COutFileStream *_fileSpec;
  CMyComPtr<ISequentialOutStream> _file;
  FString _currentPath;
  const CArchiveEntry *_currentEntry;
  Int32 _askMode;
  UInt32 _filesDone;
  UInt32 _folders;
  UInt32 _skipped;
  UInt32 _errors;
  UInt64 _bytes;

  bool ResolveTarget(const CArchiveEntry &entry, FString &target);
  HRESULT ResolveConflict(FString &target, const CArchiveEntry &entry, bool &skip);
  void WriteZoneIdentifier(const FString &path);
public:
  bool WrongPassword;
  bool PasswordCancelled;
  bool UserCancelled;

  CExtractCallback():
      _fileSpec(NULL),
      _currentEntry(NULL),
      _askMode(0),
      _filesDone(0),
      _folders(0),
      _skipped(0),
      _errors(0),
      _bytes(0),
      WrongPassword(false),
      PasswordCancelled(false),
      UserCancelled(false)
      {}

  void Init(CArchiveSession *session, const CExtractOptions *options, COperationContext *ctx, bool test);
  void CloseCurrent(bool aborted);
  // Runs after all data is written: links last (so they cannot redirect later writes), then folder times.
  void FinishDeferredWork();
  void GetResult(CExtractResult &result) const;
};

void CExtractCallback::Init(CArchiveSession *session, const CExtractOptions *options, COperationContext *ctx, bool test)
{
  _session = session;
  _options = options;
  _ctx = ctx;
  _test = test;
  _overwrite = options->Overwrite;
  if (!NDir::MyGetFullPathName(options->Destination, _root))
    _root = options->Destination;
  while (_root.Len() > 3 && IS_PATH_SEPAR(_root.Back()))
    _root.DeleteBack();
  _baseComponents.Clear();
  if (!options->BasePath.IsEmpty())
  {
    UStringVector parts;
    SplitPathToParts(options->BasePath, parts);
    FOR_VECTOR (i, parts)
      if (!parts[i].IsEmpty())
        _baseComponents.Add(parts[i]);
  }
}

// ---- IProgress ----

Z7_COM7F_IMF(CExtractCallback::SetTotal(UInt64 total))
{
  _ctx->TotalBytes = total;
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(CExtractCallback::SetCompleted(const UInt64 *completeValue))
{
  if (completeValue)
    _ctx->CompletedBytes = *completeValue;
  _ctx->Report();
  return _ctx->Checkpoint();
}

// ---- IArchiveExtractCallback ----

Z7_COM7F_IMF(CExtractCallback::GetStream(UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode))
{
  COM_TRY_BEGIN
  *outStream = NULL;
  RINOK(_ctx->Checkpoint())

  _currentEntry = index < _session->Entries.Size() ? &_session->Entries[index] : NULL;
  _currentPath.Empty();
  _ctx->SetCurrentItem(_currentEntry ? _currentEntry->Path : UString());

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract || _test || !_currentEntry)
    return S_OK;

  const CArchiveEntry &entry = *_currentEntry;
  if (entry.IsAltStream && !_options->ExtractAlternateStreams)
  {
    _skipped++;
    return S_OK;
  }

  FString target;
  if (!ResolveTarget(entry, target))
  {
    _skipped++;
    return S_OK;
  }

  if (entry.IsDirectory)
  {
    if (!NDir::CreateComplexDir(target))
    {
      _errors++;
      _ctx->AddMessage(NMessageSeverity::kError, LastErrorMessage(), entry.Path);
      return S_OK;
    }
    CDeferredDir &d = _directories.AddNew();
    d.Path = target;
    d.MTime_Defined = entry.Modified_Defined;
    d.MTime = entry.Modified;
    d.CTime_Defined = entry.Created_Defined;
    d.CTime = entry.Created;
    d.Attrib_Defined = entry.Attributes_Defined;
    d.Attrib = entry.Attributes;
    _folders++;
    return S_OK;
  }

  {
    const int slash = target.ReverseFind_PathSepar();
    if (slash > 0 && !NDir::CreateComplexDir(target.Left((unsigned)slash)))
    {
      _errors++;
      _ctx->AddMessage(NMessageSeverity::kError, LastErrorMessage(), entry.Path);
      return S_OK;
    }
  }

  if (NFind::DoesFileOrDirExist(target))
  {
    bool skip = false;
    const HRESULT res = ResolveConflict(target, entry, skip);
    if (res != S_OK)
      return res;
    if (skip)
    {
      _skipped++;
      return _ctx->IsCancelled() ? E_ABORT : S_OK;
    }
  }

  _fileSpec = new COutFileStream;
  _file = _fileSpec;
  if (!_fileSpec->Create_ALWAYS(target))
  {
    const UString message = LastErrorMessage();
    _file.Release();
    _fileSpec = NULL;
    // Skip this item but keep going, like 7-Zip does for per-file write errors.
    _errors++;
    _ctx->AddMessage(NMessageSeverity::kError, message, entry.Path);
    return S_OK;
  }
  if (entry.Size > 0 && entry.Size < ((UInt64)1 << 40))
    _fileSpec->File.SetLength_KeepPosition(entry.Size);
  _currentPath = target;
  *outStream = _file;
  (*outStream)->AddRef();
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CExtractCallback::PrepareOperation(Int32 askExtractMode))
{
  _askMode = askExtractMode;
  return S_OK;
}

static const char *DescribeResult(Int32 code, bool encrypted)
{
  using namespace NArchive::NExtract::NOperationResult;
  switch (code)
  {
    case kUnsupportedMethod: return "Unsupported compression method.";
    case kDataError: return encrypted ? "Data error in encrypted file. Wrong password?" : "Data error.";
    case kCRCError: return encrypted ? "CRC failed in encrypted file. Wrong password?" : "CRC failed.";
    case kUnavailable: return "Unavailable data.";
    case kUnexpectedEnd: return "Unexpected end of data.";
    case kDataAfterEnd: return "There is some data after the end of the payload data.";
    case kIsNotArc: return "Is not an archive.";
    case kHeadersError: return "Headers error.";
    case kWrongPassword: return "Wrong password.";
    default: return NULL;
  }
}

Z7_COM7F_IMF(CExtractCallback::SetOperationResult(Int32 opRes))
{
  COM_TRY_BEGIN
  using namespace NArchive::NExtract::NOperationResult;
  const CArchiveEntry *entry = _currentEntry;
  CloseCurrent(false);

  if (opRes != kOK)
  {
    _errors++;
    const bool encrypted = (entry && entry->IsEncrypted) || _session->HeadersEncrypted;
    if (opRes == kWrongPassword || (encrypted && (opRes == kCRCError || opRes == kDataError)))
      WrongPassword = true;
    UString message;
    const char *s = DescribeResult(opRes, encrypted);
    if (s)
      message = s;
    else
    {
      message = "Error ";
      message.Add_UInt32((UInt32)opRes);
      message += ".";
    }
    _ctx->AddMessage(NMessageSeverity::kError, message, entry ? entry->Path : UString());
  }
  else if (entry && !entry->IsDirectory)
    _bytes += entry->Size;

  if (entry && !entry->IsDirectory)
  {
    _filesDone++;
    _ctx->CompletedFiles = _filesDone;
  }
  _ctx->Report();
  return _ctx->Checkpoint();
  COM_TRY_END
}

// ---- ICryptoGetTextPassword ----

Z7_COM7F_IMF(CExtractCallback::CryptoGetTextPassword(BSTR *password))
{
  COM_TRY_BEGIN
  *password = NULL;
  UString pwd;
  bool defined = false;
  if (_options->Password_Defined)
  {
    pwd = _options->Password;
    defined = true;
  }
  else if (_session->Password_Defined)
  {
    pwd = _session->Password;
    defined = true;
  }
  if (!defined && _ctx->Prompts)
  {
    if (_ctx->Prompts->RequestPassword(_session->DisplayName, false, pwd) == S_OK)
      defined = true;
  }
  if (!defined)
  {
    PasswordCancelled = true;
    return E_ABORT;
  }
  _session->Password = pwd;
  _session->Password_Defined = true;
  return StringToBstr(pwd, password);
  COM_TRY_END
}

// ---- helpers ----

bool CExtractCallback::ResolveTarget(const CArchiveEntry &entry, FString &target)
{
  UStringVector components;
  NPathSanitizer::GetSafeComponents(entry.Path, components, entry.IsAltStream);

  // Strip the folder being browsed so "Extract" from inside docs\2024 produces 2024's contents.
  unsigned skip = 0;
  if (!_baseComponents.IsEmpty())
  {
    while (skip < _baseComponents.Size() && skip < components.Size()
        && components[skip].IsEqualTo_NoCase(NPathSanitizer::CleanComponent(_baseComponents[skip])))
      skip++;
    if (skip < _baseComponents.Size())
      skip = 0;
  }
  UStringVector relative;
  for (unsigned i = skip; i < components.Size(); i++)
    relative.Add(components[i]);

  if (_options->PathMode == NExtractPathMode::kNoPaths)
  {
    if (entry.IsDirectory)
      return false;
    if (!relative.IsEmpty())
    {
      const UString last = relative.Back();
      relative.Clear();
      relative.Add(last);
    }
  }
  if (relative.IsEmpty())
    return false;

  if (!NPathSanitizer::CombineSafe(_root, relative, target))
  {
    _ctx->AddMessage(NMessageSeverity::kWarning,
        UString("Skipped an item whose path points outside the destination folder."), entry.Path);
    return false;
  }

  UString first = relative[0];
  {
    const int colon = first.Find(L':');
    if (colon >= 0)
      first.DeleteFrom((unsigned)colon);
  }
  FString top = _root;
  if (!IS_PATH_SEPAR(top.Back()))
    top.Add_PathSepar();
  top += us2fs(first);
  bool found = false;
  FOR_VECTOR (i, _topLevel)
    if (_topLevel[i].IsEqualTo_NoCase(top))
    {
      found = true;
      break;
    }
  if (!found)
    _topLevel.Add(top);
  return true;
}

HRESULT CExtractCallback::ResolveConflict(FString &target, const CArchiveEntry &entry, bool &skip)
{
  skip = false;
  NOverwriteMode::EEnum mode = _overwrite;
  if (mode == NOverwriteMode::kAsk)
  {
    if (!_ctx->Prompts)
      mode = NOverwriteMode::kOverwrite;
    else
    {
      COverwriteRequest request;
      request.TargetPath = target;
      request.ExistingSize = 0;
      request.ExistingModified_Defined = false;
      request.ExistingModified.dwLowDateTime = request.ExistingModified.dwHighDateTime = 0;
      NFind::CFileInfo fi;
      if (fi.Find(target))
      {
        if (!fi.IsDir())
          request.ExistingSize = fi.Size;
        request.ExistingModified = fi.MTime;
        request.ExistingModified_Defined = true;
      }
      request.NewSize = entry.Size;
      request.NewModified_Defined = entry.Modified_Defined;
      request.NewModified = entry.Modified;

      NOverwriteChoice::EEnum choice = NOverwriteChoice::kCancel;
      if (_ctx->Prompts->ResolveOverwrite(request, choice) != S_OK)
        choice = NOverwriteChoice::kCancel;
      switch (choice)
      {
        case NOverwriteChoice::kReplace: mode = NOverwriteMode::kOverwrite; break;
        case NOverwriteChoice::kReplaceAll: mode = _overwrite = NOverwriteMode::kOverwrite; break;
        case NOverwriteChoice::kSkip: mode = NOverwriteMode::kSkip; break;
        case NOverwriteChoice::kSkipAll: mode = _overwrite = NOverwriteMode::kSkip; break;
        case NOverwriteChoice::kKeepBoth: mode = NOverwriteMode::kRenameNew; break;
        case NOverwriteChoice::kKeepBothAll: mode = _overwrite = NOverwriteMode::kRenameNew; break;
        case NOverwriteChoice::kCancel:
        default:
          _ctx->AddMessage(NMessageSeverity::kInfo, UString("Cancelled."));
          UserCancelled = true;
          return E_ABORT;
      }
    }
  }

  switch (mode)
  {
    case NOverwriteMode::kSkip:
      skip = true;
      return S_OK;
    case NOverwriteMode::kRenameNew:
      target = NPathSanitizer::MakeUnique(target);
      return S_OK;
    case NOverwriteMode::kRenameExisting:
      if (!NDir::MyMoveFile(target, NPathSanitizer::MakeUnique(target)))
      {
        _errors++;
        _ctx->AddMessage(NMessageSeverity::kError, LastErrorMessage(), entry.Path);
        skip = true;
      }
      return S_OK;
    case NOverwriteMode::kAsk:
    case NOverwriteMode::kOverwrite:
    default:
    {
      NFind::CFileInfo fi;
      if (fi.Find(target))
      {
        if (fi.IsDir())
        {
          _ctx->AddMessage(NMessageSeverity::kError, UString("A folder with the same name already exists."), entry.Path);
          skip = true;
          return S_OK;
        }
        if (fi.IsReadOnly())
          NDir::SetFileAttrib(target, fi.Attrib & ~(DWORD)FILE_ATTRIBUTE_READONLY);
      }
      return S_OK;
    }
  }
}

void CExtractCallback::CloseCurrent(bool aborted)
{
  COutFileStream *fileSpec = _fileSpec;
  CMyComPtr<ISequentialOutStream> file = _file;
  const FString path = _currentPath;
  const CArchiveEntry *entry = _currentEntry;
  _fileSpec = NULL;
  _file.Release();
  _currentPath.Empty();
  if (!fileSpec || path.IsEmpty())
    return;

  if (!aborted && entry)
  {
    // Engine might have reported a smaller size than preallocated (e.g. on data errors).
    UInt64 length = 0;
    if (fileSpec->File.GetLength(length) && length != fileSpec->ProcessedSize)
      fileSpec->File.SetLength(fileSpec->ProcessedSize);
    if (_options->RestoreTimestamps)
    {
      if (!fileSpec->File.SetTime(
          entry->Created_Defined ? &entry->Created : NULL, NULL,
          entry->Modified_Defined ? &entry->Modified : NULL))
        _ctx->AddMessage(NMessageSeverity::kWarning, LastErrorMessage(), entry->Path);
    }
  }
  fileSpec->Close();
  file.Release();

  if (aborted || !entry)
    return;

  WriteZoneIdentifier(path);
  if (_options->RestoreAttributes && entry->Attributes_Defined)
  {
    const DWORD keep = (DWORD)(entry->Attributes & 0xFFFF) & (FILE_ATTRIBUTE_READONLY
        | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE);
    if (keep != 0 && !NDir::SetFileAttrib(path, keep))
      _ctx->AddMessage(NMessageSeverity::kWarning, LastErrorMessage(), entry->Path);
  }
  if (_options->RestoreSymbolicLinks && !entry->SymbolicLink.IsEmpty())
  {
    CDeferredLink &link = _symlinks.AddNew();
    link.Path = path;
    link.Target = entry->SymbolicLink;
  }
}

void CExtractCallback::WriteZoneIdentifier(const FString &path)
{
  if (!_session->ZoneIdentifier_Defined || _options->ZonePropagation == NZonePropagation::kOff)
    return;
  if (path.Len() > 2 && path.Find(FTEXT(':'), 2) >= 0) // an alternate stream
    return;
  if (_options->ZonePropagation == NZonePropagation::kOfficeFilesOnly && !IsOfficeFile(fs2us(path)))
    return;
  NIO::COutFile file;
  if (!file.Create_ALWAYS(path + FTEXT(":Zone.Identifier"))
      || !file.WriteFull(_session->ZoneIdentifier, _session->ZoneIdentifier.Size()))
    _ctx->AddMessage(NMessageSeverity::kWarning, LastErrorMessage(), fs2us(path));
}

static int CompareDirsByLengthDown(void *const *a, void *const *b, void * /* param */)
{
  const unsigned la = ((const CDeferredDir *)*a)->Path.Len();
  const unsigned lb = ((const CDeferredDir *)*b)->Path.Len();
  return MyCompare(lb, la);
}

static bool IsRootedPath(const UString &s)
{
  // like .NET Path.IsPathRooted: "\x", "/x", "C:x", "C:\x", "\\server"
  if (s.IsEmpty())
    return false;
  if (s[0] == '\\' || s[0] == '/')
    return true;
  return s.Len() >= 2 && s[1] == ':';
}

void CExtractCallback::FinishDeferredWork()
{
  FString rootPrefix = _root;
  if (!IS_PATH_SEPAR(rootPrefix.Back()))
    rootPrefix.Add_PathSepar();

  FOR_VECTOR (i, _symlinks)
  {
    const CDeferredLink &link = _symlinks[i];
    UString target = link.Target;
    target.Replace(L'/', L'\\');
    const int slash = link.Path.ReverseFind_PathSepar();
    const FString linkDir = link.Path.Left((unsigned)(slash + 1));
    FString resolved;
    if (IsRootedPath(link.Target)
        || !NDir::MyGetFullPathName(linkDir + us2fs(target), resolved)
        || !IsString1PrefixedByString2_NoCase(resolved, rootPrefix))
    {
      UString message ("Symbolic link target '");
      message += link.Target;
      message += "' is outside the destination; kept as a regular file.";
      _ctx->AddMessage(NMessageSeverity::kWarning, message, fs2us(link.Path));
      continue;
    }
    NDir::DeleteFileAlways(link.Path);
    if (!::CreateSymbolicLinkW(link.Path, target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    {
      UString message ("Could not create symbolic link: ");
      message += LastErrorMessage();
      _ctx->AddMessage(NMessageSeverity::kWarning, message, fs2us(link.Path));
    }
  }

  if (!_options->RestoreTimestamps)
    return;
  _directories.Sort(CompareDirsByLengthDown, NULL);
  FOR_VECTOR (i, _directories)
  {
    // Folder metadata is best-effort.
    const CDeferredDir &d = _directories[i];
    NDir::SetDirTime(d.Path, d.CTime_Defined ? &d.CTime : NULL, NULL, d.MTime_Defined ? &d.MTime : NULL);
    if (_options->RestoreAttributes && d.Attrib_Defined)
    {
      const DWORD keep = (DWORD)(d.Attrib & 0xFFFF) & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
      if (keep != 0)
        NDir::SetFileAttrib(d.Path, FILE_ATTRIBUTE_DIRECTORY | keep);
    }
  }
}

void CExtractCallback::GetResult(CExtractResult &result) const
{
  result.Destination = _root;
  result.FilesProcessed = _filesDone;
  result.Folders = _folders;
  result.Skipped = _skipped;
  result.Errors = _errors;
  result.Bytes = _bytes;
  result.WrongPassword = WrongPassword;
  result.TopLevelPaths = _topLevel;
}


static int CompareUInt32(const UInt32 *a, const UInt32 *b, void * /* param */)
{
  return MyCompare(*a, *b);
}

namespace NExtractEngine {

static HRESULT Run(CArchiveSession &session, const CRecordVector<UInt32> *indices, const CExtractOptions &options,
    COperationContext &ctx, bool test, CExtractResult &result, UString &errorMessage)
{
  errorMessage.Empty();
  result = CExtractResult();
  if (!session.Archive)
  {
    errorMessage = "The archive is not open.";
    return E_FAIL;
  }
  if (!test && !NDir::CreateComplexDir(options.Destination))
  {
    const HRESULT res = GetLastError_noZero_HRESULT();
    errorMessage = "Cannot create folder '";
    errorMessage += fs2us(options.Destination);
    errorMessage += "': ";
    errorMessage += LastErrorMessage();
    return res;
  }

  CExtractCallback *callbackSpec = new CExtractCallback;
  CMyComPtr<IArchiveExtractCallback> callback = callbackSpec;
  callbackSpec->Init(&session, &options, &ctx, test);

  CRecordVector<UInt32> sorted;
  if (indices)
  {
    sorted = *indices;
    sorted.Sort(CompareUInt32, NULL);
    // remove duplicates
    unsigned k = 0;
    FOR_VECTOR (i, sorted)
      if (k == 0 || sorted[k - 1] != sorted[i])
        sorted[k++] = sorted[i];
    sorted.DeleteFrom(k);
  }

  UInt64 totalFiles = 0;
  if (!indices)
  {
    FOR_VECTOR (i, session.Entries)
      if (!session.Entries[i].IsDirectory)
        totalFiles++;
  }
  else
  {
    FOR_VECTOR (i, sorted)
      if (sorted[i] < session.Entries.Size() && !session.Entries[sorted[i]].IsDirectory)
        totalFiles++;
  }
  ctx.TotalFiles = totalFiles;

  HRESULT hr;
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(session.Gate);
    hr = session.Archive->Extract(
        (indices && !sorted.IsEmpty()) ? &sorted[0] : NULL,
        indices ? sorted.Size() : (UInt32)(Int32)-1,
        test ? 1 : 0, callback);
    callbackSpec->CloseCurrent(true);
  }

  if (!test)
    callbackSpec->FinishDeferredWork();

  if (hr == E_ABORT && (callbackSpec->PasswordCancelled || callbackSpec->UserCancelled))
  {
    callbackSpec->GetResult(result);
    return E_ABORT;
  }
  if (hr != S_OK)
  {
    callbackSpec->GetResult(result);
    FormatHResultMessage(hr, UString(test ? "Testing" : "Extracting"), errorMessage);
    return hr;
  }

  if (callbackSpec->WrongPassword)
  {
    session.Password_Defined = false;
    session.Password.Empty();
  }

  ctx.Report(true);
  callbackSpec->GetResult(result);
  return S_OK;
}

HRESULT Extract(CArchiveSession &session, const CRecordVector<UInt32> *indices, const CExtractOptions &options,
    COperationContext &ctx, CExtractResult &result, UString &errorMessage)
{
  return Run(session, indices, options, ctx, false, result, errorMessage);
}

HRESULT Test(CArchiveSession &session, const CRecordVector<UInt32> *indices,
    COperationContext &ctx, CExtractResult &result, UString &errorMessage)
{
  CExtractOptions options;
  NDir::MyGetTempPath(options.Destination);
  return Run(session, indices, options, ctx, true, result, errorMessage);
}

void CollectIndices(const CRecordVector<const CArchiveEntry *> &files,
    const CRecordVector<const CArchiveFolder *> &folders, CRecordVector<UInt32> &indices)
{
  indices.Clear();
  FOR_VECTOR (i, files)
    indices.Add(files[i]->Index);
  FOR_VECTOR (i, folders)
    folders[i]->EnumerateEntriesRecursive(indices);
  indices.Sort(CompareUInt32, NULL);
  unsigned k = 0;
  FOR_VECTOR (i, indices)
    if (k == 0 || indices[k - 1] != indices[i])
      indices[k++] = indices[i];
  indices.DeleteFrom(k);
}

}
