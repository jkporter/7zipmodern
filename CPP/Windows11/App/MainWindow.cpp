// MainWindow.cpp

#include "StdAfx.h"

#include "MainWindow.h"

#include "../../Windows/DLL.h"

#include "../HString.h"
#include "../Xaml.h"

#include "App.h"
#include "PageFactory.h"
#include "ThemeHelper.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::System;
using namespace NWindows;
using namespace NWindows11;

CMainWindow::CMainWindow():
    _hwnd(NULL),
    _nextTabId(1),
    _closeConfirmed(false)
{
  _root = NXaml::Load<Grid>(IDX_MAIN_WINDOW);
  _tabView = NXaml::FindName<TabView>(_root, L"Tabs");
  _captionSpacer = NXaml::FindName<ColumnDefinition>(_root, L"CaptionSpacer");
  _activityRing = NXaml::FindName<ProgressRing>(_root, L"ActivityRing");
  _noActivityText = NXaml::FindName<TextBlock>(_root, L"NoActivityText");
  _window.Content(_root);
  _window.as<IWindowNative>()->get_WindowHandle(&_hwnd);

  _window.ExtendsContentIntoTitleBar(true);
  _window.AppWindow().TitleBar().PreferredHeightOption(Windowing::TitleBarHeightOption::Tall);
  _window.SetTitleBar(NXaml::FindName<UIElement>(_root, L"DragRegion"));
  FString icon = NDLL::GetModuleDirPrefix();
  icon += "Assets\\7zip.ico";
  _window.AppWindow().SetIcon(ToHString(fs2us(icon)));
  _window.AppWindow().Title(L"7-Zip");

  const double scale = ::GetDpiForWindow(_hwnd) / 96.0;
  _window.AppWindow().Resize(winrt::Windows::Graphics::SizeInt32 { (int32_t)(1180 * scale), (int32_t)(760 * scale) });
  CenterOnScreen();

  _dialogs.Init(_root, _hwnd);
  _operations.Init(_hwnd);
  ListView activityList = NXaml::FindName<ListView>(_root, L"ActivityList");
  activityList.ItemsSource(_operations.GetOperations());
  _operations.GetOperations().VectorChanged([this](const auto &, const Collections::IVectorChangedEventArgs &args)
  {
    if (args.CollectionChange() == Collections::CollectionChange::ItemInserted)
      _operations.GetOperation(args.Index())->StateChanged([this] { UpdateActivity(); });
    UpdateActivity();
  });

  _root.ActualThemeChanged([this](auto &&, auto &&) { NThemeHelper::UpdateCaptionButtons(_window); });
  _window.AppWindow().Changed([this](const Windowing::AppWindow &, const Windowing::AppWindowChangedEventArgs &e)
  {
    if (e.DidSizeChange())
      UpdateCaptionSpacer();
  });
  _root.Loaded([this](auto &&, auto &&) { UpdateCaptionSpacer(); });

  NXaml::FindName<Button>(_root, L"SettingsButton").Click([this](auto &&, auto &&) { OpenSettings(); });
  _tabView.AddTabButtonClick([this](auto &&, auto &&) { AddTab(CreateHomePage(this)); });
  _tabView.TabCloseRequested([this](const TabView &, const TabViewTabCloseRequestedEventArgs &args)
  {
    const int i = FindTab(args.Tab());
    if (i >= 0)
      CloseTabAt((unsigned)i);
  });
  _tabView.SelectionChanged([this](auto &&, auto &&)
  {
    const int i = GetSelectedTab();
    if (i >= 0)
      SetWindowTitle(ToUString(unbox_value_or<hstring>(_tabs[(unsigned)i].Item.Header(), hstring())));
  });

  const VirtualKeyModifiers ctrl = VirtualKeyModifiers::Control;
  AddAccelerator(VirtualKey::T, ctrl, [this] { AddTab(CreateHomePage(this)); });
  const std::function<void()> closeCurrent = [this]
  {
    const int i = GetSelectedTab();
    if (i >= 0)
      CloseTabAt((unsigned)i);
  };
  AddAccelerator(VirtualKey::W, ctrl, closeCurrent);
  AddAccelerator(VirtualKey::F4, ctrl, closeCurrent);
  AddAccelerator(VirtualKey::Tab, ctrl, [this]
  {
    const int n = (int)_tabView.TabItems().Size();
    if (n > 0)
      _tabView.SelectedIndex((_tabView.SelectedIndex() + 1) % n);
  });
  AddAccelerator(VirtualKey::Tab, ctrl | VirtualKeyModifiers::Shift, [this]
  {
    const int n = (int)_tabView.TabItems().Size();
    if (n > 0)
      _tabView.SelectedIndex((_tabView.SelectedIndex() - 1 + n) % n);
  });
  AddAccelerator(VirtualKey::O, ctrl, [this] { OnOpenAccelerator(); });
  AddAccelerator(VirtualKey::N, ctrl, [this] { OnNewArchiveAccelerator(); });
  AddAccelerator((VirtualKey)0xBC, ctrl, [this] { OpenSettings(); }); // Ctrl+,
  // Ctrl+1...Ctrl+8 select that tab, Ctrl+9 the last one (TabView guidance)
  for (int n = 1; n <= 9; n++)
    AddAccelerator((VirtualKey)((int)VirtualKey::Number0 + n), ctrl, [this, n]
    {
      const int count = (int)_tabView.TabItems().Size();
      const int target = (n == 9) ? count - 1 : n - 1;
      if (target >= 0 && target < count)
        _tabView.SelectedIndex(target);
    });

  _window.AppWindow().Closing([this](const Windowing::AppWindow &, const Windowing::AppWindowClosingEventArgs &args)
  {
    OnClosing(args);
  });
  _window.Closed([](auto &&, auto &&) { g_App.MainWindow = NULL; });

  UpdateActivity();
}


