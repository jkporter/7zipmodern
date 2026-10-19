// SevenZipEngine.cpp

#include "StdAfx.h"

#include "SevenZipEngine.h"

#include "../../Windows/DLL.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/Synchronization.h"

using namespace NWindows;

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

namespace NSevenZipEngine {

static NSynchronization::CCriticalSection g_CS;
static NDLL::CLibrary g_Library;
static FString g_LoadedPath;
static volatile bool g_Loaded = false;

static Func_CreateObject f_CreateObject;
static Func_GetNumberOfFormats f_GetNumberOfFormats;
static Func_GetHandlerProperty2 f_GetHandlerProperty2;
static Func_GetHashers f_GetHashers;
static Func_GetNumberOfMethods f_GetNumberOfMethods;
static Func_GetMethodProperty f_GetMethodProperty;
static Func_GetModuleProp f_GetModuleProp;
static Func_GetIsArc f_GetIsArc;

static bool Bind(HMODULE m)
{
  f_CreateObject = Z7_GET_PROC_ADDRESS(Func_CreateObject, m, "CreateObject");
  f_GetNumberOfFormats = Z7_GET_PROC_ADDRESS(Func_GetNumberOfFormats, m, "GetNumberOfFormats");
  f_GetHandlerProperty2 = Z7_GET_PROC_ADDRESS(Func_GetHandlerProperty2, m, "GetHandlerProperty2");
  f_GetHashers = Z7_GET_PROC_ADDRESS(Func_GetHashers, m, "GetHashers");
  f_GetNumberOfMethods = Z7_GET_PROC_ADDRESS(Func_GetNumberOfMethods, m, "GetNumberOfMethods");
  f_GetMethodProperty = Z7_GET_PROC_ADDRESS(Func_GetMethodProperty, m, "GetMethodProperty");
  // optional exports:
  f_GetModuleProp = Z7_GET_PROC_ADDRESS(Func_GetModuleProp, m, "GetModuleProp");
  f_GetIsArc = Z7_GET_PROC_ADDRESS(Func_GetIsArc, m, "GetIsArc");
  return f_CreateObject && f_GetNumberOfFormats && f_GetHandlerProperty2
      && f_GetHashers && f_GetNumberOfMethods && f_GetMethodProperty;
}

HRESULT EnsureLoaded(const FString &explicitPath, UString &errorMessage)
{
  if (g_Loaded)
    return S_OK;
  NSynchronization::CCriticalSectionLock lock(g_CS);
  if (g_Loaded)
    return S_OK;

  FStringVector candidates;
  if (!explicitPath.IsEmpty())
    candidates.Add(explicitPath);
  const FString dir = NDLL::GetModuleDirPrefix();
  candidates.Add(dir + FTEXT("7z.dll"));
  candidates.Add(dir + FTEXT("Engine") FSTRING_PATH_SEPARATOR FTEXT("7z.dll"));

  FOR_VECTOR (i, candidates)
  {
    const FString &path = candidates[i];
    if (!NFile::NFind::DoesFileExist_FollowLink(path))
      continue;
    NDLL::CLibrary lib;
    if (!lib.Load(path))
      continue;
    if (!Bind(lib.Get_HMODULE()))
      continue;
    g_Library.Attach(lib.Detach());
    g_LoadedPath = path;
    g_Loaded = true;
    return S_OK;
  }
  errorMessage = "The 7-Zip engine (7z.dll) was not found next to the application.";
  return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
}

HRESULT EnsureLoaded()
{
  UString errorMessage;
  return EnsureLoaded(FString(), errorMessage);
}

bool IsLoaded()
{
  return g_Loaded;
}

FString GetLoadedPath()
{
  NSynchronization::CCriticalSectionLock lock(g_CS);
  return g_LoadedPath;
}

HRESULT GetNumberOfFormats(UInt32 &numFormats)
{
  numFormats = 0;
  RINOK(EnsureLoaded())
  return f_GetNumberOfFormats(&numFormats);
}

HRESULT GetHandlerProperty(UInt32 formatIndex, PROPID propID, NCOM::CPropVariant &value)
{
  value.Clear();
  RINOK(EnsureLoaded())
  return f_GetHandlerProperty2(formatIndex, propID, &value);
}

HRESULT GetNumberOfMethods(UInt32 &numMethods)
{
  numMethods = 0;
  RINOK(EnsureLoaded())
  return f_GetNumberOfMethods(&numMethods);
}

HRESULT GetMethodProperty(UInt32 index, PROPID propID, NCOM::CPropVariant &value)
{
  value.Clear();
  RINOK(EnsureLoaded())
  return f_GetMethodProperty(index, propID, &value);
}

bool GetEngineVersion(UInt32 &major, UInt32 &minor)
{
  major = minor = 0;
  if (EnsureLoaded() != S_OK || !f_GetModuleProp)
    return false;
  NCOM::CPropVariant prop;
  if (f_GetModuleProp(NModulePropID::kVersion, &prop) != S_OK || prop.vt != VT_UI4)
    return false;
  major = prop.ulVal >> 16;
  minor = prop.ulVal & 0xFFFF;
  return true;
}

bool ProbeIsArc(UInt32 formatIndex, const Byte *header, size_t size, UInt32 &result)
{
  result = k_IsArc_Res_NO;
  if (EnsureLoaded() != S_OK || !f_GetIsArc)
    return false;
  Func_IsArc isArc = NULL;
  if (f_GetIsArc(formatIndex, &isArc) != S_OK || !isArc)
    return false;
  result = isArc(header, size);
  return true;
}

HRESULT CreateObject(const GUID &classId, const GUID &iid, void **outObject)
{
  *outObject = NULL;
  RINOK(EnsureLoaded())
  RINOK(f_CreateObject(&classId, &iid, outObject))
  return *outObject ? S_OK : E_NOINTERFACE;
}

HRESULT CreateInArchive(const GUID &classId, CMyComPtr<IInArchive> &archive)
{
  archive.Release();
  return CreateObject(classId, IID_IInArchive, (void **)&archive);
}

HRESULT CreateOutArchive(const GUID &classId, CMyComPtr<IOutArchive> &archive)
{
  archive.Release();
  return CreateObject(classId, IID_IOutArchive, (void **)&archive);
}

HRESULT GetHashers(CMyComPtr<IHashers> &hashers)
{
  hashers.Release();
  RINOK(EnsureLoaded())
  RINOK(f_GetHashers(&hashers))
  return hashers ? S_OK : E_NOINTERFACE;
}

}
