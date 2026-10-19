// ArchivePage.cpp

#include "StdAfx.h"

#include "ArchivePage.h"

#include "../../Common/IntToString.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"

#include "../HString.h"
#include "../Xaml.h"

#include "../Core/PathSanitizer.h"

#include "App.h"
#include "DialogService.h"
#include "PlatformServices.h"
#include "ShellServices.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::System;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace NWindows;
using namespace NWindows11;

static const double k_DetailsPaneWidth = 300;
static const double k_DetailsMinPageWidth = 820;

CTabPageRef CreateArchivePage(IWindowHost *host, const UString &path, const UString &format,
    CArchiveViewModel *parent, const UString &pathInParent)
{
  com_ptr<CArchiveViewModel> vm = make_self<CArchiveViewModel>(host, path, parent, pathInParent, format);
  com_ptr<CArchivePage> page = make_self<CArchivePage>(host, vm);
  page->Init();
  vm->OpenAsync();
  return MakeUiObjectRef<ITabPage>(page);
}

static UString Quoted(const UString &s)
{
  UString r (L"\x201C");
  r += s;
  r += L"\x201D";
  return r;
}

static UString UInt32ToUString(UInt32 v)
{
  wchar_t s[16];
  ConvertUInt32ToString(v, s);
  return UString(s);
}

static IAsyncOperation<IVectorView<IStorageItem> > ToStorageItemsAsync(UStringVector paths)
{
  std::vector<IStorageItem> list;
  FOR_VECTOR (i, paths)
  {
    const hstring p = ToHString(paths[i]);
    if (NFile::NFind::DoesDirExist(us2fs(paths[i])))
      list.push_back((co_await StorageFolder::GetFolderFromPathAsync(p)).as<IStorageItem>());
    else
      list.push_back((co_await StorageFile::GetFileFromPathAsync(p)).as<IStorageItem>());
  }
  co_return single_threaded_vector<IStorageItem>(std::move(list)).GetView();
}

// ---------------- CColumnLayout ----------------

static const wchar_t * const k_ColumnNames[4] = { L"ModifiedWidth", L"TypeWidth", L"SizeWidth", L"PackedWidth" };

CColumnLayout::CColumnLayout()
{
  _widths[0] = 170;
  _widths[1] = 160;
  _widths[2] = 100;
  _widths[3] = 100;
  for (unsigned i = 0; i < 4; i++)
    AddProperty(k_ColumnNames[i], [this, i] { return box_value(GridLengthHelper::FromPixels(_widths[i])); });
}

void CColumnLayout::Fit(double width)
{
  double w[4];
  w[0] = width < 620 ? 0 : 170;
  w[1] = width < 900 ? 0 : 160;
  w[2] = width < 460 ? 0 : 100;
  w[3] = width < 760 ? 0 : 100;
  for (unsigned i = 0; i < 4; i++)
    if (_widths[i] != w[i])
    {
      _widths[i] = w[i];
      RaisePropertyChanged(k_ColumnNames[i]);
    }
}

void CColumnLayout::Bind(const Grid &grid)
{
  IVector<ColumnDefinition> cols = grid.ColumnDefinitions();
  if (cols.Size() < 5)
    return;
  for (unsigned i = 0; i < 4; i++)
  {
    Binding b;
    b.Source(*this);
    b.Path(PropertyPath(k_ColumnNames[i]));
    b.Mode(BindingMode::OneWay);
    BindingOperations::SetBinding(cols.GetAt(i + 1), ColumnDefinition::WidthProperty(), b);
  }
}

// ---------------- CArchivePage ----------------

CArchivePage::CArchivePage(IWindowHost *host, const com_ptr<CArchiveViewModel> &vm):
    _host(host),
    _vm(vm),
    _closed(false)
{
  _columns = make_self<CColumnLayout>();
}

void CArchivePage::OnClick(const wchar_t *name, const std::function<void()> &action)
{
  NXaml::FindName<Primitives::ButtonBase>(_root, name).Click([action](const CInspectable &, const RoutedEventArgs &) { action(); });
}

