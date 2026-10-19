// PlatformServices.cpp

#include "StdAfx.h"

#include <bcrypt.h>
#include <wincred.h>
#include <UserConsentVerifierInterop.h>

#include "PlatformServices.h"

#include "../../Common/IntToString.h"
#include "../../Common/MyBuffer.h"
#include "../../Common/StringConvert.h"
#include "../../Common/UTFConvert.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"

#include "../HString.h"

#include "ShellServices.h"

using namespace winrt;
using namespace NWindows;
using namespace NFile;

// ---------- CTaskbarProgress ----------

ITaskbarList3 *CTaskbarProgress::GetTaskbar()
{
  if (!_taskbar && !_failed)
  {
    if (FAILED(_taskbar.CoCreateInstance(CLSID_TaskbarList, IID_ITaskbarList3, NULL, CLSCTX_INPROC_SERVER))
        || FAILED(_taskbar->HrInit()))
    {
      _taskbar.Release();
      _failed = true;
    }
  }
  return _taskbar;
}

void CTaskbarProgress::Clear(HWND hwnd)
{
  ITaskbarList3 *tb = GetTaskbar();
  if (tb && hwnd)
    tb->SetProgressState(hwnd, TBPF_NOPROGRESS);
}

void CTaskbarProgress::Set(HWND hwnd, double fraction, bool paused, bool error)
{
  ITaskbarList3 *tb = GetTaskbar();
  if (!tb || !hwnd)
    return;
  tb->SetProgressState(hwnd, error ? TBPF_ERROR : paused ? TBPF_PAUSED : fraction < 0 ? TBPF_INDETERMINATE : TBPF_NORMAL);
  if (fraction >= 0)
    tb->SetProgressValue(hwnd, (ULONGLONG)(fraction * 10000), 10000);
}


// ---------- NEfficiencyMode ----------

namespace NEfficiencyMode {

static LONG g_Users;

static void Apply(bool on)
{
  PROCESS_POWER_THROTTLING_STATE state;
  memset(&state, 0, sizeof(state));
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  state.StateMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
  const HANDLE process = ::GetCurrentProcess();
  ::SetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state));
  ::SetPriorityClass(process, on ? IDLE_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}

void Enter()
{
  if (::InterlockedIncrement(&g_Users) == 1)
    Apply(true);
}

void Leave()
{
  if (::InterlockedDecrement(&g_Users) == 0)
    Apply(false);
}

}


// ---------- NNotifications ----------

namespace NNotifications {

using namespace winrt::Microsoft::Windows::AppNotifications;

static bool g_Registered;

void Register(const CInvokedHandler &onInvoked)
{
  try
  {
    AppNotificationManager manager = AppNotificationManager::Default();
    manager.NotificationInvoked([onInvoked](const AppNotificationManager &, const AppNotificationActivatedEventArgs &e)
    {
      onInvoked(e.Arguments());
    });
    manager.Register();
    g_Registered = true;
  }
  catch (...)
  {
    g_Registered = false;
  }
}

void Unregister()
{
  if (!g_Registered)
    return;
  g_Registered = false;
  try { AppNotificationManager::Default().Unregister(); }
  catch (...) {}
}

void ShowCompleted(const UString &title, const UString &body, const UString &revealPath, bool failed)
{
  if (!g_Registered)
    return;
  try
  {
    Builder::AppNotificationBuilder builder;
    builder.AddText(NWindows11::ToHString(title));
    builder.AddText(NWindows11::ToHString(body));
    if (!revealPath.IsEmpty() && !failed)
    {
      Builder::AppNotificationButton button(L"Show in folder");
      button.AddArgument(L"action", L"reveal");
      button.AddArgument(L"path", NWindows11::ToHString(revealPath));
      builder.AddButton(button);
    }
    AppNotificationManager::Default().Show(builder.BuildNotification());
  }
  catch (...)
  {
    // notifications are best-effort (e.g. turned off, or Do not disturb)
  }
}

}


