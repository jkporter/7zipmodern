// ThemeHelper.cpp

#include "StdAfx.h"

#include "ThemeHelper.h"

#include "App.h"

using namespace winrt;
using namespace winrt::Microsoft::UI;
using namespace winrt::Microsoft::UI::Xaml;

namespace NThemeHelper {

void Apply(const Window &window)
{
  const CAppSettings &s = g_App.Settings.Current;
  const FrameworkElement root = window.Content().try_as<FrameworkElement>();
  if (root)
    root.RequestedTheme(
        s.Theme == NAppTheme::kLight ? ElementTheme::Light :
        s.Theme == NAppTheme::kDark ? ElementTheme::Dark :
        ElementTheme::Default);

  if (s.Backdrop == NBackdropKind::kAcrylic)
    window.SystemBackdrop(Media::DesktopAcrylicBackdrop());
  else
  {
    Media::MicaBackdrop mica;
    mica.Kind(s.Backdrop == NBackdropKind::kMica ?
        Composition::SystemBackdrops::MicaKind::Base :
        Composition::SystemBackdrops::MicaKind::BaseAlt);
    window.SystemBackdrop(mica);
  }
  UpdateCaptionButtons(window);
}

static winrt::Windows::UI::Color Argb(Byte a, Byte r, Byte g, Byte b)
{
  winrt::Windows::UI::Color c;
  c.A = a; c.R = r; c.G = g; c.B = b;
  return c;
}

void UpdateCaptionButtons(const Window &window)
{
  const FrameworkElement root = window.Content().try_as<FrameworkElement>();
  if (!root || !Windowing::AppWindowTitleBar::IsCustomizationSupported())
    return;
  const Windowing::AppWindowTitleBar tb = window.AppWindow().TitleBar();
  const bool dark = (root.ActualTheme() == ElementTheme::Dark);
  const winrt::Windows::UI::Color transparent = Argb(0, 0, 0, 0);
  const winrt::Windows::UI::Color fore = dark ? Argb(0xFF, 0xFF, 0xFF, 0xFF) : Argb(0xFF, 0, 0, 0);
  tb.ButtonBackgroundColor(transparent);
  tb.ButtonInactiveBackgroundColor(transparent);
  tb.ButtonForegroundColor(fore);
  tb.ButtonHoverBackgroundColor(dark ? Argb(0x18, 0xFF, 0xFF, 0xFF) : Argb(0x10, 0, 0, 0));
  tb.ButtonHoverForegroundColor(fore);
  tb.ButtonPressedBackgroundColor(dark ? Argb(0x10, 0xFF, 0xFF, 0xFF) : Argb(0x08, 0, 0, 0));
  tb.ButtonInactiveForegroundColor(dark ? Argb(0x80, 0xFF, 0xFF, 0xFF) : Argb(0x80, 0, 0, 0));
}

}