void CArchivePage::OnMenuClick(const wchar_t *name, const std::function<void()> &action)
{
  NXaml::FindName<MenuFlyoutItem>(_root, name).Click([action](const CInspectable &, const RoutedEventArgs &) { action(); });
}

void CArchivePage::AddAccelerator(VirtualKey key, VirtualKeyModifiers modifiers,
    const std::function<void(const KeyboardAcceleratorInvokedEventArgs &)> &action)
{
  KeyboardAccelerator a;
  a.Key(key);
  a.Modifiers(modifiers);
  a.Invoked([action](const KeyboardAccelerator &, const KeyboardAcceleratorInvokedEventArgs &args) { action(args); });
  _root.KeyboardAccelerators().Append(a);
}

void CArchivePage::Init()
{
  _root = NXaml::Load<FrameworkElement>(IDX_ARCHIVE_PAGE);
  _root.DataContext(_vm.as<IInspectable>());

  _toolbar = NXaml::FindName<CommandBar>(_root, L"Toolbar");
  _searchBox = NXaml::FindName<AutoSuggestBox>(_root, L"SearchBox");
  _breadcrumbs = NXaml::FindName<BreadcrumbBar>(_root, L"Breadcrumbs");
  _itemsList = NXaml::FindName<ListView>(_root, L"ItemsList");
  _detailsPane = NXaml::FindName<ScrollViewer>(_root, L"DetailsPane");
  _detailsColumn = NXaml::FindName<ColumnDefinition>(_root, L"DetailsColumn");
  _detailsToggle = NXaml::FindName<AppBarToggleButton>(_root, L"DetailsToggle");
  _renameButton = NXaml::FindName<AppBarButton>(_root, L"RenameButton");
  _deleteButton = NXaml::FindName<AppBarButton>(_root, L"DeleteButton");
  _extractSmartItem = NXaml::FindName<MenuFlyoutItem>(_root, L"ExtractSmartItem");
  _menuOpen = NXaml::FindName<MenuFlyoutItem>(_root, L"MenuOpen");
  _menuOpenWith = NXaml::FindName<MenuFlyoutItem>(_root, L"MenuOpenWith");
  _menuOpenInside = NXaml::FindName<MenuFlyoutItem>(_root, L"MenuOpenInside");
  _menuRename = NXaml::FindName<MenuFlyoutItem>(_root, L"MenuRename");
  _menuDelete = NXaml::FindName<MenuFlyoutItem>(_root, L"MenuDelete");

  _columns->Bind(NXaml::FindName<Grid>(_root, L"ColumnHeader"));

  // page events (handlers capture this: the page outlives its controls' events)
  _root.SizeChanged([this](const CInspectable &, const SizeChangedEventArgs &e) { OnSizeChanged(e.NewSize().Width); });
  _root.DragOver([this](const CInspectable &, const DragEventArgs &e) { OnDragOver(e); });
  _root.Drop([this](const CInspectable &, const DragEventArgs &e) { OnDropAsync(e); });

  // list
  _itemsList.SelectionChanged([this](const CInspectable &, const SelectionChangedEventArgs &) { OnSelectionChanged(); });
  _itemsList.DoubleTapped([this](const CInspectable &, const DoubleTappedRoutedEventArgs &e) { OnItemsDoubleTapped(e); });
  _itemsList.KeyDown([this](const CInspectable &, const KeyRoutedEventArgs &e) { OnItemsKeyDown(e); });
  _itemsList.AddHandler(UIElement::RightTappedEvent(),
      box_value(RightTappedEventHandler([this](const CInspectable &, const RightTappedRoutedEventArgs &e) { OnItemRightTapped(e); })),
      true);
  _itemsList.DragItemsStarting([this](const CInspectable &, const DragItemsStartingEventArgs &e) { OnDragItemsStarting(e); });
  _itemsList.ContainerContentChanging([this](const ListViewBase &, const ContainerContentChangingEventArgs &args)
      { OnContainerContentChanging(args); });
  NXaml::FindName<MenuFlyout>(_root, L"ItemMenu").Opening([this](const CInspectable &, const CInspectable &) { OnItemMenuOpening(); });

  // navigation
  _breadcrumbs.ItemClicked([this](const BreadcrumbBar &, const BreadcrumbBarItemClickedEventArgs &args)
  {
    // The last crumb is the current folder; clicking it does nothing (F5 refreshes).
    const UStringVector &paths = _vm->GetBreadcrumbPaths();
    const int index = args.Index();
    if (index >= 0 && (unsigned)index + 1 < paths.Size())
      _vm->Navigate(paths[(unsigned)index]);
  });
  OnClick(L"UpButton", [this] { _vm->GoUp(); });
  OnClick(L"RetryButton", [this] { _vm->OpenAsync(); });

  // column headers
  OnClick(L"SortNameButton", [this] { _vm->SetSort(NSortColumn::kName); });
  OnClick(L"SortModifiedButton", [this] { _vm->SetSort(NSortColumn::kModified); });
  OnClick(L"SortTypeButton", [this] { _vm->SetSort(NSortColumn::kType); });
  OnClick(L"SortSizeButton", [this] { _vm->SetSort(NSortColumn::kSize); });
  OnClick(L"SortPackedButton", [this] { _vm->SetSort(NSortColumn::kPacked); });

  // toolbar
  OnMenuClick(L"ExtractToItem", [this] { ExtractWithDialogAsync(GetSelection()); });
  OnMenuClick(L"ExtractSmartItem", [this] { _vm->ExtractSmartAsync(GetSelection()); });
  OnMenuClick(L"ExtractHereItem", [this] { _vm->ExtractHereAsync(GetSelection()); });
  OnMenuClick(L"AddFilesItem", [this] { AddFilesAsync(false); });
  OnMenuClick(L"AddFolderItem", [this] { AddFilesAsync(true); });
  OnMenuClick(L"NewFolderItem", [this] { _vm->NewFolderAsync(); });
  _renameButton.Click([this](const CInspectable &, const RoutedEventArgs &)
  {
    const CItemVector sel = GetSelection();
    if (sel.Size() == 1)
      _vm->RenameAsync(sel[0]);
  });
  _deleteButton.Click([this](const CInspectable &, const RoutedEventArgs &) { _vm->DeleteAsync(GetSelection()); });
  OnClick(L"ShareButton", [this] { ShareAsync(); });
  OnClick(L"TestButton", [this] { _vm->TestAsync(); });
  _detailsToggle.Click([this](const CInspectable &, const RoutedEventArgs &) { OnDetailsToggleClick(); });
  OnClick(L"RevealArchiveButton", [this]
  {
    CArchiveViewModel *parent = _vm->GetParent();
    NShellActions::RevealInExplorer(parent ? parent->FilePath : _vm->FilePath);
  });
  OnClick(L"CopyPathButton", [this] { OnCopyPath(); });
  OnClick(L"HashArchiveButton", [this]
  {
    UStringVector paths;
    paths.Add(_vm->FilePath);
    _host->OpenHash(paths, UString());
  });
  OnClick(L"RefreshButton", [this] { _vm->RefreshAsync(); });

  // context menu
  OnMenuClick(L"MenuOpen", [this]
  {
    const CItemVector sel = GetSelection();
    if (sel.Size() == 1)
      _vm->OpenItemAsync(sel[0]);
  });
  OnMenuClick(L"MenuOpenWith", [this]
  {
    const CItemVector sel = GetSelection();
    if (sel.Size() == 1)
      _vm->OpenItemAsync(sel[0], false, true);
  });
  OnMenuClick(L"MenuOpenInside", [this]
  {
    const CItemVector sel = GetSelection();
    if (sel.Size() == 1)
      _vm->OpenInsideAsync(sel[0]);
  });
  OnMenuClick(L"MenuExtract", [this] { ExtractWithDialogAsync(GetSelection()); });
  OnMenuClick(L"MenuCopy", [this] { CopyToClipboardAsync(); });
  OnMenuClick(L"MenuShare", [this] { ShareAsync(); });
  _menuRename.Click([this](const CInspectable &, const RoutedEventArgs &)
  {
    const CItemVector sel = GetSelection();
    if (sel.Size() == 1)
      _vm->RenameAsync(sel[0]);
  });
  _menuDelete.Click([this](const CInspectable &, const RoutedEventArgs &) { _vm->DeleteAsync(GetSelection()); });
  OnMenuClick(L"MenuProperties", [this] { OnMenuProperties(); });

  // keyboard
  AddAccelerator(VirtualKey::E, VirtualKeyModifiers::Control, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    args.Handled(true);
    ExtractWithDialogAsync(GetSelection());
  });
  AddAccelerator(VirtualKey::F, VirtualKeyModifiers::Control, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    _searchBox.Focus(FocusState::Keyboard);
    args.Handled(true);
  });
  AddAccelerator(VirtualKey::Up, VirtualKeyModifiers::Menu, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    _vm->GoUp();
    args.Handled(true);
  });
  AddAccelerator(VirtualKey::F5, VirtualKeyModifiers::None, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    args.Handled(true);
    _vm->RefreshAsync();
  });
  AddAccelerator(VirtualKey::C, VirtualKeyModifiers::Control, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    if (IsSearchBoxFocused())
      return;
    args.Handled(true);
    CopyToClipboardAsync();
  });
  AddAccelerator(VirtualKey::V, VirtualKeyModifiers::Control, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    if (IsSearchBoxFocused() || !_vm->CanModify())
      return;
    DataPackageView content { nullptr };
    try
    {
      content = Clipboard::GetContent();
    }
    catch (const hresult_error &)
    {
      return;
    }
    if (!content || !content.Contains(StandardDataFormats::StorageItems()))
      return;
    args.Handled(true);
    PasteAsync(content);
  });
  AddAccelerator(VirtualKey::F6, VirtualKeyModifiers::None, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    MovePaneFocus(1);
    args.Handled(true);
  });
  AddAccelerator(VirtualKey::F6, VirtualKeyModifiers::Shift, [this](const KeyboardAcceleratorInvokedEventArgs &args)
  {
    MovePaneFocus(-1);
    args.Handled(true);
  });

  _detailsToggle.IsChecked(IReference<bool>(g_App.Settings.Current.ShowDetailsPane));
  ApplyDetailsVisibility();
  _vmChangedToken = _vm->PropertyChanged([this](const CInspectable &, const PropertyChangedEventArgs &e)
  {
    OnViewModelChanged(e.PropertyName());
  });
}

