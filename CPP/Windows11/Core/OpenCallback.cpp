// OpenCallback.cpp

#include "StdAfx.h"

#include "OpenCallback.h"

#include "../../Common/ComTry.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/PropVariant.h"

#include "../../7zip/Common/FileStreams.h"

#include "PathSanitizer.h"
#include "SharedInFileStream.h"

using namespace NWindows;
using namespace NFile;

void COpenCallback::Init(const FString &archivePath, COperationContext *ctx, bool password_Defined, const UString &password)
{
  _ctx = ctx;
  FString fullPath;
  if (!NDir::MyGetFullPathName(archivePath, fullPath))
    fullPath = archivePath;
  const int slash = fullPath.ReverseFind_PathSepar();
  _directory = fullPath.Left((unsigned)(slash + 1));
  _archiveName = fs2us(fullPath.Ptr((unsigned)(slash + 1)));
  _currentName = _archiveName;
  Password_Defined = password_Defined;
  Password = password;
}

Z7_COM7F_IMF(COpenCallback::SetTotal(const UInt64 *files, const UInt64 *bytes))
{
  if (bytes)
    _ctx->TotalBytes = *bytes;
  if (files)
    _ctx->TotalFiles = *files;
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(COpenCallback::SetCompleted(const UInt64 *files, const UInt64 *bytes))
{
  if (bytes)
    _ctx->CompletedBytes = *bytes;
  if (files)
    _ctx->CompletedFiles = *files;
  _ctx->Report();
  return _ctx->Checkpoint();
}

Z7_COM7F_IMF(COpenCallback::GetProperty(PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NCOM::CPropVariant prop;
  if (propID == kpidName)
    prop = _currentName;
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(COpenCallback::GetStream(const wchar_t *name, IInStream **inStream))
{
  COM_TRY_BEGIN
  *inStream = NULL;
  if (DisableVolumes)
    return S_FALSE;
  const UString volumeName = name;
  // Only accept plain file names in the archive's own folder.
  if (volumeName.Find(L'\\') >= 0 || volumeName.Find(L'/') >= 0 || volumeName.Find(L':') >= 0)
    return S_FALSE;
  const FString full = _directory + us2fs(volumeName);
  NFind::CFileInfo fi;
  if (!fi.Find(full) || fi.IsDir())
    return S_FALSE;
  CSharedInFileStream *spec = new CSharedInFileStream;
  CMyComPtr<IInStream> stream = spec;
  if (!spec->Open(full))
    return GetLastError_noZero_HRESULT();
  VolumePaths.Add(full);
  _currentName = volumeName;
  *inStream = stream.Detach();
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(COpenCallback::CryptoGetTextPassword(BSTR *password))
{
  COM_TRY_BEGIN
  *password = NULL;
  PasswordRequested = true;
  if (!Password_Defined)
  {
    HRESULT res = E_ABORT;
    if (_ctx->Prompts)
      res = _ctx->Prompts->RequestPassword(_archiveName, false, Password);
    if (res != S_OK)
    {
      PasswordCancelled = true;
      return E_ABORT;
    }
    Password_Defined = true;
  }
  return StringToBstr(Password, password);
  COM_TRY_END
}