// ---------- NShareService ----------

namespace NShareService {

using namespace winrt::Windows::ApplicationModel::DataTransfer;

static winrt::fire_and_forget Fill(DataRequest request, DataRequestDeferral deferral, UStringVector paths)
{
  try
  {
    std::vector<winrt::Windows::Storage::IStorageItem> items;
    FOR_VECTOR (i, paths)
    {
      const winrt::hstring p = NWindows11::ToHString(paths[i]);
      if (NFind::DoesDirExist_FollowLink(us2fs(paths[i])))
        items.push_back(co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(p));
      else
        items.push_back(co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(p));
    }
    request.Data().SetStorageItems(winrt::single_threaded_vector(std::move(items)));
  }
  catch (...) {}
  deferral.Complete();
}

void ShareFiles(HWND hwnd, const UStringVector &paths, const UString &title)
{
  if (paths.IsEmpty())
    return;
  try
  {
    const auto interop = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
    DataTransferManager manager { nullptr };
    winrt::check_hresult(interop->GetForWindow(hwnd, winrt::guid_of<DataTransferManager>(), winrt::put_abi(manager)));
    auto token = std::make_shared<winrt::event_token>();
    *token = manager.DataRequested([token, paths, title](const DataTransferManager &sender, const DataRequestedEventArgs &args)
    {
      sender.DataRequested(*token);
      const DataRequest request = args.Request();
      const DataRequestDeferral deferral = request.GetDeferral();
      request.Data().Properties().Title(NWindows11::ToHString(title));
      UString description;
      if (paths.Size() == 1)
      {
        const UString &p = paths[0];
        description = p.Ptr((unsigned)(p.ReverseFind_PathSepar() + 1));
      }
      else
      {
        description.Add_UInt32(paths.Size());
        description += " items";
      }
      request.Data().Properties().Description(NWindows11::ToHString(description));
      Fill(request, deferral, paths);
    });
    winrt::check_hresult(interop->ShowShareUIForWindow(hwnd));
  }
  catch (...) {}
}

}


// ---------- NPasswordStore ----------

namespace NPasswordStore {

static const wchar_t * const kPrefix = L"7-Zip/archive/";

// same key as the C# app: SHA-256 of the upper-case full path (UTF-8), first 16 bytes in hex
static UString KeyFor(const UString &archivePath)
{
  FString full;
  if (!NDir::MyGetFullPathName(us2fs(archivePath), full))
    full = us2fs(archivePath);
  UString upper = fs2us(full);
  if (!upper.IsEmpty())
  {
    UString temp;
    wchar_t *buf = temp.GetBuf(upper.Len());
    const int n = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, upper, (int)upper.Len(),
        buf, (int)upper.Len(), NULL, NULL, 0);
    temp.ReleaseBuf_SetEnd(n > 0 ? (unsigned)n : 0);
    if (n > 0)
      upper = temp;
  }
  AString utf8;
  ConvertUnicodeToUTF8(upper, utf8);
  Byte hash[32];
  memset(hash, 0, sizeof(hash));
  ::BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)(void *)utf8.GetBuf(), utf8.Len(), hash, sizeof(hash));
  UString key (kPrefix);
  for (unsigned i = 0; i < 16; i++)
  {
    const char *digits = "0123456789ABCDEF";
    key.Add_Char(digits[hash[i] >> 4]);
    key.Add_Char(digits[hash[i] & 15]);
  }
  return key;
}

static UString GetFileName(const UString &path)
{
  return UString(path.Ptr((unsigned)(path.ReverseFind_PathSepar() + 1)));
}

bool Has(const UString &archivePath)
{
  PCREDENTIALW cred = NULL;
  if (!::CredReadW(KeyFor(archivePath), CRED_TYPE_GENERIC, 0, &cred))
    return false;
  ::CredFree(cred);
  return true;
}