// ---------------- ITabPage ----------------

NPageKind::EEnum CArchivePage::GetPageKind()
{
  return NPageKind::kArchive;
}

FrameworkElement CArchivePage::GetRoot()
{
  return _root;
}

UString CArchivePage::GetTabTitle()
{
  return _vm->DisplayName;
}

IconSource CArchivePage::GetTabIcon()
{
  ImageIconSource icon;
  icon.ImageSource(NShellIcons::GetIconForPath(_vm->FilePath));
  return icon;
}

UString CArchivePage::GetArchivePath()
{
  return _vm->GetParent() ? UString() : _vm->FilePath;
}

void CArchivePage::OnTabClosed()
{
  if (_closed)
    return;
  _closed = true;
  _vm->PropertyChanged(_vmChangedToken);
  _vm->Dispose();
}

// ---------------- helpers ----------------

CItemVector CArchivePage::GetSelection() const
{
  CItemVector v;
  for (const CInspectable &obj: _itemsList.SelectedItems())
  {
    CItemViewModel *item = CItemViewModel::FromObject(obj);
    if (item)
    {
      com_ptr<CItemViewModel> p;
      p.copy_from(item);
      v.Add(p);
    }
  }
  return v;
}

// Ctrl+C / Ctrl+V in the search box edit its text. The focus is on the box's inner TextBox.
bool CArchivePage::IsSearchBoxFocused() const
{
  if (!_root.XamlRoot())
    return false;
  for (DependencyObject e = FocusManager::GetFocusedElement(_root.XamlRoot()).try_as<DependencyObject>();
      e; e = VisualTreeHelper::GetParent(e))
    if (e == _searchBox)
      return true;
  return false;
}

