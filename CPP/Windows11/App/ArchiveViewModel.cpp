// ArchiveViewModel.cpp

#include "StdAfx.h"

#include "ArchiveViewModel.h"

#include "../../Common/IntToString.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"

#include "../Dispatcher.h"
#include "../HString.h"

#include "../Core/PathSanitizer.h"
#include "../Core/PropertyFormatter.h"

#include "App.h"
#include "DialogService.h"
#include "OperationService.h"
#include "PlatformServices.h"
#include "ShellServices.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace NWindows;
using namespace NWindows11;

static const unsigned k_MaxSearchResults = 5000;
static const unsigned k_MaxPasswordRetries = 4;
static const wchar_t * const k_InvalidNameChars = L"\\/:*?\"<>|";
static const wchar_t * const k_InvalidNameMessage = L"A name can't contain \\ / : * ? \" < > |";

// ---------------- helpers ----------------

static UString ParentPathOf(const UString &path)
{
  const int i = path.ReverseFind(L'\\');
  return i < 0 ? UString() : path.Left((unsigned)i);
}

static FString CombinePath(const FString &folder, const UString &name)
{
  FString s (folder);
  if (!s.IsEmpty() && !IS_PATH_SEPAR(s.Back()))
    s.Add_PathSepar();
  s += us2fs(name);
  return s;
}

static UString UInt32ToUString(UInt32 v)
{
  wchar_t s[16];
  ConvertUInt32ToString(v, s);
  return UString(s);
}

static UString Quoted(const UString &s)
{
  UString r (L"\x201C");
  r += s;
  r += L"\x201D";
  return r;
}

static bool HasInvalidNameChars(const UString &name)
{
  for (const wchar_t *p = k_InvalidNameChars; *p; p++)
    if (name.Find(*p) >= 0)
      return true;
  return false;
}

// culture-aware, case-insensitive "contains" (C# StringComparison.CurrentCultureIgnoreCase)
static bool ContainsNoCase(const UString &s, const UString &sub)
{
  return ::FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE,
      s, (int)s.Len(), sub, (int)sub.Len(), NULL, NULL, NULL, 0) >= 0;
}

// Explorer-style "file2" before "file10" ordering
static int NaturalCompare(const UString &a, const UString &b)
{
  const int r = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
      a, (int)a.Len(), b, (int)b.Len(), NULL, NULL, 0);
  return r == 0 ? 0 : r - CSTR_EQUAL;
}

static int CompareTextNoCase(const UString &a, const UString &b)
{
  const int r = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE,
      a, (int)a.Len(), b, (int)b.Len(), NULL, NULL, 0);
  return r == 0 ? 0 : r - CSTR_EQUAL;
}

template <class T> static int CompareValues(T a, T b) { return a < b ? -1 : a > b ? 1 : 0; }

// C# Nullable.Compare: undefined < defined
static int CompareOptional(bool aDefined, UInt64 a, bool bDefined, UInt64 b)
{
  if (aDefined != bDefined)
    return aDefined ? 1 : -1;
  return aDefined ? CompareValues(a, b) : 0;
}