void Save(const UString &archivePath, const UString &password)
{
  UString target = KeyFor(archivePath);
  UString comment = GetFileName(archivePath);
  CREDENTIALW cred;
  memset(&cred, 0, sizeof(cred));
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = target.GetBuf();
  cred.Comment = comment.GetBuf();
  CByteBuffer blob(password.Len() * sizeof(wchar_t));
  if (password.Len() != 0)
    memcpy(blob, password.Ptr(), blob.Size());
  cred.CredentialBlob = blob;
  cred.CredentialBlobSize = (DWORD)blob.Size();
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  ::CredWriteW(&cred, 0);
  if (blob.Size() != 0)
    ::SecureZeroMemory(blob, blob.Size());
}

static UString ReadSecret(const UString &archivePath)
{
  UString s;
  PCREDENTIALW cred = NULL;
  if (!::CredReadW(KeyFor(archivePath), CRED_TYPE_GENERIC, 0, &cred))
    return s;
  s.SetFrom((const wchar_t *)(const void *)cred->CredentialBlob, cred->CredentialBlobSize / 2);
  ::CredFree(cred);
  return s;
}

winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> TryUnlockAsync(HWND hwnd, UString archivePath)
{
  using namespace winrt::Windows::Security::Credentials::UI;
  if (!Has(archivePath))
    co_return winrt::hstring();
  try
  {
    const UserConsentVerifierAvailability availability = co_await UserConsentVerifier::CheckAvailabilityAsync();
    if (availability == UserConsentVerifierAvailability::Available)
    {
      UString message (L"Unlock the saved password for ");
      message += GetFileName(archivePath);
      const auto interop = winrt::get_activation_factory<UserConsentVerifier, IUserConsentVerifierInterop>();
      winrt::Windows::Foundation::IAsyncOperation<UserConsentVerificationResult> op { nullptr };
      winrt::check_hresult(interop->RequestVerificationForWindowAsync(hwnd,
          reinterpret_cast<HSTRING>(winrt::get_abi(NWindows11::ToHString(message))),
          winrt::guid_of<winrt::Windows::Foundation::IAsyncOperation<UserConsentVerificationResult> >(),
          winrt::put_abi(op)));
      const UserConsentVerificationResult result = co_await op;
      if (result != UserConsentVerificationResult::Verified)
        co_return winrt::hstring();
    }
  }
  catch (...)
  {
    co_return winrt::hstring();
  }
  co_return NWindows11::ToHString(ReadSecret(archivePath));
}

void Forget(const UString &archivePath)
{
  ::CredDeleteW(KeyFor(archivePath), CRED_TYPE_GENERIC, 0);
}

unsigned ForgetAll()
{
  UString filter (kPrefix);
  filter += "*";
  DWORD count = 0;
  PCREDENTIALW *creds = NULL;
  if (!::CredEnumerateW(filter, 0, &count, &creds))
    return 0;
  UStringVector targets;
  for (DWORD i = 0; i < count; i++)
    targets.Add(UString(creds[i]->TargetName));
  ::CredFree(creds);
  FOR_VECTOR (i, targets)
    ::CredDeleteW(targets[i], CRED_TYPE_GENERIC, 0);
  return targets.Size();
}

}


// ---------- NJumpList ----------

namespace NJumpList {

winrt::fire_and_forget UpdateAsync()
{
  using namespace winrt::Windows::UI::StartScreen;
  try
  {
    if (!NShellActions::IsPackaged() || !JumpList::IsSupported())
      co_return;
    JumpList list = co_await JumpList::LoadCurrentAsync();
    list.SystemGroupKind(JumpListSystemGroupKind::Recent);
    list.Items().Clear();
    JumpListItem create = JumpListItem::CreateWithArguments(L"compress", L"Create archive");
    create.Logo(winrt::Windows::Foundation::Uri(L"ms-appx:///Assets/JumpCompress.png"));
    JumpListItem hash = JumpListItem::CreateWithArguments(L"hash", L"Verify checksums");
    hash.Logo(winrt::Windows::Foundation::Uri(L"ms-appx:///Assets/JumpHash.png"));
    list.Items().Append(create);
    list.Items().Append(hash);
    co_await list.SaveAsync();
  }
  catch (...) {}
}

}