bool CArchivePage::IsDetailsToggleChecked() const
{
  const IReference<bool> v = _detailsToggle.IsChecked();
  return v && v.Value();
}

void CArchivePage::OnViewModelChanged(const hstring &propertyName)
{
  if (propertyName != L"IsLoading" || _vm->IsLoading() || _closed)
    return;
  UString text (L"Extract to ");
  text += Quoted(_vm->GetSmartFolderName());
  _extractSmartItem.Text(ToHString(text));
  if (_vm->GetLoadError().IsEmpty())
    _itemsList.Focus(FocusState::Programmatic);
}

// ---------------- layout ----------------

void CArchivePage::OnSizeChanged(double width)
{
  const bool details = IsDetailsToggleChecked() && width >= k_DetailsMinPageWidth;
  _detailsPane.Visibility(details ? Visibility::Visible : Visibility::Collapsed);
  _detailsColumn.Width(GridLengthHelper::FromPixels(details ? k_DetailsPaneWidth : 0));
  _columns->Fit(width - (details ? 330 : 30));
}

void CArchivePage::ApplyDetailsVisibility()
{
  const double width = _root.ActualWidth();
  const bool on = IsDetailsToggleChecked() && (width == 0 || width >= k_DetailsMinPageWidth);
  _detailsPane.Visibility(on ? Visibility::Visible : Visibility::Collapsed);
  _detailsColumn.Width(GridLengthHelper::FromPixels(on ? k_DetailsPaneWidth : 0));
  if (width > 0)
    _columns->Fit(width - (on ? 330 : 30));
}

