// SevenZip.ShellExtension.dll
//
// Windows 11 File Explorer context menu for 7-Zip (IExplorerCommand), replacing the classic
// IContextMenu handler in CPP/7zip/UI/Explorer. It is registered by the MSIX manifest
// (desktop4:FileExplorerContextMenus + com:SurrogateServer), so it shows in the modern top-level menu
// instead of under "Show more options", and needs no registry writes or admin rights.
//
// The handler only decides which commands to show and launches the app with a verb; all archive
// work happens in the app (which uses 7z.dll). It never loads 7z.dll into Explorer.

#include <windows.h>
#include <shlwapi.h>
#include <shobjidl_core.h>
#include <shlobj_core.h>
#include <appmodel.h>
#include <new>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

// {AB1904A6-9AFE-48AE-8A8C-B605FD3135F5} — must match Package.appxmanifest.
static const CLSID CLSID_SevenZipCommand = { 0xab1904a6, 0x9afe, 0x48ae, { 0x8a, 0x8c, 0xb6, 0x05, 0xfd, 0x31, 0x35, 0xf5 } };

static HMODULE g_module = nullptr;
static LONG g_objects = 0;
static LONG g_locks = 0;

// ---------------------------------------------------------------------------------------------
// Helpers

static std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L'\\');
    return slash == std::wstring::npos ? s : s.substr(0, slash);
}

/// Package family name of the package this DLL lives in (works inside and outside package context).
static std::wstring PackageFamilyName()
{
    wchar_t pfn[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    UINT32 len = ARRAYSIZE(pfn);
    if (GetCurrentPackageFamilyName(&len, pfn) == ERROR_SUCCESS)
        return pfn;

    // ...\WindowsApps\<PackageFullName>\SevenZip.ShellExtension.dll
    std::wstring dir = ModuleDirectory();
    std::wstring fullName = dir.substr(dir.find_last_of(L'\\') + 1);
    len = ARRAYSIZE(pfn);
    if (PackageFamilyNameFromFullName(fullName.c_str(), &len, pfn) == ERROR_SUCCESS)
        return pfn;
    return {};
}

/// Reads contextmenu.ini written by the app (Key=1/0 per line, UTF-16 LE with BOM).
class MenuSettings
{
public:
    MenuSettings()
    {
        wchar_t local[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local)))
            return;
        std::vector<std::wstring> candidates;
        std::wstring pfn = PackageFamilyName();
        if (!pfn.empty())
            candidates.push_back(std::wstring(local) + L"\\Packages\\" + pfn + L"\\LocalState\\contextmenu.ini");
        candidates.push_back(std::wstring(local) + L"\\7-Zip WinUI\\contextmenu.ini");
        for (auto& path : candidates)
            if (Load(path))
                break;
    }

    bool IsOn(const wchar_t* key) const
    {
        std::wstring needle = std::wstring(L"\n") + key + L"=";
        size_t pos = _text.find(needle);
        if (pos == std::wstring::npos)
            return true; // default: everything on
        return _text[pos + needle.size()] != L'0';
    }

private:
    bool Load(const std::wstring& path)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        char buffer[4096];
        DWORD read = 0;
        BOOL ok = ReadFile(h, buffer, sizeof(buffer) - 2, &read, nullptr);
        CloseHandle(h);
        if (!ok || read < 2)
            return false;
        const wchar_t* w = reinterpret_cast<const wchar_t*>(buffer);
        size_t count = read / sizeof(wchar_t);
        if (count > 0 && w[0] == 0xFEFF) { w++; count--; }
        _text = L"\n" + std::wstring(w, count);
        _text.erase(std::remove(_text.begin(), _text.end(), L'\r'), _text.end());
        return true;
    }

    std::wstring _text;
};

static const wchar_t* const kArchiveExtensions[] = {
    L".7z", L".zip", L".rar", L".tar", L".gz", L".tgz", L".bz2", L".tbz", L".tbz2", L".xz", L".txz", L".zst", L".tzst",
    L".lzma", L".lz", L".z", L".taz", L".cab", L".arj", L".lzh", L".lha", L".cpio", L".rpm", L".deb", L".iso", L".img",
    L".wim", L".swm", L".esd", L".vhd", L".vhdx", L".vmdk", L".vdi", L".qcow", L".qcow2", L".dmg", L".hfs", L".xar",
    L".pkg", L".squashfs", L".cramfs", L".apfs", L".ext", L".ext4", L".fat", L".ntfs", L".mbr", L".gpt", L".chm",
    L".msi", L".msix", L".appx", L".nupkg", L".jar", L".apk", L".001",
};

