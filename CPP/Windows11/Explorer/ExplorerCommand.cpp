// ExplorerCommand.cpp

#include "StdAfx.h"

#include "ExplorerCommand.h"

#include "../../Common/ComTry.h"

#include "AppLaunch.h"
#include "ArchiveNames.h"
#include "MenuSettings.h"

extern
LONG g_DllRefCount;

static const CCommandInfo g_Commands[] =
{
  { "Open",          "open",           L"Open archive",                      NSelection::kSingleArchive, false },
  { "Extract",       "extract",        L"Extract files\x2026",               NSelection::kArchivesOnly,  false },
  { "ExtractHere",   "extract-here",   L"Extract here",                      NSelection::kArchivesOnly,  false },
  { "ExtractSmart",  "extract-smart",  L"Extract to \x201C%s\\\x201D",       NSelection::kArchivesOnly,  false },
  { "Test",          "test",           L"Test archive",                      NSelection::kArchivesOnly,  false },
  { "Compress",      "compress",       L"Add to archive\x2026",              NSelection::kAny,           true  },
  { "Compress7z",    "compress-7z",    L"Add to \x201C%s.7z\x201D",          NSelection::kAny,           false },
  { "CompressZip",   "compress-zip",   L"Add to \x201C%s.zip\x201D",         NSelection::kAny,           false },
  { "CompressShare", "compress-share", L"Compress and share\x2026",          NSelection::kAny,           false },
  { "Hash",          "hash",           L"Checksums (CRC, SHA)",              NSelection::kAny,           true  }
};


class CCoTaskWSTR
{
  LPWSTR _s;
  Z7_CLASS_NO_COPY(CCoTaskWSTR)
public:
  CCoTaskWSTR(): _s(NULL) {}
  ~CCoTaskWSTR() { ::CoTaskMemFree(_s); }
  LPWSTR *operator&() { return &_s; }
  operator LPCWSTR() const { return _s; }
};

static HRESULT CopyToCoTaskMem(const UString &s, LPWSTR *dest)
{
  const size_t size = ((size_t)s.Len() + 1) * sizeof(wchar_t);
  wchar_t *p = (wchar_t *)::CoTaskMemAlloc(size);
  *dest = p;
  if (!p)
    return E_OUTOFMEMORY;
  memcpy(p, s.Ptr(), size);
  return S_OK;
}

// Only file-system items have paths (not Control Panel, virtual library nodes, ...).
static void GetItemPaths(IShellItemArray *items, UStringVector &paths)
{
  paths.Clear();
  if (!items)
    return;
  DWORD numItems = 0;
  if (FAILED(items->GetCount(&numItems)))
    return;
  for (DWORD i = 0; i < numItems; i++)
  {
    CMyComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(i, &item)) || !item)
      continue;
    CCoTaskWSTR path;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && (LPCWSTR)path)
      paths.Add(UString((LPCWSTR)path));
  }
}

static bool AreAllArchives(const UStringVector &paths)
{
  FOR_VECTOR (i, paths)
    if (!IsArchivePath(paths[i]))
      return false;
  return true;
}


// ---------- CRootCommand ----------

CRootCommand::CRootCommand() { InterlockedIncrement(&g_DllRefCount); }
CRootCommand::~CRootCommand() { InterlockedDecrement(&g_DllRefCount); }

Z7_COM7F_IMF(CRootCommand::GetTitle(IShellItemArray * /* items */, LPWSTR *name))
{
  *name = NULL;
  COM_TRY_BEGIN
  return CopyToCoTaskMem(UString("7-Zip"), name);
  COM_TRY_END
}

Z7_COM7F_IMF(CRootCommand::GetIcon(IShellItemArray * /* items */, LPWSTR *icon))
{
  *icon = NULL;
  COM_TRY_BEGIN
  UString path (GetAppExePath());
  path += ",0";
  return CopyToCoTaskMem(path, icon);
  COM_TRY_END
}

Z7_COM7F_IMF(CRootCommand::GetToolTip(IShellItemArray * /* items */, LPWSTR *infoTip))
{
  *infoTip = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CRootCommand::GetCanonicalName(GUID *commandName))
{
  *commandName = CLSID_CSevenZipCommand;
  return S_OK;
}

