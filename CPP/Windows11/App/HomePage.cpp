// HomePage.cpp

#include "StdAfx.h"

#include "HomePage.h"

#include "../../Windows/FileFind.h"

#include "../Command.h"
#include "../HString.h"
#include "../Xaml.h"

#include "../Core/ArchiveFormat.h"

#include "App.h"
#include "PageFactory.h"
#include "ShellServices.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace NWindows;
using namespace NWindows11;

// One row of the recent list (C# RecentItem).

class CRecentItem: public CBindableT<CRecentItem>
{
  UString _path;
  UString _when;
  com_ptr<CCommand> _showInFolderCommand;
  com_ptr<CCommand> _removeCommand;

  static UString FormatWhen(const FILETIME &utc);
public:
  CRecentItem(CHomePage *page, const CRecentArchive &r);
  const UString &GetPath() const { return _path; }
};

UString CRecentItem::FormatWhen(const FILETIME &utc)
{
  SYSTEMTIME u, l, now;
  if (!::FileTimeToSystemTime(&utc, &u) || !::SystemTimeToTzSpecificLocalTime(NULL, &u, &l))
    return UString();
  ::GetLocalTime(&now);
  wchar_t buf[128];
  buf[0] = 0;
  // today: the short time ("t"), else the short date ("d")
  if (l.wYear == now.wYear && l.wMonth == now.wMonth && l.wDay == now.wDay)
    ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &l, NULL, buf, Z7_ARRAY_SIZE(buf));
  else
    ::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &l, NULL, buf, Z7_ARRAY_SIZE(buf), NULL);
  return UString(buf);
}

CRecentItem::CRecentItem(CHomePage *page, const CRecentArchive &r):
    _path(r.Path),
    _when(FormatWhen(r.LastOpened))
{
  const UString path = _path;
  _showInFolderCommand = make_self<CCommand>([page, path] { page->RevealRecent(path); });
  _removeCommand = make_self<CCommand>([page, path] { page->RemoveRecent(path); });

  AddProperty(L"Path", [this] { return BoxString(_path); });
  AddProperty(L"Name", [this] { return BoxString(UString(_path.Ptr((unsigned)(_path.ReverseFind_PathSepar() + 1)))); });
  AddProperty(L"Folder", [this]
  {
    UString folder (_path);
    const int slash = folder.ReverseFind_PathSepar();
    folder.DeleteFrom(slash < 0 ? 0 : (unsigned)slash);
    return BoxString(folder);
  });
  AddProperty(L"Icon", [this] { return NShellIcons::GetIconForPath(_path).as<CInspectable>(); });
  AddProperty(L"When", [this] { return BoxString(_when); });
  AddProperty(L"ShowInFolderCommand", [this] { return _showInFolderCommand.as<CInspectable>(); });
  AddProperty(L"RemoveCommand", [this] { return _removeCommand.as<CInspectable>(); });
}


CTabPageRef CreateHomePage(IWindowHost *host)
{
  com_ptr<CHomePage> page = make_self<CHomePage>(host);
  page->Init();
  return MakeUiObjectRef<ITabPage>(page);
}


void CHomePage::Init()
{
  _root = NXaml::Load<UserControl>(IDX_HOME_PAGE);
  _root.DataContext(*this);
  _recentList = NXaml::FindName<ListView>(_root, L"RecentList");
  _emptyRecentText = NXaml::FindName<TextBlock>(_root, L"EmptyRecentText");
  _clearRecentButton = NXaml::FindName<HyperlinkButton>(_root, L"ClearRecentButton");
  _dropZone = NXaml::FindName<Border>(_root, L"DropZone");

  NXaml::FindName<Button>(_root, L"OpenButton").Click([this](auto &&, auto &&) { OnOpen(); });
  NXaml::FindName<Button>(_root, L"CompressButton").Click([this](auto &&, auto &&) { OnCompress(); });
  NXaml::FindName<Button>(_root, L"HashButton").Click([this](auto &&, auto &&) { _host->OpenHash(UStringVector(), UString()); });
  NXaml::FindName<Button>(_root, L"BenchmarkButton").Click([this](auto &&, auto &&) { _host->OpenBenchmark(); });
  _clearRecentButton.Click([this](auto &&, auto &&)
  {
    g_App.Settings.ClearRecent();
    LoadRecent();
  });
  _recentList.ItemClick([this](const IInspectable &, const ItemClickEventArgs &e)
  {
    const CRecentItem *item = get_self<CRecentItem>(e.ClickedItem().as<Data::ICustomPropertyProvider>());
    _host->OpenArchive(item->GetPath(), UString());
  });

  _root.DragOver([this](const IInspectable &, const DragEventArgs &e)
  {
    if (!e.DataView().Contains(StandardDataFormats::StorageItems()))
      return;
    e.AcceptedOperation(DataPackageOperation::Copy);
    e.DragUIOverride().Caption(L"Open or compress");
    const ResourceDictionary res = Application::Current().Resources();
    _dropZone.BorderBrush(res.Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Media::Brush>());
    _dropZone.Background(res.Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Media::Brush>());
  });
  _root.DragLeave([this](auto &&, auto &&) { ResetDropZone(); });
  _root.Drop([this](const IInspectable &, const DragEventArgs &e) { OnDrop(e); });

  LoadRecent();
}