void CMainWindow::AddAccelerator(VirtualKey key, VirtualKeyModifiers modifiers, const std::function<void()> &action)
{
  winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator a;
  a.Key(key);
  a.Modifiers(modifiers);
  a.Invoked([action](const winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator &,
      const winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs &e)
  {
    e.Handled(true);
    action();
  });
  _root.KeyboardAccelerators().Append(a);
}

void CMainWindow::CenterOnScreen()
{
  const Windowing::AppWindow appWindow = _window.AppWindow();
  const winrt::Windows::Graphics::RectInt32 area = Windowing::DisplayArea::GetFromWindowId(appWindow.Id(),
      Windowing::DisplayAreaFallback::Nearest).WorkArea();
  const winrt::Windows::Graphics::SizeInt32 size = appWindow.Size();
  appWindow.Move(winrt::Windows::Graphics::PointInt32 { area.X + (area.Width - size.Width) / 2, area.Y + (area.Height - size.Height) / 2 });
}

void CMainWindow::UpdateCaptionSpacer()
{
  const XamlRoot xamlRoot = _root.XamlRoot();
  const double scale = xamlRoot ? xamlRoot.RasterizationScale() : 1.0;
  double width = _window.AppWindow().TitleBar().RightInset() / scale;
  if (width < 0)
    width = 0;
  _captionSpacer.Width(GridLengthHelper::FromPixels(width));
}

void CMainWindow::UpdateActivity()
{
  _activityRing.IsActive(_operations.HasActive());
  _noActivityText.Visibility(_operations.GetNumOperations() == 0 ? Visibility::Visible : Visibility::Collapsed);
}

void CMainWindow::SetWindowTitle(const UString &tabTitle)
{
  UString s (tabTitle);
  s += L" \x2013 7-Zip";
  _window.AppWindow().Title(ToHString(s));
}


fire_and_forget CMainWindow::OnClosing(Windowing::AppWindowClosingEventArgs args)
{
  if (_closeConfirmed || !_operations.HasActive())
  {
    CloseAllTabs();
    co_return;
  }
  args.Cancel(true);
  const bool ok = co_await _dialogs.ConfirmAsync(UString(L"Operations are still running"),
      UString(L"Closing 7-Zip will cancel them. Partially written archives are removed."),
      UString(L"Cancel them and close"), true);
  if (!ok)
    co_return;
  _operations.CancelAll();
  _closeConfirmed = true;
  CloseAllTabs();
  _window.Close();
}

void CMainWindow::CloseAllTabs()
{
  for (size_t i = 0; i < _tabs.size(); i++)
  {
    const CTabPageRef &page = _tabs[i].Page;
    page->OnTabClosed();
    const FrameworkElement root = page->GetRoot();
    if (root)
      root.DataContext(nullptr);
  }
  _tabs.clear();
  _tabView.TabItems().Clear();
}

