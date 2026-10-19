// CompressPage.cpp

#include "StdAfx.h"

#include "CompressPage.h"

#include "../HString.h"
#include "../Xaml.h"

#include "../Core/PathSanitizer.h"

#include "App.h"
#include "PageFactory.h"
#include "PlatformServices.h"
#include "ShellServices.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace NWindows11;

CTabPageRef CreateCompressPage(IWindowHost *host, const UStringVector &paths, const UString &targetArchive)
{
  com_ptr<CCompressPage> page = make_self<CCompressPage>(host, paths, targetArchive);
  page->Init();
  return MakeUiObjectRef<ITabPage>(page);
}

CCompressPage::CCompressPage(IWindowHost *host, const UStringVector &paths, const UString &targetArchive):
    _host(host),
    _vm(make_self<CCompressViewModel>(paths, targetArchive)),
    _closed(false)
    {}

static void SetItems(const FrameworkElement &root, const wchar_t *name, const CUiItemVector &items)
{
  NXaml::FindName<Controls::ItemsControl>(root, name).ItemsSource(items);
}

void CCompressPage::Init()
{
  _root = NXaml::Load<FrameworkElement>(IDX_COMPRESS_PAGE);

  // The lists get their items before the DataContext, so the SelectedIndex bindings find them.
  SetItems(_root, L"SourcesList", _vm->Sources);
  SetItems(_root, L"FormatBox", _vm->Formats);
  SetItems(_root, L"LevelBox", _vm->LevelChoices);
  SetItems(_root, L"MethodBox", _vm->MethodChoices);
  SetItems(_root, L"DictionaryBox", _vm->DictionaryChoices);
  SetItems(_root, L"WordSizeBox", _vm->WordSizeChoices);
  SetItems(_root, L"SolidBox", _vm->SolidChoices);
  SetItems(_root, L"ThreadsBox", _vm->ThreadChoices);
  SetItems(_root, L"ZipEncryptionBox", _vm->ZipEncryptionChoices);
  SetItems(_root, L"VolumeBox", _vm->VolumeChoices);
  SetItems(_root, L"UpdateModeBox", _vm->UpdateModes);
  SetItems(_root, L"PathModeBox", _vm->PathModes);
  _root.DataContext(*_vm);

  _rememberCheck = NXaml::FindName<Controls::CheckBox>(_root, L"RememberCheck");
  NXaml::FindName<Controls::Button>(_root, L"AddFilesButton").Click([this](auto &&, auto &&) { OnAddFilesClick(); });
  NXaml::FindName<Controls::Button>(_root, L"AddFolderButton").Click([this](auto &&, auto &&) { OnAddFolderClick(); });
  NXaml::FindName<Controls::Button>(_root, L"BrowseButton").Click([this](auto &&, auto &&) { OnBrowseClick(); });
  NXaml::FindName<Controls::Button>(_root, L"CloseButton").Click([this](auto &&, auto &&) { _host->CloseTab(this); });
  NXaml::FindName<Controls::Button>(_root, L"CreateButton").Click([this](auto &&, auto &&) { OnCreateClick(); });

  // PasswordBox.Password is not reliably bindable TwoWay: forward the changes from code.
  NXaml::FindName<Controls::PasswordBox>(_root, L"PasswordBox").PasswordChanged(
      [this](const winrt::Windows::Foundation::IInspectable &sender, auto &&)
      { _vm->SetPassword(ToUString(sender.as<Controls::PasswordBox>().Password())); });
  NXaml::FindName<Controls::PasswordBox>(_root, L"ConfirmPasswordBox").PasswordChanged(
      [this](const winrt::Windows::Foundation::IInspectable &sender, auto &&)
      { _vm->SetConfirmPassword(ToUString(sender.as<Controls::PasswordBox>().Password())); });

  _root.DragOver([this](auto &&, const DragEventArgs &e) { OnDragOver(e); });
  _root.Drop([this](auto &&, const DragEventArgs &e) { OnDrop(e); });

  _vmChangedToken = _vm->PropertyChanged([this](auto &&, const Data::PropertyChangedEventArgs &e)
  {
    if (!_closed && e.PropertyName() == L"SourceSummary")
      _host->UpdateTab(this, GetTabTitle());
  });

  _vm->Start();
}

// ---- ITabPage ----

NPageKind::EEnum CCompressPage::GetPageKind() { return NPageKind::kCompress; }

