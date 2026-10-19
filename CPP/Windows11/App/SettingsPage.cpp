// SettingsPage.cpp

#include "StdAfx.h"

#include "SettingsPage.h"

#include "../../7zip/MyVersion.h"

#include "../HString.h"
#include "../Xaml.h"

#include "../Core/ArchiveFormat.h"
#include "../Core/CompressionSettings.h"
#include "../Core/HashEngine.h"
#include "../Core/PropertyFormatter.h"
#include "../Core/SevenZipEngine.h"

#include "App.h"
#include "DialogService.h"
#include "PageFactory.h"
#include "PlatformServices.h"
#include "ShellServices.h"
#include "resource.h"

#ifdef _MSC_VER
// lambdas that capture a pointer to a CAppSettings member
#pragma warning(disable : 4371) // layout of class may have changed from a previous version of the compiler
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace NWindows11;

static const NCompressionLevel::EEnum k_Levels[] =
{
  NCompressionLevel::kStore,
  NCompressionLevel::kFastest,
  NCompressionLevel::kFast,
  NCompressionLevel::kNormal,
  NCompressionLevel::kMaximum,
  NCompressionLevel::kUltra
};

static int GetLevelIndex()
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_Levels); i++)
    if (k_Levels[i] == g_App.Settings.Current.DefaultLevel)
      return (int)i;
  return -1;
}

static void SetLevelIndex(int i)
{
  if (i < (int)Z7_ARRAY_SIZE(k_Levels))
    g_App.Settings.Current.DefaultLevel = k_Levels[i];
}


CTabPageRef CreateSettingsPage(IWindowHost *host)
{
  com_ptr<CSettingsPage> page = make_self<CSettingsPage>(host);
  page->Init();
  return MakeUiObjectRef<ITabPage>(page);
}


void CSettingsPage::AddBoolSetting(const wchar_t *name, bool CAppSettings::*field)
{
  AddProperty(name, [field] { return box_value(g_App.Settings.Current.*field); },
      [this, field](const CInspectable &v)
      {
        const bool b = unbox_value_or<bool>(v, g_App.Settings.Current.*field);
        if (b == g_App.Settings.Current.*field)
          return;
        g_App.Settings.Current.*field = b;
        Save();
      });
}

void CSettingsPage::AddIndexSetting(const wchar_t *name, int (*get)(), void (*set)(int), bool applyTheme)
{
  AddProperty(name, [get] { return box_value(get()); },
      [this, get, set, applyTheme](const CInspectable &v)
      {
        const int i = unbox_value_or<int>(v, -1);
        if (i < 0 || i == get())
          return;
        set(i);
        Save();
        if (applyTheme)
          g_App.ApplyThemeToAll();
      });
}

CSettingsPage::CSettingsPage(IWindowHost *host): _host(host), _loaded(false)
{
  AddIndexSetting(L"ThemeIndex",
      [] { return (int)g_App.Settings.Current.Theme; },
      [](int i) { if (i <= NAppTheme::kDark) g_App.Settings.Current.Theme = (NAppTheme::EEnum)i; }, true);
  AddIndexSetting(L"BackdropIndex",
      [] { return (int)g_App.Settings.Current.Backdrop; },
      [](int i) { if (i <= NBackdropKind::kAcrylic) g_App.Settings.Current.Backdrop = (NBackdropKind::EEnum)i; }, true);
  AddIndexSetting(L"OverwriteIndex",
      [] { return (int)g_App.Settings.Current.OverwriteMode; },
      [](int i) { if (i <= NOverwriteMode::kRenameExisting) g_App.Settings.Current.OverwriteMode = (NOverwriteMode::EEnum)i; }, false);
  AddIndexSetting(L"ZoneIndex",
      [] { return (int)g_App.Settings.Current.ZonePropagation; },
      [](int i) { if (i <= NZonePropagation::kOfficeFilesOnly) g_App.Settings.Current.ZonePropagation = (NZonePropagation::EEnum)i; }, false);
  AddIndexSetting(L"LevelIndex", GetLevelIndex, SetLevelIndex, false);

  AddBoolSetting(L"SmartExtractFolder", &CAppSettings::SmartExtractFolder);
  AddBoolSetting(L"OpenFolderAfterExtract", &CAppSettings::OpenFolderAfterExtract);
  AddBoolSetting(L"AutoOpenTarballs", &CAppSettings::AutoOpenTarballs);
  AddBoolSetting(L"RestoreSymbolicLinks", &CAppSettings::RestoreSymbolicLinks);
  AddBoolSetting(L"DefaultEncryptFileNames", &CAppSettings::DefaultEncryptFileNames);
  AddBoolSetting(L"EfficiencyModeForBackground", &CAppSettings::EfficiencyModeForBackground);
  AddBoolSetting(L"MenuOpen", &CAppSettings::MenuOpen);
  AddBoolSetting(L"MenuExtract", &CAppSettings::MenuExtract);
  AddBoolSetting(L"MenuExtractHere", &CAppSettings::MenuExtractHere);
  AddBoolSetting(L"MenuExtractSmart", &CAppSettings::MenuExtractSmart);
  AddBoolSetting(L"MenuTest", &CAppSettings::MenuTest);
  AddBoolSetting(L"MenuCompress", &CAppSettings::MenuCompress);
  AddBoolSetting(L"MenuCompress7z", &CAppSettings::MenuCompress7z);
  AddBoolSetting(L"MenuCompressZip", &CAppSettings::MenuCompressZip);
  AddBoolSetting(L"MenuCompressShare", &CAppSettings::MenuCompressShare);
  AddBoolSetting(L"MenuHash", &CAppSettings::MenuHash);
  AddBoolSetting(L"NotifyOnCompletion", &CAppSettings::NotifyOnCompletion);
  AddBoolSetting(L"OfferSavedPasswords", &CAppSettings::OfferSavedPasswords);
  AddBoolSetting(L"ConfirmDelete", &CAppSettings::ConfirmDelete);

  AddProperty(L"AboutDescription", []
  {
    UString s = NPropertyFormatter::FormatCount((Int64)NFormatRegistry::All().Size(), L"archive format");
    s += L" \x00B7 ";
    s += NPropertyFormatter::FormatCount((Int64)NHashEngine::GetAlgorithms().Size(), L"checksum algorithm");
    return BoxString(s);
  });
}

