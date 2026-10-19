// AppLaunch.cpp

#include "StdAfx.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4668) // 'NTDDI_WIN10_GA' is not defined as a preprocessor macro
#endif
#include <appmodel.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <ShObjIdl_core.h>

#include "AppLaunch.h"

#include "../../Common/MyCom.h"

#include "../../Windows/DLL.h"
#include "../../Windows/FileDir.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/ProcessUtils.h"

using namespace NWindows;
using namespace NFile;

// the command line limit is 32K chars; the app gets a list file long before that
static const size_t kMaxCommandLineArgs = 8000;

static void AddQuoted(UString &s, const UString &arg)
{
  s.Add_Char('\"');
  s += arg;
  s.Add_Char('\"');
}


bool GetPackageFamilyName(UString &name)
{
  name.Empty();
  WCHAR pfn[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
  UINT32 len = Z7_ARRAY_SIZE(pfn);
  if (::GetCurrentPackageFamilyName(&len, pfn) == ERROR_SUCCESS)
  {
    name = pfn;
    return true;
  }

  // Explorer loads us without package context: ...\WindowsApps\<PackageFullName>\SevenZip.ShellExtension.dll
  UString dir (fs2us(NDLL::GetModuleDirPrefix()));
  if (!dir.IsEmpty() && IS_PATH_SEPAR(dir.Back()))
    dir.DeleteBack();
  const UString fullName (dir.Ptr(dir.ReverseFind_PathSepar() + 1));
  len = Z7_ARRAY_SIZE(pfn);
  if (::PackageFamilyNameFromFullName(fullName, &len, pfn) == ERROR_SUCCESS)
  {
    name = pfn;
    return true;
  }
  return false;
}


UString GetAppExePath()
{
  UString path (fs2us(NDLL::GetModuleDirPrefix()));
  path += "SevenZip.exe";
  return path;
}


HRESULT BuildAppArguments(const char *verb, const UStringVector &paths, UString &args)
{
  args = verb;

  size_t total = 0;
  FOR_VECTOR (i, paths)
    total += paths[i].Len() + 3;

  if (total < kMaxCommandLineArgs)
  {
    FOR_VECTOR (i, paths)
    {
      args.Add_Space();
      AddQuoted(args, paths[i]);
    }
    return S_OK;
  }

  FString listPath;
  if (!NDir::MyGetTempPath(listPath))
    return GetLastError_noZero_HRESULT();
  listPath += "7zw";
  AString postfix;
  NIO::COutFile file;
  if (!NDir::CreateTempFile2(listPath, true, postfix, &file))
    return GetLastError_noZero_HRESULT();
  listPath += postfix;

  UString text (L"\xFEFF"); // BOM
  FOR_VECTOR (i, paths)
  {
    text += paths[i];
    text += "\r\n";
  }
  if (!file.WriteFull(text.Ptr(), text.Len() * sizeof(wchar_t)))
    return GetLastError_noZero_HRESULT();
  if (!file.Close())
    return GetLastError_noZero_HRESULT();

  UString listArg ('@');
  listArg += fs2us(listPath);
  args.Add_Space();
  AddQuoted(args, listArg);
  return S_OK;
}


HRESULT LaunchApp(const UString &args)
{
  UString pfn;
  if (GetPackageFamilyName(pfn) && !pfn.IsEmpty())
  {
    // With the AUMID the app runs with its package identity (notifications, settings, ...).
    CMyComPtr<IApplicationActivationManager> aam;
    HRESULT hr = aam.CoCreateInstance(CLSID_ApplicationActivationManager,
        IID_IApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER);
    if (SUCCEEDED(hr))
    {
      ::CoAllowSetForegroundWindow(aam, NULL);
      UString aumid (pfn);
      aumid += "!App";
      DWORD processId = 0;
      hr = aam->ActivateApplication(aumid, args, AO_NONE, &processId);
      if (SUCCEEDED(hr))
        return hr;
    }
  }

  // unpackaged (development) layout: SevenZip.exe is next to this DLL
  CProcess process;
  const WRes wres = process.Create(GetAppExePath(), args, NULL);
  if (wres != 0)
    return HRESULT_FROM_WIN32(wres);
  ::AllowSetForegroundWindow(::GetProcessId(process));
  return S_OK;
}
