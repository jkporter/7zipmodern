// ThemeHelper.h

#ifndef ZIP7_INC_THEME_HELPER_H
#define ZIP7_INC_THEME_HELPER_H

// Theme + system backdrop (Mica / Mica Alt for tabbed windows / Acrylic) per Windows 11 guidance.

namespace NThemeHelper
{
  // applies g_App.Settings (Theme, Backdrop) to the window and its content
  void Apply(const winrt::Microsoft::UI::Xaml::Window &window);
  // caption button colours must follow the in-app theme when the title bar is custom
  void UpdateCaptionButtons(const winrt::Microsoft::UI::Xaml::Window &window);
}

#endif