static UInt64 FileTimeToUInt64(const FILETIME &ft)
{
  return ((UInt64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static void AppendJoined(UString &s, const UStringVector &parts, const wchar_t *separator)
{
  bool first = true;
  FOR_VECTOR (i, parts)
  {
    if (parts[i].IsEmpty())
      continue;
    if (!first)
      s += separator;
    s += parts[i];
    first = false;
  }
}

// "0.#" format
static UString FormatPercentValue(double v)
{
  const double r = (double)(Int64)(v * 10 + 0.5) / 10;
  return NPropertyFormatter::FormatDecimal(r, r == (double)(Int64)r ? 0 : 1);
}

static int FindString(const UStringVector &v, const UString &s)
{
  FOR_VECTOR (i, v)
    if (v[i].IsEqualTo_NoCase(s))
      return (int)i;
  return -1;
}

struct CSortParam
{
  NSortColumn::EEnum Column;
  int Sign;
};

static int CompareItems(void *const *p1, void *const *p2, void *param)
{
  CItemViewModel &a = **(const com_ptr<CItemViewModel> *)*p1;
  CItemViewModel &b = **(const com_ptr<CItemViewModel> *)*p2;
  const CSortParam &sp = *(const CSortParam *)param;
  if (a.IsFolder != b.IsFolder)
    return a.IsFolder ? -1 : 1;
  int r;
  switch (sp.Column)
  {
    case NSortColumn::kModified:
      r = CompareOptional(a.Modified_Defined, FileTimeToUInt64(a.Modified), b.Modified_Defined, FileTimeToUInt64(b.Modified));
      break;
    case NSortColumn::kType: r = CompareTextNoCase(a.GetTypeName(), b.GetTypeName()); break;
    case NSortColumn::kSize: r = CompareValues(a.Size, b.Size); break;
    case NSortColumn::kPacked: r = CompareOptional(a.PackedSize_Defined, a.PackedSize, b.PackedSize_Defined, b.PackedSize); break;
    case NSortColumn::kName:
    default: r = NaturalCompare(a.Name, b.Name); break;
  }
  r *= sp.Sign;
  return r != 0 ? r : NaturalCompare(a.Name, b.Name);
}


// ---------------- CPendingEdit ----------------

CPendingEdit::CPendingEdit(UInt32 index, const UString &archivePath, const FString &tempPath):
    Index(index),
    ArchivePath(archivePath),
    TempPath(tempPath)
{
  AddProperty(L"ArchivePath", [this] { return BoxString(ArchivePath); });
  AddProperty(L"ApplyCommand", [this]() -> CInspectable { return ApplyCommand; });
  AddProperty(L"DiscardCommand", [this]() -> CInspectable { return DiscardCommand; });
}


// ---------------- CEditWatcher ----------------

bool CEditWatcher::Start(const weak_ref<CArchiveViewModel> &owner, UInt32 index, const UString &archivePath,
    const FString &tempPath)
{
  _owner = owner;
  _index = index;
  _archivePath = archivePath;
  _path = tempPath;
  if (!ReadState(_size, _mTime))
    return false;
  FString dir, name;
  if (!NFile::NDir::GetFullPathAndSplit(tempPath, dir, name))
    return false;
  // The temp folder holds only this file. FILE_NAME also catches apps that save by renaming a new file over it.
  _change = ::FindFirstChangeNotificationW(dir, FALSE,
      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME);
  if (_change == INVALID_HANDLE_VALUE)
    return false;
  _wait = ::CreateThreadpoolWait(WaitCallback, this, NULL);
  if (!_wait)
    return false;
  ::SetThreadpoolWait(_wait, _change, NULL);
  return true;
}

CEditWatcher::~CEditWatcher()
{
  if (_wait)
  {
    // A running callback may re-arm the wait before it sees _stopping: cancel twice.
    InterlockedExchange(&_stopping, 1);
    ::SetThreadpoolWait(_wait, NULL, NULL);
    ::WaitForThreadpoolWaitCallbacks(_wait, TRUE);
    ::SetThreadpoolWait(_wait, NULL, NULL);
    ::WaitForThreadpoolWaitCallbacks(_wait, TRUE);
    ::CloseThreadpoolWait(_wait);
  }
  if (_change != INVALID_HANDLE_VALUE)
    ::FindCloseChangeNotification(_change);
}

VOID CALLBACK CEditWatcher::WaitCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WAIT wait, TP_WAIT_RESULT)
{
  CEditWatcher *w = (CEditWatcher *)context;
  if (InterlockedCompareExchange(&w->_stopping, 0, 0) != 0)
    return;
  w->OnChange();
  if (InterlockedCompareExchange(&w->_stopping, 0, 0) == 0 && ::FindNextChangeNotification(w->_change))
    ::SetThreadpoolWait(wait, w->_change, NULL);
}

bool CEditWatcher::ReadState(UInt64 &size, FILETIME &mTime) const
{
  NFile::NFind::CFileInfo fi;
  if (!fi.Find(_path) || fi.IsDir())
    return false;
  size = fi.Size;
  mTime = fi.MTime;
  return true;
}

void CEditWatcher::OnChange()
{
  UInt64 size;
  FILETIME mTime;
  // missing while an app replaces the file: keep the old state and wait for the next change
  if (!ReadState(size, mTime))
    return;
  if (size == _size && CompareFileTime(&mTime, &_mTime) == 0)
    return;
  _size = size;
  _mTime = mTime;
  const weak_ref<CArchiveViewModel> owner = _owner;
  const UInt32 index = _index;
  const UString archivePath = _archivePath;
  const FString path = _path;
  g_App.Dispatcher.TryEnqueue([owner, index, archivePath, path]
  {
    if (com_ptr<CArchiveViewModel> vm = owner.get())
      vm->AddPendingEdit(index, archivePath, path);
  });
}


// ---------------- CArchiveViewModel ----------------

CArchiveViewModel::CArchiveViewModel(IWindowHost *host, const UString &path, CArchiveViewModel *parent,
    const UString &pathInParent, const UString &format):
    _host(host),
    _searchGeneration(0),
    _disposed(false),
    _isLoading(true),
    _isSearching(false),
    _sort(NSortColumn::kName),
    _sortDescending(false),
    FilePath(path),
    Format(format),
    PathInParent(pathInParent)
{
  _parent.copy_from(parent);
  if (parent)
  {
    DisplayName = parent->DisplayName;
    DisplayName += L" \x203A ";
    DisplayName += GetFileNamePart(pathInParent.IsEmpty() ? path : pathInParent);
  }
  else
    DisplayName = GetFileNamePart(path);

  _itemsVector = single_threaded_observable_vector<IInspectable>();
  _breadcrumbs = single_threaded_observable_vector<IInspectable>();
  _pendingEditsVector = single_threaded_observable_vector<IInspectable>();
  _details = single_threaded_observable_vector<IInspectable>();

  AddProperty(L"Items", [this] { return _itemsVector.as<IInspectable>(); });
  AddProperty(L"Breadcrumbs", [this] { return _breadcrumbs.as<IInspectable>(); });
  AddProperty(L"PendingEdits", [this] { return _pendingEditsVector.as<IInspectable>(); });
  AddProperty(L"Details", [this] { return _details.as<IInspectable>(); });
  AddProperty(L"DisplayName", [this] { return BoxString(DisplayName); });
  AddProperty(L"CurrentPath", [this] { return BoxString(_currentPath); });
  AddProperty(L"IsLoading", [this] { return box_value(_isLoading); });
  AddProperty(L"LoadError", [this] { return BoxString(_loadError); });
  AddProperty(L"HasLoadError", [this] { return box_value(!_loadError.IsEmpty()); });
  AddProperty(L"StatusText", [this] { return BoxString(_statusText); });
  AddProperty(L"SelectionText", [this] { return BoxString(_selectionText); });
  AddProperty(L"SearchText", [this] { return BoxString(_searchText); },
      [this](const CInspectable &v) { SetSearchText(UnboxString(v)); });
  AddProperty(L"IsSearching", [this] { return box_value(_isSearching); });
  AddProperty(L"Warning", [this] { return BoxString(_warning); });
  AddProperty(L"HasWarning", [this] { return box_value(!_warning.IsEmpty()); });
  AddProperty(L"DetailsTitle", [this] { return BoxString(_detailsTitle); });
  AddProperty(L"DetailsIcon", [this]() -> CInspectable { return _detailsIcon; });
  AddProperty(L"CanModify", [this] { return box_value(CanModify()); });
  AddProperty(L"ReadOnlyReason", [this] { return BoxString(GetReadOnlyReason()); });
  AddProperty(L"CanGoUp", [this] { return box_value(CanGoUp()); });
  AddProperty(L"EmptyText", [this] { return BoxString(GetEmptyText()); });
  AddProperty(L"EmptyVisible", [this] { return box_value(GetEmptyVisible()); });
  AddProperty(L"ReadOnlyVisible", [this] { return box_value(GetReadOnlyVisible()); });
  AddSortProperties(L"NameSortGlyph", L"NameSortLabel", NSortColumn::kName, L"Name");
  AddSortProperties(L"ModifiedSortGlyph", L"ModifiedSortLabel", NSortColumn::kModified, L"Date modified");
  AddSortProperties(L"TypeSortGlyph", L"TypeSortLabel", NSortColumn::kType, L"Type");
  AddSortProperties(L"SizeSortGlyph", L"SizeSortLabel", NSortColumn::kSize, L"Size");
  AddSortProperties(L"PackedSortGlyph", L"PackedSortLabel", NSortColumn::kPacked, L"Packed");
}

void CArchiveViewModel::AddSortProperties(const wchar_t *glyphName, const wchar_t *labelName,
    NSortColumn::EEnum column, const wchar_t *label)
{
  AddProperty(glyphName, [this, column] { return BoxString(GetSortGlyph(column)); });
  AddProperty(labelName, [this, column, label] { return BoxString(GetSortLabel(column, label)); });
}

UString CArchiveViewModel::GetSortGlyph(NSortColumn::EEnum column) const
{
  if (column != _sort)
    return UString();
  return UString(_sortDescending ? L"\xE70D" : L"\xE70E");
}

// Accessible names: the sort arrow says nothing to a screen reader on its own.
UString CArchiveViewModel::GetSortLabel(NSortColumn::EEnum column, const wchar_t *label) const
{
  UString s (label);
  if (column == _sort)
    s += _sortDescending ? L", sorted descending" : L", sorted ascending";
  return s;
}

void CArchiveViewModel::RaiseSortProperties()
{
  static const wchar_t * const k_Names[] =
  {
    L"NameSortGlyph", L"NameSortLabel", L"ModifiedSortGlyph", L"ModifiedSortLabel",
    L"TypeSortGlyph", L"TypeSortLabel", L"SizeSortGlyph", L"SizeSortLabel",
    L"PackedSortGlyph", L"PackedSortLabel"
  };
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_Names); i++)
    RaisePropertyChanged(k_Names[i]);
}