void CMainWindow::BringToFront()
{
  const Windowing::OverlappedPresenter p = _window.AppWindow().Presenter().try_as<Windowing::OverlappedPresenter>();
  if (p && p.State() == Windowing::OverlappedPresenterState::Minimized)
    p.Restore();
  _window.Activate();
  ::SetForegroundWindow(_hwnd);
}


// ---------------- activation ----------------

void CMainWindow::Handle(const CLaunchRequest &request)
{
  const NLaunchVerb::EEnum verb = request.Verb;
  if (verb == NLaunchVerb::kOpen)
  {
    FOR_VECTOR (i, request.Paths)
      OpenArchive(request.Paths[i], request.Format);
  }
  else if (verb == NLaunchVerb::kCompress && request.ShareOperation)
    HandleShare(request.ShareOperation);
  else if (verb == NLaunchVerb::kCompress)
    OpenCompress(request.Paths, UString());
  else if (verb == NLaunchVerb::kHash)
    OpenHash(request.Paths, request.HashAlgorithm);
  else if (verb == NLaunchVerb::kBenchmark)
    OpenBenchmark();
  else if (verb == NLaunchVerb::kSettings)
    OpenSettings();
  if (_tabView.TabItems().Size() == 0)
    AddTab(CreateHomePage(this));
}

// Files shared to 7-Zip from another app's Share sheet become a new archive.
fire_and_forget CMainWindow::HandleShare(winrt::Windows::ApplicationModel::DataTransfer::ShareTarget::ShareOperation share)
{
  using namespace winrt::Windows::ApplicationModel::DataTransfer;
  try
  {
    if (share.Data().Contains(StandardDataFormats::StorageItems()))
    {
      const auto items = co_await share.Data().GetStorageItemsAsync();
      share.ReportDataRetrieved();
      UStringVector paths;
      for (const auto &item: items)
      {
        const hstring p = item.Path();
        if (!p.empty())
          paths.Add(ToUString(p));
      }
      if (!paths.IsEmpty())
        OpenCompress(paths, UString());
    }
  }
  catch (...) {}
  try { share.ReportCompleted(); }
  catch (...) {}
}

fire_and_forget CMainWindow::OnOpenAccelerator()
{
  const hstring path = co_await NPickers::PickArchiveAsync(GetWindowId());
  if (!path.empty())
    OpenArchive(ToUString(path), UString());
}

fire_and_forget CMainWindow::OnNewArchiveAccelerator()
{
  const auto files = co_await NPickers::PickFilesAsync(GetWindowId(), UString(L"Choose files to compress"));
  UStringVector paths;
  NPickers::ToUStringVector(files, paths);
  if (!paths.IsEmpty())
    OpenCompress(paths, UString());
}


// ---------------- tabs ----------------

int CMainWindow::FindTab(const ITabPage *page) const
{
  for (size_t i = 0; i < _tabs.size(); i++)
    if (_tabs[i].Page.Interface == page)
      return (int)i;
  return -1;
}

int CMainWindow::FindTab(const IInspectable &item) const
{
  if (!item)
    return -1;
  const UInt32 id = unbox_value_or<UInt32>(item.as<TabViewItem>().Tag(), 0);
  for (size_t i = 0; i < _tabs.size(); i++)
    if (_tabs[i].Id == id)
      return (int)i;
  return -1;
}

int CMainWindow::GetSelectedTab() const
{
  return FindTab(_tabView.SelectedItem());
}

bool CMainWindow::SelectExisting(NPageKind::EEnum kind)
{
  for (size_t i = 0; i < _tabs.size(); i++)
    if (_tabs[i].Page->GetPageKind() == kind)
    {
      _tabView.SelectedItem(_tabs[i].Item);
      return true;
    }
  return false;
}