static bool IsArchivePath(const std::wstring& path)
{
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!ext || !*ext)
        return false;
    for (auto e : kArchiveExtensions)
        if (_wcsicmp(ext, e) == 0)
            return true;
    // .r00/.z01 style multi-part pieces are opened through their first part, so don't offer them.
    return false;
}

static std::vector<std::wstring> GetPaths(IShellItemArray* items)
{
    std::vector<std::wstring> paths;
    if (!items)
        return paths;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count)))
        return paths;
    for (DWORD i = 0; i < count; i++)
    {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item)))
            continue;
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        {
            paths.emplace_back(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    return paths;
}

static std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

/// Builds the command line; big selections go through a single-use @listfile in %TEMP%.
static std::wstring BuildArguments(const wchar_t* verb, const std::vector<std::wstring>& paths)
{
    std::wstring args = verb;
    size_t total = 0;
    for (auto& p : paths)
        total += p.size() + 3;
    if (total < 8000)
    {
        for (auto& p : paths)
            args += L" " + Quote(p);
        return args;
    }

    wchar_t tempDir[MAX_PATH], listFile[MAX_PATH];
    GetTempPathW(ARRAYSIZE(tempDir), tempDir);
    GetTempFileNameW(tempDir, L"7zw", 0, listFile);
    HANDLE h = CreateFileW(listFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        const wchar_t bom = 0xFEFF;
        DWORD written;
        WriteFile(h, &bom, sizeof(bom), &written, nullptr);
        for (auto& p : paths)
        {
            std::wstring line = p + L"\r\n";
            WriteFile(h, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
        }
        CloseHandle(h);
    }
    return args + L" " + Quote(std::wstring(L"@") + listFile);
}

/// Starts the app with package identity (via its AUMID) so it gets notifications, settings, etc.
static HRESULT LaunchApp(const std::wstring& arguments)
{
    std::wstring pfn = PackageFamilyName();
    if (!pfn.empty())
    {
        IApplicationActivationManager* aam = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&aam));
        if (SUCCEEDED(hr))
        {
            CoAllowSetForegroundWindow(aam, nullptr);
            DWORD pid = 0;
            hr = aam->ActivateApplication((pfn + L"!App").c_str(), arguments.c_str(), AO_NONE, &pid);
            aam->Release();
            if (SUCCEEDED(hr))
                return hr;
        }
    }

    // Unpackaged/dev layout: SevenZip.exe sits next to this DLL.
    std::wstring exe = ModuleDirectory() + L"\\SevenZip.exe";
    std::wstring cmd = Quote(exe) + L" " + arguments;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return HRESULT_FROM_WIN32(GetLastError());
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}

static HRESULT DupString(const std::wstring& s, LPWSTR* out) { return SHStrDupW(s.c_str(), out); }

static std::wstring StemOf(const std::wstring& path)
{
    std::wstring name = PathFindFileNameW(path.c_str());
    if (name.empty())
        name = path;
    // Strip up to two archive extensions: photos.tar.gz -> photos
    for (int i = 0; i < 2; i++)
    {
        const wchar_t* ext = PathFindExtensionW(name.c_str());
        if (!*ext || !(IsArchivePath(name) || _wcsicmp(ext, L".tar") == 0))
            break;
        name.resize(name.size() - wcslen(ext));
    }
    return name;
}

/// "name" for the "Add to name.7z" item: the item itself or, for several items, their folder.
static std::wstring ArchiveStemFor(const std::vector<std::wstring>& paths)
{
    if (paths.empty())
        return L"Archive";
    if (paths.size() == 1)
    {
        std::wstring p = paths[0];
        DWORD attr = GetFileAttributesW(p.c_str());
        std::wstring name = PathFindFileNameW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return name;
        const wchar_t* ext = PathFindExtensionW(name.c_str());
        return name.substr(0, name.size() - wcslen(ext));
    }
    std::wstring dir = paths[0].substr(0, paths[0].find_last_of(L'\\'));
    std::wstring parent = PathFindFileNameW(dir.c_str());
    return parent.empty() || parent.back() == L':' ? L"Archive" : parent;
}

