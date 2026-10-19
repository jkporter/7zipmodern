// ArchiveViewModel.h

#ifndef ZIP7_INC_ARCHIVE_VIEW_MODEL_H
#define ZIP7_INC_ARCHIVE_VIEW_MODEL_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Bindable.h"
#include "../Command.h"

#include "../Core/ArchiveWriter.h"
#include "../Core/ExtractEngine.h"

#include "ItemViewModel.h"
#include "OperationViewModel.h"
#include "PageFactory.h"

namespace NSortColumn
{
  enum EEnum
  {
    kName,
    kModified,
    kType,
    kSize,
    kPacked
  };
}

class CArchiveViewModel;

/*
A file opened from the archive and changed by another app (C# PendingEdit),
shown as an "Update archive" InfoBar. Bindable: ArchivePath, ApplyCommand, DiscardCommand.
*/

class CPendingEdit: public NWindows11::CBindableT<CPendingEdit>
{
public:
  UInt32 Index;
  UString ArchivePath;
  FString TempPath;
  winrt::Microsoft::UI::Xaml::Input::ICommand ApplyCommand { nullptr };
  winrt::Microsoft::UI::Xaml::Input::ICommand DiscardCommand { nullptr };

  CPendingEdit(UInt32 index, const UString &archivePath, const FString &tempPath);
};


/*
Watches a file that was extracted to a temp folder and opened with its app
(C# FileSystemWatcher). When the file's size or time changes, the view model
gets AddPendingEdit() on the UI thread.
*/

class CEditWatcher
{
  HANDLE _change;
  PTP_WAIT _wait;
  LONG _stopping; // Interlocked*
  winrt::weak_ref<CArchiveViewModel> _owner;
  UInt32 _index;
  UString _archivePath;
  FString _path;
  // last seen state of the file (thread-pool callbacks only)
  UInt64 _size;
  FILETIME _mTime;

  static VOID CALLBACK WaitCallback(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WAIT wait, TP_WAIT_RESULT result);
  bool ReadState(UInt64 &size, FILETIME &mTime) const;
  void OnChange();

  Z7_CLASS_NO_COPY(CEditWatcher)
public:
  CEditWatcher(): _change(INVALID_HANDLE_VALUE), _wait(NULL), _stopping(0), _index(0), _size(0) {}
  ~CEditWatcher();
  bool Start(const winrt::weak_ref<CArchiveViewModel> &owner, UInt32 index, const UString &archivePath,
      const FString &tempPath);
};


/*
An open archive tab: navigation, search, sorting and every archive command
(C# ArchiveViewModel). DataContext of ArchivePage.xaml. UI thread only.
Bindable: Items, Breadcrumbs (names), PendingEdits, Details, CurrentPath, IsLoading,
LoadError, HasLoadError, StatusText, SelectionText, SearchText (TwoWay), IsSearching,
Warning, HasWarning, DetailsTitle, DetailsIcon, CanModify, ReadOnlyReason, CanGoUp,
EmptyText, EmptyVisible, ReadOnlyVisible, DisplayName,
{Name|Modified|Type|Size|Packed}SortGlyph / SortLabel.
The coroutines must be started on the UI thread; callers may await them or not.
*/

class CArchiveViewModel: public NWindows11::CBindableT<CArchiveViewModel>
{
  IWindowHost *_host;
  winrt::com_ptr<CArchiveViewModel> _parent;
  winrt::com_ptr<CArchiveSessionHolder> _session;
  winrt::com_ptr<CArchiveSessionHolder> _outer; // .tar.gz: the gzip layer kept alive under the tar
  CObjectVector<CEditWatcher> _watchers;
  LONG _searchGeneration; // Interlocked*: incremented to cancel a running search
  bool _disposed;

  CItemVector _items;
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> _itemsVector;
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> _breadcrumbs;
  UStringVector _breadcrumbPaths;
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> _pendingEditsVector;
  CObjectVector<winrt::com_ptr<CPendingEdit> > _pendingEdits;
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> _details;

  UString _currentPath;
  bool _isLoading;
  UString _loadError;
  UString _statusText;
  UString _selectionText;
  UString _searchText;
  bool _isSearching;
  NSortColumn::EEnum _sort;
  bool _sortDescending;
  UString _warning;
  UString _detailsTitle;
  winrt::Microsoft::UI::Xaml::Media::ImageSource _detailsIcon { nullptr };