void CArchivePage::OnDetailsToggleClick()
{
  g_App.Settings.Current.ShowDetailsPane = IsDetailsToggleChecked();
  g_App.Settings.Save();
  ApplyDetailsVisibility();
}

// F6 / Shift+F6 cycle the major regions: toolbar, search, location, file list, details.
void CArchivePage::MovePaneFocus(int step)
{
  std::vector<Control> panes;
  panes.push_back(_toolbar);
  panes.push_back(_searchBox);
  panes.push_back(_breadcrumbs);
  panes.push_back(_itemsList);
  if (_detailsPane.Visibility() == Visibility::Visible)
    panes.push_back(_detailsPane);
  const int count = (int)panes.size();

  int current = -1;
  DependencyObject e = FocusManager::GetFocusedElement(_root.XamlRoot()).try_as<DependencyObject>();
  for (; e && current < 0; e = VisualTreeHelper::GetParent(e))
    for (int i = 0; i < count; i++)
      if (panes[(size_t)i] == e)
        current = i;

  const int start = current < 0 ? (step > 0 ? -1 : 0) : current;
  for (int i = 1; i <= count; i++)
  {
    const Control &target = panes[(size_t)((((start + step * i) % count) + count) % count)];
    if (!target.IsEnabled())
      continue;
    // Containers such as the command bar and details pane aren't tab stops themselves.
    if (target.Focus(FocusState::Keyboard))
      return;
    const UIElement first = FocusManager::FindFirstFocusableElement(target).try_as<UIElement>();
    if (first && first.Focus(FocusState::Keyboard))
      return;
  }
}

// ---------------- list ----------------

void CArchivePage::OnContainerContentChanging(const ContainerContentChangingEventArgs &args)
{
  if (args.InRecycleQueue())
    return;
  // Containers are recycled with their template root: bind each row's columns once.
  const Grid row = args.ItemContainer().ContentTemplateRoot().try_as<Grid>();
  if (!row || row.Tag())
    return;
  row.Tag(box_value(true));
  _columns->Bind(row);
}