void CArchiveViewModel::SetString(UString &field, const UString &value, const wchar_t *name)
{
  if (field == value)
    return;
  field = value;
  RaisePropertyChanged(name);
}

void CArchiveViewModel::SetIsLoading(bool value)
{
  if (_isLoading == value)
    return;
  _isLoading = value;
  RaisePropertyChanged(L"IsLoading");
  RaiseStateProperties();
}

void CArchiveViewModel::SetLoadError(const UString &value)
{
  if (_loadError == value)
    return;
  _loadError = value;
  RaisePropertyChanged(L"LoadError");
  RaiseStateProperties();
}

void CArchiveViewModel::SetIsSearching(bool value)
{
  if (_isSearching == value)
    return;
  _isSearching = value;
  RaisePropertyChanged(L"IsSearching");
  RaisePropertyChanged(L"EmptyText");
}

// C# ArchivePage raised these on every view model change.
void CArchiveViewModel::RaiseStateProperties()
{
  RaisePropertyChanged(L"HasLoadError");
  RaisePropertyChanged(L"EmptyText");
  RaisePropertyChanged(L"EmptyVisible");
  RaisePropertyChanged(L"ReadOnlyVisible");
  RaisePropertyChanged(L"CanModify");
  RaisePropertyChanged(L"ReadOnlyReason");
  RaisePropertyChanged(L"CanGoUp");
}

UString CArchiveViewModel::GetEmptyText() const
{
  return UString(_isSearching ? L"No matches" : L"This folder is empty");
}

bool CArchiveViewModel::GetEmptyVisible() const
{
  return !_isLoading && _loadError.IsEmpty() && _items.IsEmpty();
}

bool CArchiveViewModel::GetReadOnlyVisible() const
{
  return !_isLoading && _session && !CanModify();
}

bool CArchiveViewModel::CanModify() const
{
  // (a nested archive whose parent failed to open has no session Parent: check _parent too)
  return _session && !_parent && _session->Session.CanUpdate();
}

UString CArchiveViewModel::GetReadOnlyReason() const
{
  if (!_session)
    return UString();
  const CArchiveSession &s = _session->Session;
  if (_parent || _outer)
    return UString(L"Nested archives are read-only. Extract the archive to change it.");
  if (s.Format && !s.Format->CanUpdate)
  {
    UString r (s.Format->Name);
    r += L" archives can be opened and extracted but not modified.";
    return r;
  }
  if (s.VolumePaths.Size() > 1 || !s.ContainerFormat.IsEmpty())
    return UString(L"Multi-volume archives are read-only.");
  if (s.Info.HasErrors())
    return UString(L"This archive has errors, so it is opened read-only.");
  return UString();
}

UString CArchiveViewModel::GetRootFilePath() const
{
  return _parent ? _parent->GetRootFilePath() : FilePath;
}

FString CArchiveViewModel::GetFolderForArchive() const
{
  return us2fs(GetDirectoryPart(GetRootFilePath()));
}

// "a.zip > b.tar" -> "b.tar"
UString CArchiveViewModel::GetLastDisplayName() const
{
  const int i = DisplayName.Find(L" \x203A ");
  if (i < 0)
    return DisplayName;
  int last = i;
  for (;;)
  {
    const int next = DisplayName.Find(L" \x203A ", (unsigned)last + 3);
    if (next < 0)
      break;
    last = next;
  }
  return DisplayName.Ptr((unsigned)last + 3);
}

UString CArchiveViewModel::GetSmartFolderName() const
{
  return NExtractPlanner::GetFolderNameFor(GetLastDisplayName());
}

com_ptr<COperationViewModel> CArchiveViewModel::NewOperation(NOperationKind::EEnum kind, const UString &title,
    const UString &archivePath)
{
  com_ptr<COperationViewModel> op = make_self<COperationViewModel>(kind, title, &_host->Dialogs(), archivePath);
  op->SetEfficiencyMode(g_App.Settings.Current.EfficiencyModeForBackground);
  return op;
}

// ---------------- open ----------------

IAsyncAction CArchiveViewModel::OpenAsync(bool password_Defined, UString password)
{
  auto strong = get_strong();
  SetIsLoading(true);
  SetLoadError(UString());
  try
  {
    UString title (L"Opening ");
    title += GetFileNamePart(FilePath);
    com_ptr<COperationViewModel> op = NewOperation(NOperationKind::kOpen, title, _parent ? UString() : FilePath);

    com_ptr<CArchiveSessionHolder> holder = make_self<CArchiveSessionHolder>();
    com_ptr<CArchiveSessionHolder> inner = make_self<CArchiveSessionHolder>();
    // keeps the parent's session alive while it is referenced by the open call
    com_ptr<CArchiveSessionHolder> parentSession;
    if (_parent)
      parentSession = _parent->_session;

    CArchiveOpenOptions options;
    options.Password_Defined = password_Defined;
    options.Password = password;
    if (!Format.IsEmpty())
      options.Format = NFormatRegistry::Find(Format);
    if (_parent)
    {
      options.DisplayName = GetFileNamePart(PathInParent);
      options.Parent = parentSession ? &parentSession->Session : NULL;
      options.PathInParent = PathInParent;
    }

    // "backup.tar.gz" opens straight into the tar (one stream layer is just noise).
    const bool unwrapTarball = g_App.Settings.Current.AutoOpenTarballs;
    FString tempFolder;
    if (unwrapTarball)
      tempFolder = NTempFiles::NewFolder(); // here: NTempFiles isn't meant for worker threads
    bool innerOpened = false;
    const FString path = us2fs(FilePath);
    CArchiveSession *session = &holder->Session;
    CArchiveSession *innerSession = &inner->Session;

    const HRESULT hr = co_await _host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage) -> HRESULT
        {
          HRESULT res;
          for (unsigned attempt = 0;; attempt++)
          {
            res = session->Open(path, options, ctx, errorMessage);
            // the prompts show "wrong password, try again" on the next request
            if (res != k_HRESULT_WrongPassword || attempt >= k_MaxPasswordRetries)
              break;
          }
          RINOK(res)
          if (unwrapTarball && session->CanUnwrapTarball())
          {
            RINOK(session->UnwrapTarball(tempFolder, *innerSession, ctx, errorMessage))
            innerOpened = true;
          }
          return S_OK;
        },
        [&] { return NPropertyFormatter::FormatCount((Int64)session->Entries.Size(), L"item"); });

    if (!tempFolder.IsEmpty() && !innerOpened)
      NFile::NDir::RemoveDir(tempFolder);
    if (_disposed)
      co_return;

    if (hr == E_ABORT)
      SetLoadError(UString(L"Opening was cancelled."));
    else if (hr != S_OK)
      SetLoadError(op->GetErrorMessage());
    else
    {
      if (innerOpened)
      {
        _outer = holder;
        _session = inner;
      }
      else
      {
        _outer = nullptr;
        _session = holder;
      }

      if (!_parent)
        g_App.Settings.AddRecent(FilePath);

      const CArchiveInfo &info = _session->Session.Info;
      UStringVector parts;
      if (info.HasErrors())
      {
        CArchiveInfo::DescribeErrorFlags(info.ErrorFlags, parts);
        parts.Add(info.Error);
        UString w (L"This archive has problems: ");
        AppendJoined(w, parts, L"; ");
        SetString(_warning, w, L"Warning");
        RaisePropertyChanged(L"HasWarning");
      }
      else if (info.HasWarnings())
      {
        CArchiveInfo::DescribeErrorFlags(info.WarningFlags, parts);
        parts.Add(info.Warning);
        UString w;
        AppendJoined(w, parts, L"; ");
        SetString(_warning, w, L"Warning");
        RaisePropertyChanged(L"HasWarning");
      }
      RaiseStateProperties();
      Navigate(UString());
    }
  }
  catch (const hresult_error &e)
  {
    if (!_disposed)
      SetLoadError(ToUString(e.message()));
  }
  if (!_disposed)
    SetIsLoading(false);
}