// ---------- NTempFiles ----------

namespace NTempFiles {

static FString g_Root;
static FString g_Session;

static UString NewGuidHex()
{
  GUID g;
  memset(&g, 0, sizeof(g));
  ::CoCreateGuid(&g);
  const Byte *p = (const Byte *)&g;
  UString s;
  for (unsigned i = 0; i < 16; i++)
  {
    const char *digits = "0123456789abcdef";
    s.Add_Char(digits[p[i] >> 4]);
    s.Add_Char(digits[p[i] & 15]);
  }
  return s;
}

static void InitPaths()
{
  if (!g_Root.IsEmpty())
    return;
  NDir::MyGetTempPath(g_Root);
  if (g_Root.IsEmpty() || !IS_PATH_SEPAR(g_Root.Back()))
    g_Root.Add_PathSepar();
  g_Root += "7-Zip WinUI";
  g_Root.Add_PathSepar();
  // "<pid hex>-<guid>" cut to 20 chars, like the C# app
  char temp[16];
  ConvertUInt32ToHex(::GetCurrentProcessId(), temp);
  UString session (temp);
  session.Add_Char('-');
  session += NewGuidHex();
  session.DeleteFrom(20);
  g_Session = g_Root;
  g_Session += us2fs(session);
}

FString NewFolder(const UString &hint)
{
  InitPaths();
  FString dir (g_Session);
  dir.Add_PathSepar();
  UString name = NewGuidHex();
  name.DeleteFrom(8);
  dir += us2fs(name);
  if (!hint.IsEmpty())
  {
    dir.Add_PathSepar();
    dir += us2fs(hint);
  }
  NDir::CreateComplexDir(dir);
  return dir;
}

static UInt64 MeasureDir(const FString &dir)
{
  UInt64 total = 0;
  NFind::CEnumerator enumerator;
  enumerator.SetDirPrefix(dir);
  NFind::CFileInfo fi;
  bool found;
  while (enumerator.Next(fi, found) && found)
  {
    if (fi.IsDir())
    {
      FString sub (dir);
      sub += fi.Name;
      sub.Add_PathSepar();
      total += MeasureDir(sub);
    }
    else
      total += fi.Size;
  }
  return total;
}

UInt64 MeasureAll()
{
  InitPaths();
  if (!NFind::DoesDirExist(g_Root))
    return 0;
  return MeasureDir(g_Root);
}

void CleanUp(bool everything)
{
  InitPaths();
  if (!NFind::DoesDirExist(g_Root))
    return;
  FILETIME now;
  ::GetSystemTimeAsFileTime(&now);
  const UInt64 nowTicks = ((UInt64)now.dwHighDateTime << 32) | now.dwLowDateTime;
  const UInt64 kDay = (UInt64)24 * 60 * 60 * 10000000;

  FStringVector remove;
  {
    NFind::CEnumerator enumerator;
    enumerator.SetDirPrefix(g_Root);
    NFind::CFileInfo fi;
    bool found;
    while (enumerator.Next(fi, found) && found)
    {
      if (!fi.IsDir())
        continue;
      FString path (g_Root);
      path += fi.Name;
      const bool mine = path.IsEqualTo_NoCase(g_Session);
      const UInt64 t = ((UInt64)fi.MTime.dwHighDateTime << 32) | fi.MTime.dwLowDateTime;
      const bool stale = t + kDay < nowTicks;
      if (mine || stale || everything)
        remove.Add(path);
    }
  }
  FOR_VECTOR (i, remove)
    NDir::RemoveDirWithSubItems(remove[i]);
}

}