void CArchivePage::OnSelectionChanged()
{
  const CItemVector selection = GetSelection();
  _vm->ShowDetails(selection);
  _renameButton.IsEnabled(_vm->CanModify() && selection.Size() == 1);
  _deleteButton.IsEnabled(_vm->CanModify() && selection.Size() > 0);
}

void CArchivePage::OnItemsDoubleTapped(const DoubleTappedRoutedEventArgs &e)
{
  const FrameworkElement fe = e.OriginalSource().try_as<FrameworkElement>();
  if (!fe)
    return;
  CItemViewModel *item = CItemViewModel::FromObject(fe.DataContext());
  if (!item)
    return;
  com_ptr<CItemViewModel> p;
  p.copy_from(item);
  _vm->OpenItemAsync(p);
}

void CArchivePage::OnItemsKeyDown(const KeyRoutedEventArgs &e)
{
  const CItemVector selection = GetSelection();
  const VirtualKey key = e.Key();
  if (key == VirtualKey::Enter)
  {
    if (selection.Size() == 1)
    {
      e.Handled(true);
      _vm->OpenItemAsync(selection[0]);
    }
  }
  else if (key == VirtualKey::Back)
  {
    e.Handled(true);
    _vm->GoUp();
  }
  else if (key == VirtualKey::Delete)
  {
    if (selection.Size() > 0 && _vm->CanModify())
    {
      e.Handled(true);
      _vm->DeleteAsync(selection);
    }
  }
  else if (key == VirtualKey::F2)
  {
    if (selection.Size() == 1 && _vm->CanModify())
    {
      e.Handled(true);
      _vm->RenameAsync(selection[0]);
    }
  }
}

// Right-clicking an unselected row selects just that row before the menu opens.
void CArchivePage::OnItemRightTapped(const RightTappedRoutedEventArgs &e)
{
  const FrameworkElement fe = e.OriginalSource().try_as<FrameworkElement>();
  if (!fe)
    return;
  const CInspectable obj = fe.DataContext();
  if (!CItemViewModel::FromObject(obj))
    return;
  uint32_t index;
  if (!_itemsList.SelectedItems().IndexOf(obj, index))
  {
    _itemsList.SelectedItems().Clear();
    _itemsList.SelectedItem(obj);
  }
}

void CArchivePage::OnItemMenuOpening()
{
  const CItemVector selection = GetSelection();
  const bool single = selection.Size() == 1;
  const bool file = single && !selection[0]->IsFolder;
  _menuOpen.Visibility(single ? Visibility::Visible : Visibility::Collapsed);
  _menuOpenWith.Visibility(file ? Visibility::Visible : Visibility::Collapsed);
  // archives open inside on double-click anyway; this is for archives with other extensions
  _menuOpenInside.Visibility(file && !NFormatRegistry::LooksLikeArchive(selection[0]->Name)
      ? Visibility::Visible : Visibility::Collapsed);
  _menuRename.IsEnabled(single && _vm->CanModify());
  _menuDelete.IsEnabled(selection.Size() > 0 && _vm->CanModify());
}

// ---------------- commands ----------------

fire_and_forget CArchivePage::ExtractWithDialogAsync(CItemVector selection)
{
  auto strong = get_strong();
  if (!_vm->GetSession())
    co_return;
  try
  {
    const CExtractOptionsPanelRef panel = _vm->CreateExtractOptions(selection);
    UString title (L"Extract ");
    if (selection.IsEmpty())
      title += _vm->DisplayName;
    else if (selection.Size() == 1)
      title += Quoted(selection[0]->Name);
    else
    {
      title += UInt32ToUString(selection.Size());
      title += L" items";
    }
    ContentDialog dialog;
    dialog.Title(BoxString(title));
    dialog.Content(panel->GetRoot());
    dialog.PrimaryButtonText(L"Extract");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(ContentDialogButton::Primary);
    dialog.PrimaryButtonClick([panel](const ContentDialog &, const ContentDialogButtonClickEventArgs &args)
    {
      if (!panel->ValidateAndShowError())
        args.Cancel(true);
    });
    const ContentDialogResult result = co_await _host->Dialogs().ShowAsync(dialog);
    if (result != ContentDialogResult::Primary || _closed)
      co_return;
    panel->SaveDefaults();
    CExtractOptions options;
    panel->GetOptions(UString(), options);
    co_await _vm->ExtractAsync(selection, options, panel->GetOpenFolderWhenDone());
  }
  catch (const hresult_error &)
  {
  }
}