IAsyncAction CArchiveViewModel::RefreshAsync()
{
  auto strong = get_strong();
  const UString path = _currentPath;
  bool password_Defined = false;
  UString password;
  if (_session)
  {
    password_Defined = _session->Session.Password_Defined;
    password = _session->Session.Password;
    _session->Session.Close();
  }
  co_await OpenAsync(password_Defined, password);
  if (!_disposed)
    Navigate(path);
}

// ---------------- navigation ----------------

void CArchiveViewModel::Navigate(const UString &path)
{
  if (!_session)
    return;
  const CArchiveFolder &root = _session->Session.Root;
  const CArchiveFolder *folder = root.Navigate(path);
  if (!folder)
    folder = &root;
  SetString(_currentPath, folder->Path, L"CurrentPath");
  if (!_searchText.IsEmpty())
  {
    InterlockedIncrement(&_searchGeneration);
    _searchText.Empty();
    RaisePropertyChanged(L"SearchText");
  }

  _breadcrumbs.Clear();
  _breadcrumbPaths.Clear();
  _breadcrumbs.Append(BoxString(DisplayName));
  _breadcrumbPaths.Add(UString());
  UString acc;
  UStringVector parts;
  {
    const UString &p = folder->Path;
    unsigned start = 0;
    for (unsigned i = 0; i <= p.Len(); i++)
      if (i == p.Len() || p[i] == L'\\')
      {
        if (i > start)
          parts.Add(p.Mid(start, i - start));
        start = i + 1;
      }
  }
  FOR_VECTOR (i, parts)
  {
    if (!acc.IsEmpty())
      acc.Add_Char('\\');
    acc += parts[i];
    _breadcrumbs.Append(BoxString(parts[i]));
    _breadcrumbPaths.Add(acc);
  }
  Populate(*folder);
}

void CArchiveViewModel::GoUp()
{
  if (!CanGoUp())
    return;
  Navigate(ParentPathOf(_currentPath));
}

const CArchiveFolder &CArchiveViewModel::GetCurrentFolder() const
{
  const CArchiveFolder &root = _session->Session.Root;
  const CArchiveFolder *f = root.Navigate(_currentPath);
  return f ? *f : root;
}

void CArchiveViewModel::Populate(const CArchiveFolder &folder)
{
  CArchiveSessionHolder *holder = _session.get();
  _items.ClearAndReserve(folder.GetNumFolders() + folder.Files.Size());
  for (unsigned i = 0; i < folder.GetNumFolders(); i++)
    _items.Add(CItemViewModel::FromFolder(holder, folder.GetFolder(i)));
  FOR_VECTOR (i, folder.Files)
    _items.Add(CItemViewModel::FromEntry(holder, *folder.Files[i]));
  SortItems();
  PublishItems();
  SetIsSearching(false);
  UpdateStatus();
  ShowDetails(CItemVector());
  RaisePropertyChanged(L"CanGoUp");
}

void CArchiveViewModel::SetSort(NSortColumn::EEnum column)
{
  if (_sort == column)
    _sortDescending = !_sortDescending;
  else
  {
    _sort = column;
    _sortDescending = (column == NSortColumn::kModified || column == NSortColumn::kSize || column == NSortColumn::kPacked);
  }
  RaiseSortProperties();
  SortItems();
  PublishItems();
}

void CArchiveViewModel::SortItems()
{
  CSortParam sp;
  sp.Column = _sort;
  sp.Sign = _sortDescending ? -1 : 1;
  _items.Sort(CompareItems, &sp);
}

// A new collection per folder / sort, like the C# list: the ListView resets once.
void CArchiveViewModel::PublishItems()
{
  std::vector<IInspectable> v;
  v.reserve(_items.Size());
  FOR_VECTOR (i, _items)
    v.push_back(_items[i].as<IInspectable>());
  _itemsVector = single_threaded_observable_vector<IInspectable>(std::move(v));
  RaisePropertyChanged(L"Items");
  RaisePropertyChanged(L"EmptyVisible");
}

void CArchiveViewModel::UpdateStatus()
{
  if (!_session)
    return;
  UInt32 folders = 0;
  UInt64 size = 0;
  FOR_VECTOR (i, _items)
  {
    const CItemViewModel &item = *_items[i];
    if (item.IsFolder)
      folders++;
    size += item.Size;
  }
  UString s = NPropertyFormatter::FormatCount((Int64)_items.Size(), L"item");
  s += L" (";
  s += NPropertyFormatter::FormatCount(folders, L"folder");
  s += L", ";
  s += NPropertyFormatter::FormatCount((Int64)(_items.Size() - folders), L"file");
  s += L") \x00B7 ";
  s += NPropertyFormatter::FormatSize(size);
  SetString(_statusText, s, L"StatusText");
}

// ---------------- search ----------------

void CArchiveViewModel::SetSearchText(const UString &value)
{
  if (_searchText == value)
    return;
  _searchText = value;
  RaisePropertyChanged(L"SearchText");
  SearchAsync(value);
}

struct CSearchHit
{
  const CArchiveFolder *Folder;
  const CArchiveEntry *Entry;
};