Z7_COM7F_IMF(CRootCommand::GetState(IShellItemArray *items, BOOL /* okToBeSlow */, EXPCMDSTATE *state))
{
  COM_TRY_BEGIN
  UStringVector paths;
  GetItemPaths(items, paths);
  *state = paths.IsEmpty() ? ECS_HIDDEN : ECS_ENABLED;
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CRootCommand::Invoke(IShellItemArray * /* items */, IBindCtx * /* bindCtx */))
{
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CRootCommand::GetFlags(EXPCMDFLAGS *flags))
{
  *flags = ECF_HASSUBCOMMANDS;
  return S_OK;
}

Z7_COM7F_IMF(CRootCommand::EnumSubCommands(IEnumExplorerCommand **enumCommands))
{
  *enumCommands = NULL;
  COM_TRY_BEGIN
  CMenuSettings settings;
  settings.Read();

  CCommandEnumerator *enumSpec = new CCommandEnumerator;
  CMyComPtr<IEnumExplorerCommand> enumerator = enumSpec;

  for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_Commands); i++)
  {
    const CCommandInfo &info = g_Commands[i];
    if (!settings.IsOn(info.Id))
      continue;
    if (info.SeparatorBefore && !enumSpec->Commands.IsEmpty())
    {
      // the separator after the archive-only group is pointless when that group is hidden
      const bool onlyWithArchives =
          info.Needs == NSelection::kAny && StringsAreEqual_Ascii(info.Id, "Compress");
      enumSpec->Commands.AddNew() = new CSeparatorCommand(onlyWithArchives);
    }
    enumSpec->Commands.AddNew() = new CSubCommand(info);
  }

  *enumCommands = enumerator.Detach();
  return S_OK;
  COM_TRY_END
}


// ---------- CSubCommand ----------

CSubCommand::CSubCommand(const CCommandInfo &info): _info(info) { InterlockedIncrement(&g_DllRefCount); }
CSubCommand::~CSubCommand() { InterlockedDecrement(&g_DllRefCount); }

bool CSubCommand::Applies(const UStringVector &paths) const
{
  if (paths.IsEmpty())
    return false;
  switch (_info.Needs)
  {
    case NSelection::kSingleArchive:
      return paths.Size() == 1 && IsArchivePath(paths[0]);
    case NSelection::kArchivesOnly:
      return AreAllArchives(paths);
    case NSelection::kAny:
    default:
      return true;
  }
}

Z7_COM7F_IMF(CSubCommand::GetTitle(IShellItemArray *items, LPWSTR *name))
{
  *name = NULL;
  COM_TRY_BEGIN
  UString title (_info.Title);
  const int pos = title.Find(L"%s");
  if (pos >= 0)
  {
    UStringVector paths;
    GetItemPaths(items, paths);
    UString value;
    if (_info.Needs == NSelection::kArchivesOnly)
      value = (paths.Size() == 1) ? GetExtractFolderName(paths[0]) : UString("*");
    else
      value = GetAddArchiveName(paths);
    title.Delete((unsigned)pos, 2);
    title.Insert((unsigned)pos, ShortenMenuName(value));
  }
  return CopyToCoTaskMem(title, name);
  COM_TRY_END
}

// no per-item icons: the root item shows the app icon
Z7_COM7F_IMF(CSubCommand::GetIcon(IShellItemArray * /* items */, LPWSTR *icon))
{
  *icon = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CSubCommand::GetToolTip(IShellItemArray * /* items */, LPWSTR *infoTip))
{
  *infoTip = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CSubCommand::GetCanonicalName(GUID *commandName))
{
  *commandName = GUID_NULL;
  return S_OK;
}