FrameworkElement CCompressPage::GetRoot() { return _root; }

UString CCompressPage::GetTabTitle()
{
  if (!_vm->HasSources())
    return UString(L"New archive");
  UString s (L"New archive · ");
  s += _vm->GetSourceSummary();
  return s;
}

Controls::IconSource CCompressPage::GetTabIcon()
{
  Controls::FontIconSource icon;
  icon.Glyph(L"\xF012");
  return icon;
}

UString CCompressPage::GetArchivePath() { return UString(); }

void CCompressPage::OnTabClosed()
{
  _closed = true;
  _vm->Closed = true;
  _vm->PropertyChanged(_vmChangedToken);
}

// ---- handlers ----

void CCompressPage::AddSource(const UString &path)
{
  // the tab title follows SourceSummary (see Init)
  _vm->AddSource(path);
}

fire_and_forget CCompressPage::OnAddFilesClick()
{
  auto strong = get_strong();
  try
  {
    const auto paths = co_await NPickers::PickFilesAsync(_host->GetWindowId(), UString(L"Add files"));
    if (_closed)
      co_return;
    UStringVector list;
    NPickers::ToUStringVector(paths, list);
    FOR_VECTOR (i, list)
      AddSource(list[i]);
  }
  catch (const hresult_error &) {}
}

fire_and_forget CCompressPage::OnAddFolderClick()
{
  auto strong = get_strong();
  try
  {
    const auto paths = co_await NPickers::PickFoldersAsync(_host->GetWindowId(), UString(L"Add folders"));
    if (_closed)
      co_return;
    UStringVector list;
    NPickers::ToUStringVector(paths, list);
    FOR_VECTOR (i, list)
      AddSource(list[i]);
  }
  catch (const hresult_error &) {}
}

fire_and_forget CCompressPage::OnBrowseClick()
{
  auto strong = get_strong();
  try
  {
    const CFormatCapabilities &f = _vm->GetSelectedFormat();
    UString label (f.DisplayName);
    label += L" archive";
    const hstring path = co_await NPickers::PickSaveAsync(_host->GetWindowId(),
        _vm->GetArchivePath(), label, UString(f.Extension));
    if (_closed || path.empty())
      co_return;
    _vm->SetArchivePath(ToUString(path));
  }
  catch (const hresult_error &) {}
}

fire_and_forget CCompressPage::OnCreateClick()
{
  auto strong = get_strong();
  try
  {
    const winrt::Windows::Foundation::IReference<bool> remember = _rememberCheck.IsChecked();
    if (remember && remember.Value())
    {
      CAppSettings &s = g_App.Settings.Current;
      s.DefaultFormat = _vm->GetSelectedFormat().Id;
      s.DefaultLevel = _vm->GetLevel();
      s.DefaultEncryptFileNames = _vm->GetEncryptFileNames();
      g_App.Settings.Save();
    }

    const bool created = co_await _vm->CreateAsync(_host);
    if (!created || _closed)
      co_return;

    UStringVector files;
    FOR_VECTOR (i, _vm->LastResult.OutputFiles)
      files.Add(fs2us(_vm->LastResult.OutputFiles[i]));
    if (!files.IsEmpty())
    {
      if (_vm->GetShareAfter())
        NShareService::ShareFiles(_host->GetHwnd(), files, GetFileNamePart(files[0]));
      else
        NShellActions::RevealInExplorer(files);
    }
    _host->CloseTab(this);
  }
  catch (const hresult_error &) {}
}

void CCompressPage::OnDragOver(const DragEventArgs &e)
{
  if (e.DataView().Contains(StandardDataFormats::StorageItems()))
  {
    e.AcceptedOperation(DataPackageOperation::Copy);
    e.DragUIOverride().Caption(L"Add to the new archive");
  }
}

fire_and_forget CCompressPage::OnDrop(DragEventArgs e)
{
  auto strong = get_strong();
  try
  {
    if (!e.DataView().Contains(StandardDataFormats::StorageItems()))
      co_return;
    const DragOperationDeferral deferral = e.GetDeferral();
    const auto items = co_await e.DataView().GetStorageItemsAsync();
    deferral.Complete();
    if (_closed)
      co_return;
    for (const auto &item: items)
    {
      const hstring path = item.Path();
      if (!path.empty())
        AddSource(ToUString(path));
    }
  }
  catch (const hresult_error &) {}
}