// Searches the whole archive (classic 7-Zip only filtered the current folder).
fire_and_forget CArchiveViewModel::SearchAsync(UString query)
{
  auto strong = get_strong();
  const LONG generation = InterlockedIncrement(&_searchGeneration);
  if (!_session)
    co_return;
  UString trimmed (query);
  trimmed.Trim();
  if (trimmed.IsEmpty())
  {
    if (_isSearching)
      Populate(GetCurrentFolder());
    co_return;
  }
  com_ptr<CArchiveSessionHolder> holder = _session;
  co_await resume_after(std::chrono::milliseconds(150));

  CRecordVector<CSearchHit> hits;
  if (InterlockedCompareExchange(&_searchGeneration, 0, 0) == generation)
  {
    CRecordVector<const CArchiveFolder *> stack;
    stack.Add(&holder->Session.Root);
    while (!stack.IsEmpty() && hits.Size() < k_MaxSearchResults
        && InterlockedCompareExchange(&_searchGeneration, 0, 0) == generation)
    {
      const CArchiveFolder *f = stack.Back();
      stack.DeleteBack();
      for (unsigned i = 0; i < f->GetNumFolders(); i++)
      {
        const CArchiveFolder &sub = f->GetFolder(i);
        if (ContainsNoCase(sub.Name, query))
        {
          CSearchHit h;
          h.Folder = &sub;
          h.Entry = NULL;
          hits.Add(h);
        }
        stack.Add(&sub);
      }
      FOR_VECTOR (i, f->Files)
      {
        const CArchiveEntry *e = f->Files[i];
        if (ContainsNoCase(e->GetName(), query))
        {
          CSearchHit h;
          h.Folder = NULL;
          h.Entry = e;
          hits.Add(h);
        }
      }
    }
  }

  // back to the UI thread in every case, so the last reference is released there
  try
  {
    co_await ResumeForeground(g_App.Dispatcher);
  }
  catch (const hresult_error &)
  {
    co_return; // the UI thread is shutting down
  }
  if (_disposed || InterlockedCompareExchange(&_searchGeneration, 0, 0) != generation || holder != _session)
    co_return;

  _items.ClearAndReserve(hits.Size());
  FOR_VECTOR (i, hits)
  {
    const CSearchHit &h = hits[i];
    if (h.Folder)
      _items.Add(CItemViewModel::FromFolder(holder.get(), *h.Folder, true));
    else
      _items.Add(CItemViewModel::FromEntry(holder.get(), *h.Entry, true));
  }
  SortItems();
  PublishItems();
  SetIsSearching(true);
  UString status;
  if (_items.Size() >= k_MaxSearchResults)
    status = L"More than 5,000 results \x2014 refine your search";
  else
  {
    status = NPropertyFormatter::FormatNumber(_items.Size());
    status += L" results for ";
    status += Quoted(query);
  }
  SetString(_statusText, status, L"StatusText");
}

// ---------------- details pane ----------------

void CArchiveViewModel::AddDetail(const UString &name, const UString &value)
{
  _details.Append(make<CPropertyRow>(name, value));
}

void CArchiveViewModel::ShowDetails(const CItemVector &selection)
{
  _details.Clear();
  if (!_session)
    return;
  const CArchiveSession &session = _session->Session;

  if (selection.IsEmpty())
  {
    const CArchiveInfo &info = session.Info;
    SetString(_detailsTitle, DisplayName, L"DetailsTitle");
    _detailsIcon = NShellIcons::GetIconForPath(FilePath);
    RaisePropertyChanged(L"DetailsIcon");
    const CFolderMeasure m = session.Root.Measure();
    UString contents = NPropertyFormatter::FormatCount(m.Files, L"file");
    contents += L", ";
    contents += NPropertyFormatter::FormatCount(m.Folders, L"folder");
    AddDetail(L"Contents", contents);
    AddDetail(L"Total size", NPropertyFormatter::FormatSizeExact(m.Size));
    if (info.PhysicalSize_Defined)
    {
      const UInt64 phys = info.PhysicalSize;
      AddDetail(L"Archive size", NPropertyFormatter::FormatSizeExact(phys));
      if (m.Size > 0)
      {
        UString c = FormatPercentValue(100.0 * (double)phys / (double)m.Size);
        c += L"% of original (saved ";
        c += NPropertyFormatter::FormatSize(m.Size > phys ? m.Size - phys : 0);
        c += L")";
        AddDetail(L"Compression", c);
      }
    }
    AddDetail(L"Encrypted", UString(session.HeadersEncrypted ? L"Yes, including file names"
        : session.HasEncryptedItems ? L"Yes" : L"No"));
    FOR_VECTOR (i, info.AllProperties)
    {
      const CPropertyItem &p = info.AllProperties[i];
      if (!p.Name.IsEqualTo("Path") && !p.Name.IsEqualTo("Physical size"))
        AddDetail(p.Name, p.Value);
    }
    SetString(_selectionText, UString(), L"SelectionText");
    return;
  }

  if (selection.Size() == 1)
  {
    CItemViewModel &item = *selection[0];
    SetString(_detailsTitle, item.Name, L"DetailsTitle");
    _detailsIcon = item.GetIcon();
    RaisePropertyChanged(L"DetailsIcon");
    if (item.IsFolder && item.Folder)
    {
      const CFolderMeasure m = item.Folder->Measure();
      AddDetail(L"Type", item.GetTypeName());
      UString c = NPropertyFormatter::FormatCount(m.Files, L"file");
      c += L", ";
      c += NPropertyFormatter::FormatCount(m.Folders, L"folder");
      AddDetail(L"Contains", c);
      AddDetail(L"Size", NPropertyFormatter::FormatSizeExact(m.Size));
      if (m.Packed > 0)
        AddDetail(L"Packed size", NPropertyFormatter::FormatSizeExact(m.Packed));
      if (item.Modified_Defined)
        AddDetail(L"Modified", NPropertyFormatter::FormatFileTime(item.Modified));
    }
    if (item.Entry)
    {
      AddDetail(L"Type", item.GetTypeName());
      CPropertyItemVector props;
      item.GetSessionHolder()->Session.GetItemProperties(item.Entry->Index, props);
      FOR_VECTOR (i, props)
      {
        const CPropertyItem &p = props[i];
        if (p.Name.IsEqualTo("Folder"))
          continue;
        if (item.IsFolder && (p.Name.IsEqualTo("Size") || p.Name.IsEqualTo("Packed size")))
          continue;
        AddDetail(p.Name, p.Value);
      }
    }
    UString sel;
    if (item.IsFolder)
      sel = L"1 folder selected";
    else
    {
      sel = L"1 item selected \x00B7 ";
      sel += item.GetSizeText();
    }
    SetString(_selectionText, sel, L"SelectionText");
    return;
  }

  UString title = NPropertyFormatter::FormatNumber(selection.Size());
  title += L" items";
  SetString(_detailsTitle, title, L"DetailsTitle");
  _detailsIcon = nullptr;
  RaisePropertyChanged(L"DetailsIcon");
  UInt64 totalSize = 0, totalPacked = 0;
  FOR_VECTOR (i, selection)
  {
    const CItemViewModel &item = *selection[i];
    totalSize += item.Size;
    if (item.PackedSize_Defined)
      totalPacked += item.PackedSize;
  }
  AddDetail(L"Size", NPropertyFormatter::FormatSizeExact(totalSize));
  if (totalPacked > 0)
    AddDetail(L"Packed size", NPropertyFormatter::FormatSizeExact(totalPacked));
  UString sel = NPropertyFormatter::FormatNumber(selection.Size());
  sel += L" items selected \x00B7 ";
  sel += NPropertyFormatter::FormatSize(totalSize);
  SetString(_selectionText, sel, L"SelectionText");
}

// ---------------- extraction ----------------

