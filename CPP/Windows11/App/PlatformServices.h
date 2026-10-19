// PlatformServices.h

#ifndef ZIP7_INC_PLATFORM_SERVICES_H
#define ZIP7_INC_PLATFORM_SERVICES_H

#include <ShObjIdl_core.h>

#include "../../Common/MyCom.h"
#include "../../Common/MyString.h"

// Progress in the taskbar button (green / yellow / red bar), driven by the busiest operation.

class CTaskbarProgress
{
  CMyComPtr<ITaskbarList3> _taskbar;
  bool _failed;
  ITaskbarList3 *GetTaskbar();
public:
  CTaskbarProgress(): _failed(false) {}
  void Clear(HWND hwnd);
  // fraction < 0: indeterminate
  void Set(HWND hwnd, double fraction, bool paused = false, bool error = false);
};


/*
Windows 11 "Efficiency mode": EcoQoS + idle priority so long background
compressions stay off the performance cores and keep the PC responsive
(replaces 7-Zip's old "Background" priority button). Reference counted.
*/

namespace NEfficiencyMode
{
  void Enter();
  void Leave();
}


// Completion notifications with a "Show in folder" button (Windows App SDK app notifications).

namespace NNotifications
{
  typedef std::function<void(const winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> &arguments)>
      CInvokedHandler;
  // the handler is called on a background thread
  void Register(const CInvokedHandler &onInvoked);
  void Unregister();
  void ShowCompleted(const UString &title, const UString &body, const UString &revealPath, bool failed = false);
}


// Windows Share sheet (replaces 7-Zip's MAPI "Compress and email"). UI thread.

namespace NShareService
{
  void ShareFiles(HWND hwnd, const UStringVector &paths, const UString &title);
}


/*
Optional "remember password": stored in Windows Credential Manager, released
only after Windows Hello (face, fingerprint or PIN) verification when Hello is set up.
*/

namespace NPasswordStore
{
  bool Has(const UString &archivePath);
  void Save(const UString &archivePath, const UString &password);
  // returns "" if there is no saved password or the verification failed
  winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> TryUnlockAsync(HWND hwnd, UString archivePath);
  void Forget(const UString &archivePath);
  unsigned ForgetAll();
}


// Taskbar / Start jump list: tasks plus the system "Recent" group (packaged only).

namespace NJumpList
{
  winrt::fire_and_forget UpdateAsync();
}


// Per-session scratch space for "open file from archive", drag-out and nested archives.

namespace NTempFiles
{
  // creates and returns a new empty folder (with a sub-folder "hint" if not empty)
  FString NewFolder(const UString &hint = UString());
  UInt64 MeasureAll();
  // removes this session's files and leftovers of sessions that crashed (or everything)
  void CleanUp(bool everything = false);
}

#endif