void CSettingsPage::Init()
{
  _root = NXaml::Load<UserControl>(IDX_SETTINGS_PAGE);
  _formatCombo = NXaml::FindName<ComboBox>(_root, L"FormatCombo");
  _tempSizeText = NXaml::FindName<TextBlock>(_root, L"TempSizeText");

  const unsigned numFormats = NFormatCapabilities::GetCount();
  const UString &current = g_App.Settings.Current.DefaultFormat;
  int selected = -1;
  for (unsigned i = 0; i < numFormats; i++)
  {
    const CFormatCapabilities &f = NFormatCapabilities::GetByIndex(i);
    _formatCombo.Items().Append(box_value(f.DisplayName));
    if (current.IsEqualTo_NoCase(f.Id))
      selected = (int)i;
  }
  _formatCombo.SelectedIndex(selected);
  _formatCombo.SelectionChanged([this](auto &&, auto &&)
  {
    const int i = _formatCombo.SelectedIndex();
    if (i < 0 || (unsigned)i >= NFormatCapabilities::GetCount())
      return;
    g_App.Settings.Current.DefaultFormat = NFormatCapabilities::GetByIndex((unsigned)i).Id;
    Save();
  });

  // App 26.4.0 · Engine 26.04 · x64 · unpackaged
  UString version (L"App ");
  version.Add_UInt32(MY_VER_MAJOR);
  version.Add_Char('.');
  version.Add_UInt32(MY_VER_MINOR);
  version.Add_Char('.');
  version.Add_UInt32(MY_VER_BUILD);
  version += L" \x00B7 Engine ";
  UInt32 major, minor;
  if (NSevenZipEngine::GetEngineVersion(major, minor))
  {
    version.Add_UInt32(major);
    version.Add_Char('.');
    if (minor < 10)
      version.Add_Char('0');
    version.Add_UInt32(minor);
  }
  else
    version += "?";
  #if defined(_M_ARM64)
  version += L" \x00B7 Arm64";
  #else
  version += L" \x00B7 X64";
  #endif
  version += NShellActions::IsPackaged() ? L" \x00B7 MSIX" : L" \x00B7 unpackaged";
  NXaml::FindName<TextBlock>(_root, L"VersionText").Text(ToHString(version));

  NXaml::FindName<Button>(_root, L"DefaultAppsButton").Click([this](auto &&, auto &&) { OnDefaultApps(); });
  NXaml::FindName<Button>(_root, L"ForgetPasswordsButton").Click([this](auto &&, auto &&) { OnForgetPasswords(); });
  NXaml::FindName<Button>(_root, L"CleanTempButton").Click([this](auto &&, auto &&)
  {
    NTempFiles::CleanUp(true);
    UpdateTempSize();
  });
  UpdateTempSize();

  _root.DataContext(*this);
  _root.Loaded([this](auto &&, auto &&) { _loaded = true; });
}

void CSettingsPage::Save()
{
  if (_loaded)
    g_App.Settings.Save();
}

void CSettingsPage::UpdateTempSize()
{
  _tempSizeText.Text(ToHString(NPropertyFormatter::FormatSize(NTempFiles::MeasureAll())));
}

fire_and_forget CSettingsPage::OnDefaultApps()
{
  com_ptr<CSettingsPage> strong = get_strong();
  hstring uri = L"ms-settings:defaultapps";
  if (NShellActions::IsPackaged())
  {
    try
    {
      const hstring aumid = winrt::Windows::ApplicationModel::Package::Current().Id().FamilyName() + L"!App";
      uri = uri + L"?registeredAUMID=" + winrt::Windows::Foundation::Uri::EscapeComponent(aumid);
    }
    catch (...) {}
  }
  try
  {
    co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(uri));
  }
  catch (...) {}
}

fire_and_forget CSettingsPage::OnForgetPasswords()
{
  com_ptr<CSettingsPage> strong = get_strong();
  const unsigned n = NPasswordStore::ForgetAll();
  UString message;
  if (n == 0)
    message = L"There were no saved passwords.";
  else
  {
    message = L"Removed ";
    message += NPropertyFormatter::FormatCount((Int64)n, L"saved password");
    message.Add_Char('.');
  }
  co_await _host->Dialogs().ShowErrorAsync(UString(L"Saved passwords"), message);
}


// ITabPage

NPageKind::EEnum CSettingsPage::GetPageKind() { return NPageKind::kSettings; }
FrameworkElement CSettingsPage::GetRoot() { return _root; }
UString CSettingsPage::GetTabTitle() { return UString(L"Settings"); }

IconSource CSettingsPage::GetTabIcon()
{
  FontIconSource icon;
  icon.Glyph(L"\xE713");
  return icon;
}

UString CSettingsPage::GetArchivePath() { return UString(); }

void CSettingsPage::OnTabClosed()
{
  g_App.Settings.Save();
}