  void SetString(UString &field, const UString &value, const wchar_t *name);
  void SetIsLoading(bool value);
  void SetLoadError(const UString &value);
  void SetIsSearching(bool value);
  void RaiseStateProperties();
  void RaiseSortProperties();
  void AddSortProperties(const wchar_t *glyphName, const wchar_t *labelName, NSortColumn::EEnum column, const wchar_t *label);
  UString GetSortGlyph(NSortColumn::EEnum column) const;
  UString GetSortLabel(NSortColumn::EEnum column, const wchar_t *label) const;
  UString GetEmptyText() const;
  bool GetEmptyVisible() const;
  bool GetReadOnlyVisible() const;

  UString GetRootFilePath() const;
  UString GetLastDisplayName() const;
  const CArchiveFolder &GetCurrentFolder() const;
  void Populate(const CArchiveFolder &folder);
  void SortItems();
  void PublishItems();
  void UpdateStatus();
  void SetSearchText(const UString &value);
  winrt::fire_and_forget SearchAsync(UString query);
  void AddDetail(const UString &name, const UString &value);

  // false: everything in the archive (C# indices == null)
  bool Resolve(const CItemVector &items, CRecordVector<UInt32> &indices, UString &basePath) const;
  void GetDefaultOptions(const FString &destination, CExtractOptions &options) const;
  winrt::com_ptr<COperationViewModel> NewOperation(NOperationKind::EEnum kind, const UString &title,
      const UString &archivePath);
  void WatchForEdits(UInt32 index, const UString &archivePath, const FString &tempPath);
  int FindPendingEdit(const CPendingEdit *edit) const;
  void RemovePendingEdit(unsigned index);
  winrt::Windows::Foundation::IAsyncAction UpdateAsync(CUpdatePlan plan, UString title);

public:
  UString FilePath;
  UString Format;       // "" = detect
  UString PathInParent;
  UString DisplayName;

  CArchiveViewModel(IWindowHost *host, const UString &path, CArchiveViewModel *parent,
      const UString &pathInParent, const UString &format);

  CArchiveViewModel *GetParent() const { return _parent.get(); }
  // NULL until the archive is open
  CArchiveSession *GetSession() const { return _session ? &_session->Session : NULL; }
  bool IsLoading() const { return _isLoading; }
  const UString &GetLoadError() const { return _loadError; }
  bool IsSearching() const { return _isSearching; }
  const UString &GetCurrentPath() const { return _currentPath; }
  const UStringVector &GetBreadcrumbPaths() const { return _breadcrumbPaths; }
  bool CanModify() const;
  UString GetReadOnlyReason() const;
  FString GetFolderForArchive() const;
  // "Extract to "name"" folder name of the toolbar menu
  UString GetSmartFolderName() const;

  // open
  winrt::Windows::Foundation::IAsyncAction OpenAsync(bool password_Defined = false, UString password = UString());
  winrt::Windows::Foundation::IAsyncAction RefreshAsync();

  // navigation, sorting, details
  void Navigate(const UString &path);
  bool CanGoUp() const { return !_currentPath.IsEmpty(); }
  void GoUp();
  void SetSort(NSortColumn::EEnum column);
  void ShowDetails(const CItemVector &selection);

  // extraction
  winrt::Windows::Foundation::IAsyncAction ExtractAsync(CItemVector items, CExtractOptions options, bool openFolder);
  winrt::Windows::Foundation::IAsyncAction ExtractHereAsync(CItemVector items);
  winrt::Windows::Foundation::IAsyncAction ExtractSmartAsync(CItemVector items);
  CExtractOptionsPanelRef CreateExtractOptions(const CItemVector &items);
  // Extracts to a private temp folder; used by open, drag-out, copy and share.
  // (paths) must stay valid until the returned action completes (await it).
  winrt::Windows::Foundation::IAsyncAction ExtractToTempAsync(CItemVector items, bool quiet, UStringVector *paths);
  winrt::Windows::Foundation::IAsyncAction OpenItemAsync(winrt::com_ptr<CItemViewModel> item,
      bool forceOutside = false, bool openWith = false);
  winrt::Windows::Foundation::IAsyncAction OpenInsideAsync(winrt::com_ptr<CItemViewModel> item);
  winrt::Windows::Foundation::IAsyncAction TestAsync();

  // edits of opened files
  void AddPendingEdit(UInt32 index, const UString &archivePath, const FString &tempPath);
  winrt::Windows::Foundation::IAsyncAction ApplyEditAsync(CPendingEdit *edit);
  void DiscardEdit(CPendingEdit *edit);

  // modification
  winrt::Windows::Foundation::IAsyncAction AddAsync(UStringVector paths);
  winrt::Windows::Foundation::IAsyncAction DeleteAsync(CItemVector items);
  winrt::Windows::Foundation::IAsyncAction RenameAsync(winrt::com_ptr<CItemViewModel> item);
  winrt::Windows::Foundation::IAsyncAction NewFolderAsync();

  void Dispose();
};

#endif
