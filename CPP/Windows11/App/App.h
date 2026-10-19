// App.h

#ifndef ZIP7_INC_APP_H
#define ZIP7_INC_APP_H

#include "LaunchRequest.h"
#include "SettingsService.h"

class CMainWindow;

/*
The application object (C# App): settings, the UI thread's dispatcher, the
window list and activation handling. A page reaches it through g_App:
  g_App.Settings.Current.AutoOpenTarballs
  g_App.Settings.Save();
  g_App.Dispatcher            (DispatcherQueue of the UI thread; the app has one UI thread)
The XAML Application class itself is private to App.cpp.
*/

class CApp
{
  struct CWindowItem
  {
    winrt::Microsoft::UI::Xaml::Window Window;
    winrt::event_token ClosedToken;
  };
  std::vector<CWindowItem> _windows;
  bool _notificationsRegistered;

public:
  CSettingsService Settings;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue Dispatcher { nullptr };
  CMainWindow *MainWindow;

  CApp(): _notificationsRegistered(false), MainWindow(NULL) {}

  // called by the XAML Application (UI thread)
  void OnLaunched(const CLaunchRequest &initial);

  // Entry point for every activation, including ones redirected from later launches.
  void Handle(const CLaunchRequest &request);

  // applies theme and backdrop and calls Exit() when the last tracked window closes
  void TrackWindow(const winrt::Microsoft::UI::Xaml::Window &window);
  unsigned GetNumWindows() const { return (unsigned)_windows.size(); }

  // re-applies theme / backdrop to every window after a settings change
  void ApplyThemeToAll();

  void Exit();
};

extern CApp g_App;

#endif