static std::wstring Truncate(std::wstring s, size_t max = 40)
{
    if (s.size() > max)
        s = s.substr(0, max / 2 - 1) + L"…" + s.substr(s.size() - (max / 2 - 1));
    return s;
}

// ---------------------------------------------------------------------------------------------
// Commands

enum class Selection { Any, ArchivesOnly, SingleArchive };

struct CommandSpec
{
    const wchar_t* Id;           // settings key
    const wchar_t* Verb;         // app command-line verb
    const wchar_t* Title;        // may contain %s for a computed name
    const wchar_t* Glyph;        // Segoe Fluent Icons code point used only in tooltips (no per-item icons)
    Selection Needs;
    bool SeparatorBefore;
};

static const CommandSpec kCommands[] = {
    { L"Open",          L"open",           L"Open archive",                 nullptr, Selection::SingleArchive, false },
    { L"Extract",       L"extract",        L"Extract files…",          nullptr, Selection::ArchivesOnly,  false },
    { L"ExtractHere",   L"extract-here",   L"Extract here",                 nullptr, Selection::ArchivesOnly,  false },
    { L"ExtractSmart",  L"extract-smart",  L"Extract to “%s\\”",  nullptr, Selection::ArchivesOnly,  false },
    { L"Test",          L"test",           L"Test archive",                 nullptr, Selection::ArchivesOnly,  false },
    { L"Compress",      L"compress",       L"Add to archive…",         nullptr, Selection::Any,           true  },
    { L"Compress7z",    L"compress-7z",    L"Add to “%s.7z”",     nullptr, Selection::Any,           false },
    { L"CompressZip",   L"compress-zip",   L"Add to “%s.zip”",    nullptr, Selection::Any,           false },
    { L"CompressShare", L"compress-share", L"Compress and share…",     nullptr, Selection::Any,           false },
    { L"Hash",          L"hash",           L"Checksums (CRC, SHA)",         nullptr, Selection::Any,           true  },
};