void CMainWindow::AddTab(const CTabPageRef &page, bool replaceHome, bool insertAfterCurrent)
{
  if (page.IsEmpty())
    return;
  const UString title = page->GetTabTitle();
  CTab tab;
  tab.Id = _nextTabId++;
  tab.Page = page;
  tab.Item = TabViewItem();
  tab.Item.Header(BoxString(title));
  tab.Item.IconSource(page->GetTabIcon());
  tab.Item.Content(page->GetRoot());
  tab.Item.Tag(box_value(tab.Id));
  tab.Item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
  tab.Item.VerticalContentAlignment(VerticalAlignment::Stretch);
  ToolTipService::SetToolTip(tab.Item, BoxString(title));

  const auto items = _tabView.TabItems();
  const int selected = GetSelectedTab();
  // Opening something from a Home tab replaces it, like a browser's new-tab page.
  if (replaceHome && selected >= 0 && _tabs[(unsigned)selected].Page->GetPageKind() == NPageKind::kHome)
  {
    CTab home = _tabs[(unsigned)selected];
    UInt32 index = 0;
    items.IndexOf(home.Item, index);
    _tabs.erase(_tabs.begin() + selected);
    home.Page->OnTabClosed();
    _tabs.push_back(tab);
    items.SetAt(index, tab.Item);
    const FrameworkElement root = home.Page->GetRoot();
    if (root)
      root.DataContext(nullptr);
  }
  else
  {
    _tabs.push_back(tab);
    const int current = _tabView.SelectedIndex();
    if (insertAfterCurrent && current >= 0)
      items.InsertAt((UInt32)current + 1, tab.Item);
    else
      items.Append(tab.Item);
  }
  _tabView.SelectedItem(tab.Item);
}

void CMainWindow::CloseTabAt(unsigned index)
{
  const CTab tab = _tabs[index];
  _tabs.erase(_tabs.begin() + index);
  tab.Page->OnTabClosed();
  UInt32 i = 0;
  if (_tabView.TabItems().IndexOf(tab.Item, i))
    _tabView.TabItems().RemoveAt(i);
  const FrameworkElement root = tab.Page->GetRoot();
  if (root)
    root.DataContext(nullptr);
  if (_tabs.empty())
  {
    if (_operations.HasActive())
      AddTab(CreateHomePage(this));
    else
      _window.Close();
  }
}


// ---------------- IWindowHost ----------------

HWND CMainWindow::GetHwnd() { return _hwnd; }
WindowId CMainWindow::GetWindowId() { return _window.AppWindow().Id(); }
CDialogService &CMainWindow::Dialogs() { return _dialogs; }
COperationService &CMainWindow::Operations() { return _operations; }

void CMainWindow::OpenArchive(const UString &path, const UString &format)
{
  for (size_t i = 0; i < _tabs.size(); i++)
  {
    const UString p = _tabs[i].Page->GetArchivePath();
    if (!p.IsEmpty() && p.IsEqualTo_NoCase(path))
    {
      _tabView.SelectedItem(_tabs[i].Item);
      return;
    }
  }
  AddTab(CreateArchivePage(this, path, format, NULL, UString()), true);
}

void CMainWindow::OpenNestedArchive(CArchiveViewModel *parent, const UString &extractedPath, const UString &pathInParent)
{
  AddTab(CreateArchivePage(this, extractedPath, UString(), parent, pathInParent), false, true);
}

void CMainWindow::OpenCompress(const UStringVector &paths, const UString &targetArchive)
{
  AddTab(CreateCompressPage(this, paths, targetArchive), true);
}

void CMainWindow::OpenHash(const UStringVector &paths, const UString &algorithm)
{
  AddTab(CreateHashPage(this, paths, algorithm), true);
}

void CMainWindow::OpenBenchmark()
{
  if (!SelectExisting(NPageKind::kBenchmark))
    AddTab(CreateBenchmarkPage(this), true);
}

void CMainWindow::OpenSettings()
{
  if (!SelectExisting(NPageKind::kSettings))
    AddTab(CreateSettingsPage(this));
}

void CMainWindow::UpdateTab(ITabPage *page, const UString &title)
{
  const int i = FindTab(page);
  if (i < 0)
    return;
  const TabViewItem item = _tabs[(unsigned)i].Item;
  item.Header(BoxString(title));
  ToolTipService::SetToolTip(item, BoxString(title));
  if (GetSelectedTab() == i)
    SetWindowTitle(title);
}

void CMainWindow::CloseTab(ITabPage *page)
{
  const int i = FindTab(page);
  if (i >= 0)
    CloseTabAt((unsigned)i);
}
