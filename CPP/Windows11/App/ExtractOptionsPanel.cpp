// ExtractOptionsPanel.cpp

#include "StdAfx.h"

#include "ExtractOptionsPanel.h"

#include "../../Windows/FileFind.h"

#include "../HString.h"
#include "../Xaml.h"

#include "../Core/PathSanitizer.h"

#include "App.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace NWindows;
using namespace NWindows11;

CExtractOptionsPanelRef CreateExtractOptionsPanel(IWindowHost *host, const FString &parentFolder,
    const UString &folderName, bool suggestSubfolder, bool needsPassword)
{
  com_ptr<CExtractOptionsPanel> panel = make_self<CExtractOptionsPanel>(
      host, parentFolder, folderName, suggestSubfolder, needsPassword);
  panel->Init();
  return MakeUiObjectRef<IExtractOptionsPanel>(panel);
}

CExtractOptionsPanel::CExtractOptionsPanel(IWindowHost *host, const FString &parentFolder,
    const UString &folderName, bool suggestSubfolder, bool needsPassword):
    _host(host)
{
  _model = make_self<CExtractOptionsModel>(parentFolder, folderName, suggestSubfolder, needsPassword);
}

void CExtractOptionsPanel::Init()
{
  _root = NXaml::Load<FrameworkElement>(IDX_EXTRACT_OPTIONS_PANEL);
  _root.DataContext(*_model);
  _errorBar = NXaml::FindName<InfoBar>(_root, L"ErrorBar");
  // the panel is kept alive by its owner while the button exists
  NXaml::FindName<Button>(_root, L"BrowseButton").Click([this](const IInspectable &, const RoutedEventArgs &)
  {
    OnBrowse();
  });
}

fire_and_forget CExtractOptionsPanel::OnBrowse()
{
  auto strong = get_strong();
  const UString dest = _model->GetDestination();
  UString start = dest;
  if (!NFile::NFind::DoesDirExist(us2fs(dest)))
    start = GetDirectoryPart(dest);
  hstring folder;
  try
  {
    folder = co_await NPickers::PickFolderAsync(_host->GetWindowId(), start, UString(L"Extract to"));
  }
  catch (const hresult_error &)
  {
    co_return;
  }
  if (folder.empty())
    co_return;
  UString path = ToUString(folder);
  if (_model->GetCreateSubfolder())
  {
    if (!path.IsEmpty() && !IS_PATH_SEPAR(path.Back()))
      path.Add_PathSepar();
    path += _model->GetFolderName();
  }
  _model->SetDestination(path);
}

void CExtractOptionsPanel::ShowError(const UString &message)
{
  _errorBar.Message(ToHString(message));
  _errorBar.IsOpen(true);
}

FrameworkElement CExtractOptionsPanel::GetRoot()
{
  return _root;
}

bool CExtractOptionsPanel::ValidateAndShowError()
{
  const UString error = _model->Validate();
  if (error.IsEmpty())
    return true;
  ShowError(error);
  return false;
}

void CExtractOptionsPanel::GetOptions(const UString &basePath, CExtractOptions &options)
{
  _model->ToOptions(basePath, options);
}

bool CExtractOptionsPanel::GetCreateSubfolder()
{
  return _model->GetCreateSubfolder();
}

bool CExtractOptionsPanel::GetOpenFolderWhenDone()
{
  return _model->GetOpenFolderWhenDone();
}

void CExtractOptionsPanel::SaveDefaults()
{
  CAppSettings &s = g_App.Settings.Current;
  s.OverwriteMode = (NOverwriteMode::EEnum)_model->GetOverwriteIndex();
  s.OpenFolderAfterExtract = _model->GetOpenFolderWhenDone();
  s.ZonePropagation = (NZonePropagation::EEnum)_model->GetZoneIndex();
  g_App.Settings.Save();
}