class SubCommand final : public IExplorerCommand
{
public:
    explicit SubCommand(const CommandSpec& spec) : _spec(spec) { InterlockedIncrement(&g_objects); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        static const QITAB qit[] = { QITABENT(SubCommand, IExplorerCommand), { nullptr, 0 } };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refs); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&_refs);
        if (r == 0) { delete this; }
        return r;
    }

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(IShellItemArray* items, LPWSTR* name) override
    {
        std::wstring title = _spec.Title;
        size_t pos = title.find(L"%s");
        if (pos != std::wstring::npos)
        {
            auto paths = GetPaths(items);
            std::wstring value;
            if (_spec.Needs == Selection::ArchivesOnly)
                value = paths.size() == 1 ? StemOf(paths[0]) : L"*";
            else
                value = ArchiveStemFor(paths);
            title.replace(pos, 2, Truncate(value));
        }
        return DupString(title, name);
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override { *icon = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override { *tip = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override { *guid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override
    {
        *state = Applies(GetPaths(items)) ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override
    {
        auto paths = GetPaths(items);
        if (paths.empty())
            return S_OK;
        return LaunchApp(BuildArguments(_spec.Verb, paths));
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** e) override { *e = nullptr; return E_NOTIMPL; }

    bool Applies(const std::vector<std::wstring>& paths) const
    {
        if (paths.empty())
            return false;
        switch (_spec.Needs)
        {
        case Selection::SingleArchive:
            return paths.size() == 1 && IsArchivePath(paths[0]);
        case Selection::ArchivesOnly:
            return std::all_of(paths.begin(), paths.end(), [](const std::wstring& p) { return IsArchivePath(p); });
        default:
            return true;
        }
    }

    const CommandSpec& Spec() const { return _spec; }

private:
    ~SubCommand() { InterlockedDecrement(&g_objects); }
    const CommandSpec& _spec;
    LONG _refs = 1;
};

class SeparatorCommand final : public IExplorerCommand
{
public:
    explicit SeparatorCommand(bool onlyWithArchives) : _onlyWithArchives(onlyWithArchives) { InterlockedIncrement(&g_objects); }
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        static const QITAB qit[] = { QITABENT(SeparatorCommand, IExplorerCommand), { nullptr, 0 } };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refs); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&_refs);
        if (r == 0) { delete this; }
        return r;
    }
    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override { *name = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override { *icon = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override { *tip = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override { *guid = GUID_NULL; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override
    {
        // The separator after the archive-only group is pointless when that group is hidden.
        bool show = true;
        if (_onlyWithArchives)
        {
            auto paths = GetPaths(items);
            show = !paths.empty() && std::all_of(paths.begin(), paths.end(), [](const std::wstring& p) { return IsArchivePath(p); });
        }
        *state = show ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return S_OK; }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = ECF_ISSEPARATOR; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** e) override { *e = nullptr; return E_NOTIMPL; }
private:
    ~SeparatorCommand() { InterlockedDecrement(&g_objects); }
    bool _onlyWithArchives;
    LONG _refs = 1;
};

class CommandEnumerator final : public IEnumExplorerCommand
{
public:
    explicit CommandEnumerator(std::vector<IExplorerCommand*> commands) : _commands(std::move(commands)) { InterlockedIncrement(&g_objects); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        static const QITAB qit[] = { QITABENT(CommandEnumerator, IEnumExplorerCommand), { nullptr, 0 } };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refs); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&_refs);
        if (r == 0) { delete this; }
        return r;
    }

    IFACEMETHODIMP Next(ULONG count, IExplorerCommand** out, ULONG* fetched) override
    {
        ULONG n = 0;
        while (n < count && _index < _commands.size())
        {
            out[n] = _commands[_index++];
            out[n]->AddRef();
            n++;
        }
        if (fetched)
            *fetched = n;
        return n == count ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Skip(ULONG count) override
    {
        _index = (std::min)(_index + count, _commands.size());
        return _index < _commands.size() ? S_OK : S_FALSE;
    }
    IFACEMETHODIMP Reset() override { _index = 0; return S_OK; }
    IFACEMETHODIMP Clone(IEnumExplorerCommand** e) override { *e = nullptr; return E_NOTIMPL; }

private:
    ~CommandEnumerator()
    {
        for (auto c : _commands)
            c->Release();
        InterlockedDecrement(&g_objects);
    }
    std::vector<IExplorerCommand*> _commands;
    size_t _index = 0;
    LONG _refs = 1;
};

/// The top-level "7-Zip" entry with its submenu.
class RootCommand final : public IExplorerCommand
{
public:
    RootCommand() { InterlockedIncrement(&g_objects); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        static const QITAB qit[] = { QITABENT(RootCommand, IExplorerCommand), { nullptr, 0 } };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_refs); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG r = InterlockedDecrement(&_refs);
        if (r == 0) { delete this; }
        return r;
    }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override { return SHStrDupW(L"7-Zip", name); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override
    {
        std::wstring path = ModuleDirectory() + L"\\SevenZip.exe,0";
        return SHStrDupW(path.c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override { *tip = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override { *guid = CLSID_SevenZipCommand; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override
    {
        // Only file-system items (not Control Panel, libraries' virtual nodes, etc.).
        *state = GetPaths(items).empty() ? ECS_HIDDEN : ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return E_NOTIMPL; }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = ECF_HASSUBCOMMANDS; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** e) override
    {
        MenuSettings settings;
        std::vector<IExplorerCommand*> commands;
        for (auto& spec : kCommands)
        {
            if (!settings.IsOn(spec.Id))
                continue;
            if (spec.SeparatorBefore && !commands.empty())
                commands.push_back(new (std::nothrow) SeparatorCommand(spec.Needs == Selection::Any && wcscmp(spec.Id, L"Compress") == 0));
            commands.push_back(new (std::nothrow) SubCommand(spec));
        }
        commands.erase(std::remove(commands.begin(), commands.end(), nullptr), commands.end());
        *e = new (std::nothrow) CommandEnumerator(std::move(commands));
        return *e ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~RootCommand() { InterlockedDecrement(&g_objects); }
    LONG _refs = 1;
};

// ---------------------------------------------------------------------------------------------
// COM plumbing

class ClassFactory final : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        static const QITAB qit[] = { QITABENT(ClassFactory, IClassFactory), { nullptr, 0 } };
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        *ppv = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        auto cmd = new (std::nothrow) RootCommand();
        if (!cmd)
            return E_OUTOFMEMORY;
        HRESULT hr = cmd->QueryInterface(riid, ppv);
        cmd->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&g_locks); else InterlockedDecrement(&g_locks);
        return S_OK;
    }
};

static ClassFactory g_factory;

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    *ppv = nullptr;
    if (clsid != CLSID_SevenZipCommand)
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