void CHomePage::LoadRecent()
{
  auto items = single_threaded_observable_vector<IInspectable>();
  const CObjectVector<CRecentArchive> &recent = g_App.Settings.Current.Recent;
  FOR_VECTOR (i, recent)
    if (NFile::NFind::DoesFileExist_FollowLink(us2fs(recent[i].Path)))
      items.Append(make<CRecentItem>(this, recent[i]));
  _recentList.ItemsSource(items);
  const bool empty = (items.Size() == 0);
  _emptyRecentText.Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
  _clearRecentButton.Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
}

void CHomePage::ResetDropZone()
{
  _dropZone.BorderBrush(Application::Current().Resources().Lookup(
      box_value(L"ControlStrongStrokeColorDefaultBrush")).as<Media::Brush>());
  _dropZone.Background(nullptr);
}

void CHomePage::RevealRecent(const UString &path)
{
  NShellActions::RevealInExplorer(path);
}

void CHomePage::RemoveRecent(const UString &path)
{
  // rebuild the list after the command that asked for it has returned
  com_ptr<CHomePage> strong = get_strong();
  g_App.Dispatcher.TryEnqueue([strong, path]
  {
    g_App.Settings.RemoveRecent(path);
    if (!strong->_closed)
      strong->LoadRecent();
  });
}

fire_and_forget CHomePage::OnOpen()
{
  com_ptr<CHomePage> strong = get_strong();
  const hstring path = co_await NPickers::PickArchiveAsync(_host->GetWindowId());
  if (!path.empty() && !_closed)
    _host->OpenArchive(ToUString(path), UString());
}

fire_and_forget CHomePage::OnCompress()
{
  com_ptr<CHomePage> strong = get_strong();
  const auto files = co_await NPickers::PickFilesAsync(_host->GetWindowId(), UString(L"Choose files to compress"));
  if (_closed)
    co_return;
  UStringVector paths;
  NPickers::ToUStringVector(files, paths);
  // like the C# app: an empty selection still opens "Create archive"
  _host->OpenCompress(paths, UString());
}

fire_and_forget CHomePage::OnDrop(DragEventArgs e)
{
  com_ptr<CHomePage> strong = get_strong();
  ResetDropZone();
  if (!e.DataView().Contains(StandardDataFormats::StorageItems()))
    co_return;
  const auto items = co_await e.DataView().GetStorageItemsAsync();
  if (_closed)
    co_return;
  UStringVector paths;
  for (const auto &item: items)
  {
    const hstring p = item.Path();
    if (!p.empty())
      paths.Add(ToUString(p));
  }
  if (paths.IsEmpty())
    co_return;
  bool allArchives = true;
  FOR_VECTOR (i, paths)
    if (!NFile::NFind::DoesFileExist_FollowLink(us2fs(paths[i])) || !NFormatRegistry::LooksLikeArchive(paths[i]))
    {
      allArchives = false;
      break;
    }
  if (allArchives)
  {
    FOR_VECTOR (i, paths)
      _host->OpenArchive(paths[i], UString());
  }
  else
    _host->OpenCompress(paths, UString());
}


// ITabPage

NPageKind::EEnum CHomePage::GetPageKind() { return NPageKind::kHome; }
FrameworkElement CHomePage::GetRoot() { return _root; }
UString CHomePage::GetTabTitle() { return UString(L"Home"); }

IconSource CHomePage::GetTabIcon()
{
  FontIconSource icon;
  icon.Glyph(L"\xE80F");
  return icon;
}

UString CHomePage::GetArchivePath() { return UString(); }
void CHomePage::OnTabClosed() { _closed = true; }
