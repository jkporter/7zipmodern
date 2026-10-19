// DllExports.cpp

#include "StdAfx.h"

#include "../../Common/MyWindows.h"

#include <ShObjIdl_core.h>

#include "../../Common/MyInitGuid.h"

#include "../../Common/ComTry.h"

#include "ExplorerCommand.h"

extern
HINSTANCE g_hInstance;
HINSTANCE g_hInstance = NULL;

extern
LONG g_DllRefCount;
LONG g_DllRefCount = 0; // objects and server locks of this DLL

class CShellExtClassFactory Z7_final:
  public IClassFactory,
  public CMyUnknownImp
{
  Z7_COM_UNKNOWN_IMP_1_MT(IClassFactory)

  STDMETHOD(CreateInstance)(IUnknown *outer, REFIID riid, void **object) Z7_override Z7_final;
  STDMETHOD(LockServer)(BOOL lock) Z7_override Z7_final;
public:
   CShellExtClassFactory() { InterlockedIncrement(&g_DllRefCount); }
  ~CShellExtClassFactory() { InterlockedDecrement(&g_DllRefCount); }
};

Z7_COMWF_B CShellExtClassFactory::CreateInstance(IUnknown *outer, REFIID riid, void **object)
{
  *object = NULL;
  if (outer)
    return CLASS_E_NOAGGREGATION;
  COM_TRY_BEGIN
  CMyComPtr<IExplorerCommand> command = new CRootCommand;
  return command->QueryInterface(riid, object);
  COM_TRY_END
}

Z7_COMWF_B CShellExtClassFactory::LockServer(BOOL lock)
{
  if (lock)
    InterlockedIncrement(&g_DllRefCount);
  else
    InterlockedDecrement(&g_DllRefCount);
  return S_OK;
}


extern "C"
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID);

extern "C"
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH)
  {
    g_hInstance = hInstance;
    DisableThreadLibraryCalls(hInstance);
  }
  return TRUE;
}


STDAPI DllCanUnloadNow(void)
{
  return g_DllRefCount == 0 ? S_OK : S_FALSE;
}


STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
  *ppv = NULL;
  if (!IsEqualCLSID(rclsid, CLSID_CSevenZipCommand))
    return CLASS_E_CLASSNOTAVAILABLE;
  COM_TRY_BEGIN
  CMyComPtr<IClassFactory> factory = new CShellExtClassFactory;
  return factory->QueryInterface(riid, ppv);
  COM_TRY_END
}
