// SettingsPage.h

#ifndef ZIP7_INC_SETTINGS_PAGE_H
#define ZIP7_INC_SETTINGS_PAGE_H

#include "../Bindable.h"

#include "SettingsService.h"
#include "WindowHost.h"

/*
The Settings tab. The page is the DataContext: every setting is a TwoWay
{Binding} property that writes g_App.Settings.Current and saves.
*/

class CSettingsPage:
  public NWindows11::CBindableT<CSettingsPage>,
  public ITabPage
{
  IWindowHost *_host;
  bool _loaded;
  winrt::Microsoft::UI::Xaml::Controls::UserControl _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ComboBox _formatCombo { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _tempSizeText { nullptr };

  void Save();
  void AddBoolSetting(const wchar_t *name, bool CAppSettings::*field);
  void AddIndexSetting(const wchar_t *name, int (*get)(), void (*set)(int), bool applyTheme);
  void UpdateTempSize();
  winrt::fire_and_forget OnDefaultApps();
  winrt::fire_and_forget OnForgetPasswords();

public:
  Z7_IFACE_IMP(ITabPage)

  CSettingsPage(IWindowHost *host);
  void Init();
};

#endif