fire_and_forget CArchivePage::AddFilesAsync(bool folders)
{
  auto strong = get_strong();
  try
  {
    UString title (folders ? L"Add folders to " : L"Add files to ");
    title += _vm->DisplayName;
    const IVectorView<hstring> result = folders ?
        co_await NPickers::PickFoldersAsync(_host->GetWindowId(), title) :
        co_await NPickers::PickFilesAsync(_host->GetWindowId(), title);
    if (_closed)
      co_return;
    UStringVector paths;
    if (result)
      NPickers::ToUStringVector(result, paths);
    co_await _vm->AddAsync(paths);
  }
  catch (const hresult_error &)
  {
  }
}

fire_and_forget CArchivePage::ShareAsync()
{
  auto strong = get_strong();
  const CItemVector selection = GetSelection();
  if (selection.IsEmpty())
  {
    UStringVector paths;
    paths.Add(_vm->FilePath);
    NShareService::ShareFiles(_host->GetHwnd(), paths, _vm->DisplayName);
    co_return;
  }
  try
  {
    UStringVector paths;
    co_await _vm->ExtractToTempAsync(selection, false, &paths);
    if (_closed || paths.IsEmpty())
      co_return;
    UString title;
    if (selection.Size() == 1)
      title = selection[0]->Name;
    else
    {
      title = UInt32ToUString(selection.Size());
      title += L" items from ";
      title += _vm->DisplayName;
    }
    NShareService::ShareFiles(_host->GetHwnd(), paths, title);
  }
  catch (const hresult_error &)
  {
  }
}

// Copy puts real files on the clipboard, so Ctrl+V in File Explorer pastes them.
fire_and_forget CArchivePage::CopyToClipboardAsync()
{
  auto strong = get_strong();
  const CItemVector selection = GetSelection();
  if (selection.IsEmpty())
    co_return;
  try
  {
    UStringVector paths;
    co_await _vm->ExtractToTempAsync(selection, false, &paths);
    if (_closed || paths.IsEmpty())
      co_return;
    DataPackage package;
    package.RequestedOperation(DataPackageOperation::Copy);
    package.SetStorageItems(co_await ToStorageItemsAsync(paths));
    Clipboard::SetContent(package);
  }
  catch (const hresult_error &)
  {
  }
}

fire_and_forget CArchivePage::PasteAsync(DataPackageView content)
{
  auto strong = get_strong();
  try
  {
    const IVectorView<IStorageItem> items = co_await content.GetStorageItemsAsync();
    if (_closed)
      co_return;
    UStringVector paths;
    for (const IStorageItem &item: items)
    {
      const hstring path = item.Path();
      if (!path.empty())
        paths.Add(ToUString(path));
    }
    co_await _vm->AddAsync(paths);
  }
  catch (const hresult_error &)
  {
  }
}

void CArchivePage::OnMenuProperties()
{
  if (!IsDetailsToggleChecked())
  {
    _detailsToggle.IsChecked(IReference<bool>(true));
    OnDetailsToggleClick();
  }
}

void CArchivePage::OnCopyPath()
{
  CArchiveViewModel *parent = _vm->GetParent();
  DataPackage package;
  package.SetText(ToHString(parent ? parent->FilePath : _vm->FilePath));
  try
  {
    Clipboard::SetContent(package);
  }
  catch (const hresult_error &)
  {
  }
}

// ---------------- drag & drop ----------------