Z7_COM7F_IMF(CSubCommand::GetState(IShellItemArray *items, BOOL /* okToBeSlow */, EXPCMDSTATE *state))
{
  COM_TRY_BEGIN
  UStringVector paths;
  GetItemPaths(items, paths);
  *state = Applies(paths) ? ECS_ENABLED : ECS_HIDDEN;
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CSubCommand::Invoke(IShellItemArray *items, IBindCtx * /* bindCtx */))
{
  COM_TRY_BEGIN
  UStringVector paths;
  GetItemPaths(items, paths);
  if (paths.IsEmpty())
    return S_OK;
  UString args;
  RINOK(BuildAppArguments(_info.Verb, paths, args))
  return LaunchApp(args);
  COM_TRY_END
}

Z7_COM7F_IMF(CSubCommand::GetFlags(EXPCMDFLAGS *flags))
{
  *flags = ECF_DEFAULT;
  return S_OK;
}

Z7_COM7F_IMF(CSubCommand::EnumSubCommands(IEnumExplorerCommand **enumCommands))
{
  *enumCommands = NULL;
  return E_NOTIMPL;
}


// ---------- CSeparatorCommand ----------

CSeparatorCommand::CSeparatorCommand(bool onlyWithArchives): _onlyWithArchives(onlyWithArchives)
  { InterlockedIncrement(&g_DllRefCount); }
CSeparatorCommand::~CSeparatorCommand() { InterlockedDecrement(&g_DllRefCount); }

Z7_COM7F_IMF(CSeparatorCommand::GetTitle(IShellItemArray * /* items */, LPWSTR *name))
{
  *name = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CSeparatorCommand::GetIcon(IShellItemArray * /* items */, LPWSTR *icon))
{
  *icon = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CSeparatorCommand::GetToolTip(IShellItemArray * /* items */, LPWSTR *infoTip))
{
  *infoTip = NULL;
  return E_NOTIMPL;
}

Z7_COM7F_IMF(CSeparatorCommand::GetCanonicalName(GUID *commandName))
{
  *commandName = GUID_NULL;
  return S_OK;
}

Z7_COM7F_IMF(CSeparatorCommand::GetState(IShellItemArray *items, BOOL /* okToBeSlow */, EXPCMDSTATE *state))
{
  COM_TRY_BEGIN
  bool show = true;
  if (_onlyWithArchives)
  {
    UStringVector paths;
    GetItemPaths(items, paths);
    show = !paths.IsEmpty() && AreAllArchives(paths);
  }
  *state = show ? ECS_ENABLED : ECS_HIDDEN;
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CSeparatorCommand::Invoke(IShellItemArray * /* items */, IBindCtx * /* bindCtx */))
{
  return S_OK;
}

Z7_COM7F_IMF(CSeparatorCommand::GetFlags(EXPCMDFLAGS *flags))
{
  *flags = ECF_ISSEPARATOR;
  return S_OK;
}

Z7_COM7F_IMF(CSeparatorCommand::EnumSubCommands(IEnumExplorerCommand **enumCommands))
{
  *enumCommands = NULL;
  return E_NOTIMPL;
}


// ---------- CCommandEnumerator ----------

CCommandEnumerator::CCommandEnumerator(): _index(0) { InterlockedIncrement(&g_DllRefCount); }
CCommandEnumerator::~CCommandEnumerator() { InterlockedDecrement(&g_DllRefCount); }

Z7_COM7F_IMF(CCommandEnumerator::Next(ULONG numCommands, IExplorerCommand **commands, ULONG *numFetched))
{
  ULONG n = 0;
  while (n < numCommands && _index < Commands.Size())
  {
    IExplorerCommand *command = Commands[_index++];
    command->AddRef();
    commands[n++] = command;
  }
  if (numFetched)
    *numFetched = n;
  return n == numCommands ? S_OK : S_FALSE;
}

Z7_COM7F_IMF(CCommandEnumerator::Skip(ULONG numCommands))
{
  const unsigned size = Commands.Size();
  if (numCommands > size - _index)
    _index = size;
  else
    _index += (unsigned)numCommands;
  return _index < size ? S_OK : S_FALSE;
}

Z7_COM7F_IMF(CCommandEnumerator::Reset())
{
  _index = 0;
  return S_OK;
}

Z7_COM7F_IMF(CCommandEnumerator::Clone(IEnumExplorerCommand **enumCommands))
{
  *enumCommands = NULL;
  return E_NOTIMPL;
}