bool CArchiveViewModel::Resolve(const CItemVector &items, CRecordVector<UInt32> &indices, UString &basePath) const
{
  indices.Clear();
  if (items.IsEmpty())
  {
    basePath = _currentPath;
    if (_currentPath.IsEmpty() && !_isSearching)
      return false;
    const CArchiveFolder &f = GetCurrentFolder();
    CRecordVector<const CArchiveFolder *> folders;
    for (unsigned i = 0; i < f.GetNumFolders(); i++)
      folders.Add(&f.GetFolder(i));
    NExtractEngine::CollectIndices(f.Files, folders, indices);
    return true;
  }
  CRecordVector<const CArchiveEntry *> files;
  CRecordVector<const CArchiveFolder *> folders;
  FOR_VECTOR (i, items)
  {
    const CItemViewModel &item = *items[i];
    // rows of a session that was reopened meanwhile would give wrong indices
    if (item.GetSessionHolder() != _session.get())
      continue;
    if (item.IsFolder)
    {
      if (item.Folder)
        folders.Add(item.Folder);
    }
    else if (item.Entry)
      files.Add(item.Entry);
  }
  NExtractEngine::CollectIndices(files, folders, indices);
  // Search results come from many folders; keep their structure relative to the archive root.
  basePath = _isSearching ? UString() : _currentPath;
  return true;
}

void CArchiveViewModel::GetDefaultOptions(const FString &destination, CExtractOptions &options) const
{
  const CAppSettings &s = g_App.Settings.Current;
  options = CExtractOptions();
  options.Destination = destination;
  options.Overwrite = s.OverwriteMode;
  options.ZonePropagation = s.ZonePropagation;
  options.RestoreSymbolicLinks = s.RestoreSymbolicLinks;
}

IAsyncAction CArchiveViewModel::ExtractAsync(CItemVector items, CExtractOptions options, bool openFolder)
{
  auto strong = get_strong();
  if (!_session)
    co_return;
  CRecordVector<UInt32> indices;
  UString basePath;
  const bool selected = Resolve(items, indices, basePath);
  options.BasePath = basePath;
  options.ExtractAlternateStreams = false;
  com_ptr<CArchiveSessionHolder> holder = _session;
  CArchiveSession *session = &holder->Session;
  UString title (L"Extracting ");
  title += DisplayName;
  com_ptr<COperationViewModel> op = NewOperation(NOperationKind::kExtract, title, _parent ? UString() : FilePath);
  CExtractResult result;
  IWindowHost *host = _host;
  try
  {
    const HRESULT hr = co_await host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage)
        {
          return NExtractEngine::Extract(*session, selected ? &indices : NULL, options, ctx, result, errorMessage);
        },
        [&]
        {
          UString s;
          if (result.Errors == 0)
          {
            s = L"Extracted ";
            s += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
            s += L" to ";
            s += fs2us(result.Destination);
          }
          else
          {
            s = NPropertyFormatter::FormatCount(result.Errors, L"error");
            s += L" \x00B7 ";
            s += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
          }
          return s;
        },
        [&] { return fs2us(result.Destination); });
    if (hr == E_ABORT)
      co_return;
    if (hr != S_OK)
    {
      co_await host->Dialogs().ShowErrorAsync(L"Couldn't extract", op->GetErrorMessage());
      co_return;
    }
    if (result.WrongPassword)
    {
      co_await host->Dialogs().ShowErrorAsync(L"Wrong password",
          L"Some files couldn't be decrypted. Check the password and try again.");
      co_return;
    }
    if (op->Messages.Size() != 0)
      co_await host->Dialogs().ShowMessagesAsync(UString(result.Errors > 0 ? L"Extraction finished with errors"
          : L"Extraction finished with warnings"), op->Messages);
    if (openFolder && result.Errors == 0)
    {
      if (result.TopLevelPaths.Size() > 0 && result.TopLevelPaths.Size() <= 16)
        NShellActions::RevealInExplorer(result.TopLevelPaths);
      else
        NShellActions::OpenFolder(fs2us(result.Destination));
    }
  }
  catch (const hresult_error &e)
  {
    host->Dialogs().ShowErrorAsync(L"Couldn't extract", ToUString(e.message()));
  }
}

IAsyncAction CArchiveViewModel::ExtractHereAsync(CItemVector items)
{
  CExtractOptions options;
  GetDefaultOptions(GetFolderForArchive(), options);
  return ExtractAsync(items, options, g_App.Settings.Current.OpenFolderAfterExtract);
}

IAsyncAction CArchiveViewModel::ExtractSmartAsync(CItemVector items)
{
  const FString folder = GetFolderForArchive();
  FString dest = folder;
  if (_session)
  {
    if (!items.IsEmpty())
      dest = CombinePath(folder, GetSmartFolderName());
    else
      dest = NExtractPlanner::GetSmartDestination(_session->Session, folder);
  }
  CExtractOptions options;
  GetDefaultOptions(dest, options);
  return ExtractAsync(items, options, g_App.Settings.Current.OpenFolderAfterExtract);
}

CExtractOptionsPanelRef CArchiveViewModel::CreateExtractOptions(const CItemVector &items)
{
  const FString folder = GetFolderForArchive();
  bool suggest = g_App.Settings.Current.SmartExtractFolder;
  if (suggest && _session && items.IsEmpty() && _currentPath.IsEmpty())
    suggest = (NExtractPlanner::GetSmartDestination(_session->Session, folder) != folder);
  const bool needsPassword = _session && _session->Session.HasEncryptedItems && !_session->Session.Password_Defined;
  return CreateExtractOptionsPanel(_host, folder, GetSmartFolderName(), suggest, needsPassword);
}

IAsyncAction CArchiveViewModel::ExtractToTempAsync(CItemVector items, bool quiet, UStringVector *paths)
{
  auto strong = get_strong();
  paths->Clear();
  if (!_session || items.IsEmpty())
    co_return;
  const FString temp = NTempFiles::NewFolder();
  CRecordVector<UInt32> indices;
  UString basePath;
  Resolve(items, indices, basePath);
  basePath = (items.Size() == 1 || !_isSearching) ? ParentPathOf(items[0]->Path) : UString();
  com_ptr<CArchiveSessionHolder> holder = _session;
  CArchiveSession *session = &holder->Session;
  UString title (L"Preparing ");
  if (items.Size() == 1)
    title += items[0]->Name;
  else
  {
    title += UInt32ToUString(items.Size());
    title += L" items";
  }
  com_ptr<COperationViewModel> op = NewOperation(NOperationKind::kExtract, title, _parent ? UString() : FilePath);
  CExtractOptions options;
  options.Destination = temp;
  options.BasePath = basePath;
  options.Overwrite = NOverwriteMode::kOverwrite;
  options.ZonePropagation = NZonePropagation::kAll;
  CExtractResult result;
  IWindowHost *host = _host;
  const HRESULT hr = co_await host->Operations().RunAsync(op,
      [&](COperationContext &ctx, UString &errorMessage)
      {
        return NExtractEngine::Extract(*session, &indices, options, ctx, result, errorMessage);
      },
      [&] { return NPropertyFormatter::FormatCount(result.FilesProcessed, L"file"); });
  if (hr == E_ABORT)
    co_return;
  if (hr != S_OK)
  {
    if (!quiet)
      co_await host->Dialogs().ShowErrorAsync(L"Couldn't extract", op->GetErrorMessage());
    co_return;
  }
  if (result.Errors > 0 && !quiet)
  {
    co_await host->Dialogs().ShowMessagesAsync(UString(L"Some files couldn't be extracted"), op->Messages);
    co_return;
  }
  FOR_VECTOR (i, items)
  {
    const CItemViewModel &item = *items[i];
    // with no base path (several search results) the top-level item is the first folder of the path
    UStringVector components;
    NPathSanitizer::GetSafeComponents(basePath.IsEmpty() ? item.Path : item.Name, components);
    const FString p = CombinePath(temp, components.IsEmpty() ? item.Name : components[0]);
    if (FindString(*paths, fs2us(p)) >= 0 || !NFile::NFind::DoesFileOrDirExist(p))
      continue;
    paths->Add(fs2us(p));
  }
}