// Drag out to File Explorer: files are extracted only when the drop happens (delayed rendering).
void CArchivePage::OnDragItemsStarting(const DragItemsStartingEventArgs &e)
{
  CItemVector items;
  for (const CInspectable &obj: e.Items())
  {
    CItemViewModel *item = CItemViewModel::FromObject(obj);
    if (item)
    {
      com_ptr<CItemViewModel> p;
      p.copy_from(item);
      items.Add(p);
    }
  }
  if (items.IsEmpty())
  {
    e.Cancel(true);
    return;
  }
  const DataPackage data = e.Data();
  data.RequestedOperation(DataPackageOperation::Copy);
  UString title;
  if (items.Size() == 1)
    title = items[0]->Name;
  else
  {
    title = UInt32ToUString(items.Size());
    title += L" items";
  }
  data.Properties().Title(ToHString(title));
  const weak_ref<CArchivePage> weak = get_weak();
  data.SetDataProvider(StandardDataFormats::StorageItems(), [weak, items](const DataProviderRequest &request)
  {
    const DataProviderDeferral deferral = request.GetDeferral();
    const bool queued = g_App.Dispatcher.TryEnqueue([weak, items, request, deferral]
    {
      if (com_ptr<CArchivePage> page = weak.get())
        page->ProvideDragDataAsync(items, request, deferral);
      else
        deferral.Complete();
    });
    if (!queued)
      deferral.Complete();
  });
}

fire_and_forget CArchivePage::ProvideDragDataAsync(CItemVector items, DataProviderRequest request,
    DataProviderDeferral deferral)
{
  auto strong = get_strong();
  try
  {
    UStringVector paths;
    co_await _vm->ExtractToTempAsync(items, true, &paths);
    request.SetData(co_await ToStorageItemsAsync(paths));
  }
  catch (const hresult_error &)
  {
  }
  deferral.Complete();
}

void CArchivePage::OnDragOver(const DragEventArgs &e)
{
  if (!e.DataView().Contains(StandardDataFormats::StorageItems()) || !_vm->CanModify())
    return;
  e.AcceptedOperation(DataPackageOperation::Copy);
  const UString &path = _vm->GetCurrentPath();
  UString caption (L"Add to ");
  caption += path.IsEmpty() ? _vm->DisplayName : GetFileNamePart(path);
  e.DragUIOverride().Caption(ToHString(caption));
}

fire_and_forget CArchivePage::OnDropAsync(DragEventArgs e)
{
  auto strong = get_strong();
  if (!e.DataView().Contains(StandardDataFormats::StorageItems()) || !_vm->CanModify())
    co_return;
  const DragOperationDeferral deferral = e.GetDeferral();
  IVectorView<IStorageItem> items { nullptr };
  try
  {
    items = co_await e.DataView().GetStorageItemsAsync();
  }
  catch (const hresult_error &)
  {
  }
  deferral.Complete();
  if (!items || _closed)
    co_return;

  // our own drag-out (extracted to the temp folder) dropped back onto the window
  FString tempPath;
  NFile::NDir::MyGetTempPath(tempPath);
  UStringVector paths;
  for (const IStorageItem &item: items)
  {
    const UString p = ToUString(item.Path());
    if (p.IsEmpty() || (!tempPath.IsEmpty() && p.IsPrefixedBy_NoCase(fs2us(tempPath))))
      continue;
    paths.Add(p);
  }
  if (paths.IsEmpty())
    co_return;
  const UString &current = _vm->GetCurrentPath();
  UString msg (L"Add ");
  if (paths.Size() == 1)
    msg += Quoted(GetFileNamePart(paths[0]));
  else
  {
    msg += UInt32ToUString(paths.Size());
    msg += L" items";
  }
  msg += L" to ";
  msg += current.IsEmpty() ? _vm->DisplayName : current;
  msg += L"?";
  try
  {
    const bool confirmed = co_await _host->Dialogs().ConfirmAsync(L"Add to archive?", msg, L"Add");
    if (confirmed && !_closed)
      co_await _vm->AddAsync(paths);
  }
  catch (const hresult_error &)
  {
  }
}
