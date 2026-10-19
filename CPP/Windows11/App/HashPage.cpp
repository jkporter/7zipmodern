// HashPage.cpp

#include "StdAfx.h"

#include "HashPage.h"

#include "../../Common/UTFConvert.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/FileIO.h"

#include "../HString.h"
#include "../Xaml.h"

#include "../Core/PathSanitizer.h"

#include "DialogService.h"
#include "PageFactory.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace NWindows;
using namespace NWindows11;

CTabPageRef CreateHashPage(IWindowHost *host, const UStringVector &paths, const UString &algorithm)
{
  com_ptr<CHashPage> page = make_self<CHashPage>(host, paths, algorithm);
  page->Init();
  return MakeUiObjectRef<ITabPage>(page);
}

CHashPage::CHashPage(IWindowHost *host, const UStringVector &paths, const UString &algorithm):
    _host(host),
    _vm(make_self<CHashViewModel>(paths, algorithm)),
    _closed(false)
    {}

static void SetItems(const FrameworkElement &root, const wchar_t *name, const CUiItemVector &items)
{
  NXaml::FindName<Controls::ItemsControl>(root, name).ItemsSource(items);
}

void CHashPage::Init()
{
  _root = NXaml::Load<FrameworkElement>(IDX_HASH_PAGE);
  SetItems(_root, L"SourcesList", _vm->Sources);
  SetItems(_root, L"AlgorithmsList", _vm->Algorithms);
  SetItems(_root, L"ResultsList", _vm->Results);
  _root.DataContext(*_vm);

  _verifyBar = NXaml::FindName<Controls::InfoBar>(_root, L"VerifyBar");
  NXaml::FindName<Controls::Button>(_root, L"AddFilesButton").Click([this](auto &&, auto &&) { OnAddFilesClick(); });
  NXaml::FindName<Controls::Button>(_root, L"ClearButton").Click([this](auto &&, auto &&)
  {
    _vm->ClearSources();
    _vm->ClearResults();
  });
  NXaml::FindName<Controls::Button>(_root, L"ComputeButton").Click([this](auto &&, auto &&) { OnComputeClick(); });
  NXaml::FindName<Controls::Button>(_root, L"CopyAllButton").Click([this](auto &&, auto &&)
  {
    CopyTextToClipboard(_vm->CopyAllText());
  });
  NXaml::FindName<Controls::Button>(_root, L"ExportButton").Click([this](auto &&, auto &&) { OnExportClick(); });
  NXaml::FindName<Controls::AutoSuggestBox>(_root, L"ExpectedBox").QuerySubmitted([this](auto &&, auto &&) { OnPasteQuery(); });

  _root.DragOver([this](auto &&, const DragEventArgs &e) { OnDragOver(e); });
  _root.Drop([this](auto &&, const DragEventArgs &e) { OnDrop(e); });

  _vmChangedToken = _vm->PropertyChanged([this](auto &&, const Data::PropertyChangedEventArgs &e)
  {
    if (e.PropertyName() == L"VerifyOk")
      UpdateVerifyBar();
  });

  // Opened with files (Explorer verb, Home page): compute right away.
  _root.Loaded([this](auto &&, auto &&)
  {
    if (!_closed && _vm->GetNumSources() != 0 && _vm->GetNumResults() == 0 && !_vm->GetIsWorking())
      OnComputeClick();
  });
}

// ---- ITabPage ----

NPageKind::EEnum CHashPage::GetPageKind() { return NPageKind::kHash; }

FrameworkElement CHashPage::GetRoot() { return _root; }

UString CHashPage::GetTabTitle() { return UString(L"Checksums"); }

Controls::IconSource CHashPage::GetTabIcon()
{
  Controls::FontIconSource icon;
  icon.Glyph(L"\xE73E");
  return icon;
}

UString CHashPage::GetArchivePath() { return UString(); }

void CHashPage::OnTabClosed()
{
  _closed = true;
  _vm->Closed = true;
  _vm->PropertyChanged(_vmChangedToken);
}

// ---- handlers ----