// Double-click: folders navigate, nested archives open in a new tab, files open with their app.
IAsyncAction CArchiveViewModel::OpenItemAsync(com_ptr<CItemViewModel> item, bool forceOutside, bool openWith)
{
  auto strong = get_strong();
  if (item->IsFolder)
  {
    Navigate(item->Path);
    co_return;
  }
  CItemVector items;
  items.Add(item);
  UStringVector paths;
  co_await ExtractToTempAsync(items, false, &paths);
  if (_disposed || paths.IsEmpty())
    co_return;
  const UString path = paths[0];
  if (!forceOutside && !openWith && NFormatRegistry::LooksLikeArchive(path)
      && !GetExtensionPart(path).IsEqualTo_Ascii_NoCase(".exe"))
  {
    _host->OpenNestedArchive(this, path, item->Path);
    co_return;
  }
  if (openWith)
    NShellActions::OpenWith(_host->GetHwnd(), path);
  else
    NShellActions::Launch(path);
  if (CanModify() && item->Entry && item->GetSessionHolder() == _session.get())
    WatchForEdits(item->Entry->Index, item->Entry->Path, us2fs(path));
}

// Opens a nested archive explicitly ("Open as archive").
IAsyncAction CArchiveViewModel::OpenInsideAsync(com_ptr<CItemViewModel> item)
{
  auto strong = get_strong();
  CItemVector items;
  items.Add(item);
  UStringVector paths;
  co_await ExtractToTempAsync(items, false, &paths);
  if (!_disposed && paths.Size() == 1)
    _host->OpenNestedArchive(this, paths[0], item->Path);
}

void CArchiveViewModel::WatchForEdits(UInt32 index, const UString &archivePath, const FString &tempPath)
{
  CEditWatcher &w = _watchers.AddNew();
  if (!w.Start(get_weak(), index, archivePath, tempPath))
    _watchers.DeleteBack();
}

int CArchiveViewModel::FindPendingEdit(const CPendingEdit *edit) const
{
  FOR_VECTOR (i, _pendingEdits)
    if (_pendingEdits[i].get() == edit)
      return (int)i;
  return -1;
}

void CArchiveViewModel::RemovePendingEdit(unsigned index)
{
  _pendingEdits.Delete(index);
  _pendingEditsVector.RemoveAt(index);
}

void CArchiveViewModel::AddPendingEdit(UInt32 index, const UString &archivePath, const FString &tempPath)
{
  if (_disposed)
    return;
  FOR_VECTOR (i, _pendingEdits)
    if (_pendingEdits[i]->TempPath == tempPath)
      return;
  com_ptr<CPendingEdit> edit = make_self<CPendingEdit>(index, archivePath, tempPath);
  // the commands are owned by the edit: no strong references back to it or to the view model
  CPendingEdit *e = edit.get();
  const weak_ref<CArchiveViewModel> weak = get_weak();
  edit->ApplyCommand = make<CCommand>([weak, e]
  {
    if (com_ptr<CArchiveViewModel> vm = weak.get())
      vm->ApplyEditAsync(e);
  });
  edit->DiscardCommand = make<CCommand>([weak, e]
  {
    if (com_ptr<CArchiveViewModel> vm = weak.get())
      vm->DiscardEdit(e);
  });
  _pendingEdits.Add(edit);
  _pendingEditsVector.Append(edit.as<IInspectable>());
}

IAsyncAction CArchiveViewModel::ApplyEditAsync(CPendingEdit *e)
{
  auto strong = get_strong();
  const int i = FindPendingEdit(e);
  if (i < 0)
    co_return;
  com_ptr<CPendingEdit> edit = _pendingEdits[(unsigned)i];
  RemovePendingEdit((unsigned)i);
  if (!_session)
    co_return;
  const CObjectVector<CArchiveEntry> &entries = _session->Session.Entries;
  int found = -1;
  FOR_VECTOR (k, entries)
    if (entries[k].Path == edit->ArchivePath)
    {
      found = (int)k;
      break;
    }
  if (found < 0)
    co_return;
  CUpdatePlan plan;
  CReplaceItem &r = plan.Replace.AddNew();
  r.Index = entries[(unsigned)found].Index;
  r.Path = edit->TempPath;
  UString title (L"Updating ");
  title += GetFileNamePart(edit->ArchivePath);
  co_await UpdateAsync(plan, title);
}

void CArchiveViewModel::DiscardEdit(CPendingEdit *edit)
{
  const int i = FindPendingEdit(edit);
  if (i >= 0)
    RemovePendingEdit((unsigned)i);
}

IAsyncAction CArchiveViewModel::TestAsync()
{
  auto strong = get_strong();
  if (!_session)
    co_return;
  com_ptr<CArchiveSessionHolder> holder = _session;
  CArchiveSession *session = &holder->Session;
  UString title (L"Testing ");
  title += DisplayName;
  com_ptr<COperationViewModel> op = NewOperation(NOperationKind::kTest, title, _parent ? UString() : FilePath);
  CExtractResult result;
  IWindowHost *host = _host;
  const UString displayName = DisplayName;
  try
  {
    const HRESULT hr = co_await host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage)
        {
          return NExtractEngine::Test(*session, NULL, ctx, result, errorMessage);
        },
        [&]
        {
          UString s;
          if (result.Errors == 0)
          {
            s = L"No errors \x00B7 ";
            s += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
            s += L" tested";
          }
          else
          {
            s = NPropertyFormatter::FormatCount(result.Errors, L"error");
            s += L" found";
          }
          return s;
        });
    if (hr == E_ABORT)
      co_return;
    if (hr != S_OK)
    {
      co_await host->Dialogs().ShowErrorAsync(L"Test failed", op->GetErrorMessage());
      co_return;
    }
    if (result.Errors == 0)
    {
      UString msg (L"There are no errors in ");
      msg += displayName;
      msg += L".\n\n";
      msg += NPropertyFormatter::FormatCount(result.FilesProcessed, L"file");
      msg += L", ";
      msg += NPropertyFormatter::FormatSize(result.Bytes);
      msg += L" verified.";
      co_await host->Dialogs().ShowErrorAsync(L"Test passed", msg);
    }
    else
    {
      UString t = NPropertyFormatter::FormatCount(result.Errors, L"error");
      t += L" in ";
      t += displayName;
      co_await host->Dialogs().ShowMessagesAsync(t, op->Messages);
    }
  }
  catch (const hresult_error &e)
  {
    host->Dialogs().ShowErrorAsync(L"Test failed", ToUString(e.message()));
  }
}

// ---------------- modification ----------------

IAsyncAction CArchiveViewModel::AddAsync(UStringVector paths)
{
  auto strong = get_strong();
  if (paths.IsEmpty())
    co_return;
  CUpdatePlan plan;
  FOR_VECTOR (i, paths)
    plan.Add.Add(us2fs(paths[i]));
  plan.TargetFolder = _currentPath;
  UString title (L"Adding ");
  if (paths.Size() == 1)
    title += GetFileNamePart(paths[0]);
  else
  {
    title += UInt32ToUString(paths.Size());
    title += L" items";
  }
  co_await UpdateAsync(plan, title);
}

IAsyncAction CArchiveViewModel::DeleteAsync(CItemVector items)
{
  auto strong = get_strong();
  if (items.IsEmpty() || !_session)
    co_return;
  if (g_App.Settings.Current.ConfirmDelete)
  {
    UString what;
    if (items.Size() == 1)
      what = Quoted(items[0]->Name);
    else
    {
      what = L"these ";
      what += UInt32ToUString(items.Size());
      what += L" items";
    }
    UString msg (L"Delete ");
    msg += what;
    msg += L" from ";
    msg += DisplayName;
    msg += L"? This can't be undone.";
    const bool confirmed = co_await _host->Dialogs().ConfirmAsync(L"Delete from archive?", msg, L"Delete", true);
    if (!confirmed || _disposed)
      co_return;
  }
  CUpdatePlan plan;
  UString basePath;
  Resolve(items, plan.Delete, basePath);
  if (plan.Delete.IsEmpty())
    co_return;
  UString title (L"Deleting ");
  if (items.Size() == 1)
    title += items[0]->Name;
  else
  {
    title += UInt32ToUString(items.Size());
    title += L" items";
  }
  co_await UpdateAsync(plan, title);
}

IAsyncAction CArchiveViewModel::RenameAsync(com_ptr<CItemViewModel> item)
{
  auto strong = get_strong();
  if (!_session)
    co_return;
  UStringVector siblings;
  {
    const CArchiveFolder &f = GetCurrentFolder();
    for (unsigned i = 0; i < f.GetNumFolders(); i++)
      siblings.Add(f.GetFolder(i).Name);
    FOR_VECTOR (i, f.Files)
      siblings.Add(f.Files[i]->GetName());
  }
  const UString oldName = item->Name;
  const hstring result = co_await _host->Dialogs().PromptTextAsync(L"Rename", L"New name", oldName, L"Rename",
      [siblings, oldName](const UString &n) -> UString
      {
        if (HasInvalidNameChars(n))
          return UString(k_InvalidNameMessage);
        if (!n.IsEqualTo_NoCase(oldName))
          FOR_VECTOR (i, siblings)
            if (siblings[i].IsEqualTo_NoCase(n))
              return UString(L"There's already an item with that name.");
        return UString();
      });
  const UString name = ToUString(result);
  if (_disposed || name.IsEmpty() || name == oldName)
    co_return;
  const UString parent = ParentPathOf(item->Path);
  UString newPath;
  if (!parent.IsEmpty())
  {
    newPath = parent;
    newPath.Add_Char('\\');
  }
  newPath += name;
  CUpdatePlan plan;
  CRenamePair &r = plan.Rename.AddNew();
  r.From = item->Path;
  r.To = newPath;
  UString title (L"Renaming ");
  title += oldName;
  co_await UpdateAsync(plan, title);
}

IAsyncAction CArchiveViewModel::NewFolderAsync()
{
  auto strong = get_strong();
  if (!_session)
    co_return;
  // the validator keeps the session (and so the folder) alive
  com_ptr<CArchiveSessionHolder> holder = _session;
  const CArchiveFolder *folder = &GetCurrentFolder();
  const hstring result = co_await _host->Dialogs().PromptTextAsync(L"New folder", L"Folder name",
      L"New folder", L"Create",
      [holder, folder](const UString &n) -> UString
      {
        if (HasInvalidNameChars(n))
          return UString(k_InvalidNameMessage);
        if (folder->FindFolder(n))
          return UString(L"That folder already exists.");
        return UString();
      });
  const UString name = ToUString(result);
  if (_disposed || name.IsEmpty())
    co_return;
  UString path;
  if (!_currentPath.IsEmpty())
  {
    path = _currentPath;
    path.Add_Char('\\');
  }
  path += name;
  CUpdatePlan plan;
  plan.NewFolders.Add(path);
  UString title (L"Creating ");
  title += name;
  co_await UpdateAsync(plan, title);
}

IAsyncAction CArchiveViewModel::UpdateAsync(CUpdatePlan plan, UString title)
{
  auto strong = get_strong();
  IWindowHost *host = _host;
  if (!_session || !CanModify())
  {
    co_await host->Dialogs().ShowErrorAsync(L"Read-only archive", GetReadOnlyReason());
    co_return;
  }
  com_ptr<CArchiveSessionHolder> holder = _session;
  CArchiveSession *session = &holder->Session;
  const UString keepPath = _currentPath;
  const bool password_Defined = session->Password_Defined;
  const UString password = session->Password;
  com_ptr<COperationViewModel> op = NewOperation(NOperationKind::kUpdate, title, FilePath);
  CCompressResult result;
  UString done (DisplayName);
  done += L" updated";
  try
  {
    const HRESULT hr = co_await host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage)
        {
          return NArchiveWriter::Update(*session, plan, NULL, ctx, result, errorMessage);
        },
        [&] { return done; },
        [&] { return FilePath; });
    if (hr == S_OK)
    {
      if (op->Messages.Size() != 0)
        co_await host->Dialogs().ShowMessagesAsync(UString(L"Updated with warnings"), op->Messages);
    }
    else if (hr != E_ABORT)
      co_await host->Dialogs().ShowErrorAsync(L"Couldn't update the archive", op->GetErrorMessage());
  }
  catch (const hresult_error &e)
  {
    host->Dialogs().ShowErrorAsync(L"Couldn't update the archive", ToUString(e.message()));
  }
  if (_disposed)
    co_return;
  // Update() closes the session on success; reopen either way so the view reflects the file on disk.
  session->Close();
  if (_session == holder)
    _session = nullptr;
  co_await OpenAsync(password_Defined, password);
  if (!_disposed)
    Navigate(keepPath);
}

void CArchiveViewModel::Dispose()
{
  _disposed = true;
  InterlockedIncrement(&_searchGeneration);
  _watchers.Clear();
  // Running operations keep their own references; the sessions close when they finish.
  _items.Clear();
  _itemsVector = single_threaded_observable_vector<IInspectable>();
  _details.Clear();
  _session = nullptr;
  _outer = nullptr;
}