void CHashPage::UpdateVerifyBar()
{
  Controls::InfoBarSeverity severity = Controls::InfoBarSeverity::Informational;
  switch (_vm->GetVerifyState())
  {
    case NVerifyState::kMatch: severity = Controls::InfoBarSeverity::Success; break;
    case NVerifyState::kNoMatch: severity = Controls::InfoBarSeverity::Error; break;
    case NVerifyState::kNone:
    default: break;
  }
  _verifyBar.Severity(severity);
}

fire_and_forget CHashPage::OnAddFilesClick()
{
  auto strong = get_strong();
  try
  {
    const auto paths = co_await NPickers::PickFilesAsync(_host->GetWindowId(), UString(L"Choose files to check"));
    if (_closed)
      co_return;
    UStringVector list;
    NPickers::ToUStringVector(paths, list);
    FOR_VECTOR (i, list)
      _vm->AddSource(list[i]);
  }
  catch (const hresult_error &) {}
}

fire_and_forget CHashPage::OnComputeClick()
{
  auto strong = get_strong();
  try
  {
    co_await _vm->ComputeAsync(_host);
  }
  catch (const hresult_error &) {}
}

// C# string.Trim()
static void TrimWhiteSpace(UString &s)
{
  unsigned start = 0;
  while (start < s.Len() && (s[start] == ' ' || (s[start] >= 9 && s[start] <= 13)))
    start++;
  s.DeleteFrontal(start);
  while (!s.IsEmpty() && (s.Back() == ' ' || (s.Back() >= 9 && s.Back() <= 13)))
    s.DeleteBack();
}

fire_and_forget CHashPage::OnPasteQuery()
{
  auto strong = get_strong();
  try
  {
    const DataPackageView content = Clipboard::GetContent();
    if (!content.Contains(StandardDataFormats::Text()))
      co_return;
    const hstring text = co_await content.GetTextAsync();
    if (_closed)
      co_return;
    UString expected = ToUString(text);
    TrimWhiteSpace(expected);
    _vm->SetExpected(expected);
  }
  catch (const hresult_error &) {}
}

fire_and_forget CHashPage::OnExportClick()
{
  auto strong = get_strong();
  try
  {
    const CAlgorithmOption *algorithm = _vm->GetExportAlgorithm();
    if (!algorithm || _vm->GetNumResults() == 0)
      co_return;
    const UString name = algorithm->Info.Name;
    UString ext = name;
    ext.MakeLower_Ascii();
    ext.RemoveChar(L'-');
    const UString first = fs2us(_vm->GetResult(0).Path);
    UString suggested = GetDirectoryPart(first);
    if (!suggested.IsEmpty() && !IS_PATH_SEPAR(suggested.Back()))
      suggested.Add_PathSepar();
    suggested += (_vm->GetNumResults() == 1 ? GetFileNamePart(first) : UString(L"checksums"));
    suggested += L'.';
    suggested += ext;
    UString label = name;
    label += L" checksum file";

    const hstring path = co_await NPickers::PickSaveAsync(_host->GetWindowId(), suggested, label, ext);
    if (_closed || path.empty())
      co_return;

    AString utf8; // UTF-8 without BOM, like C# File.WriteAllTextAsync
    ConvertUnicodeToUTF8(_vm->ExportText(name), utf8);
    bool ok;
    DWORD error = 0;
    {
      NFile::NIO::COutFile file;
      ok = file.Create_ALWAYS(us2fs(ToUString(path))) && file.WriteFull(utf8.Ptr(), utf8.Len());
      if (!ok)
        error = ::GetLastError();
    }
    if (!ok)
      co_await _host->Dialogs().ShowErrorAsync(L"Couldn't save the checksum file", NError::MyFormatMessage(error));
  }
  catch (const hresult_error &) {}
}

void CHashPage::OnDragOver(const DragEventArgs &e)
{
  if (e.DataView().Contains(StandardDataFormats::StorageItems()))
  {
    e.AcceptedOperation(DataPackageOperation::Copy);
    e.DragUIOverride().Caption(L"Compute checksums");
  }
}

fire_and_forget CHashPage::OnDrop(DragEventArgs e)
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
        _vm->AddSource(ToUString(path));
    }
    co_await _vm->ComputeAsync(_host);
  }
  catch (const hresult_error &) {}
}
