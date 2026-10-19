// MainWindow.h

#ifndef ZIP7_INC_MAIN_WINDOW_H
#define ZIP7_INC_MAIN_WINDOW_H

#include "DialogService.h"
#include "LaunchRequest.h"
#include "OperationService.h"
#include "WindowHost.h"

/*
The main window: tabs in the title bar (like File Explorer and Terminal on
Windows 11) on Mica Alt. Each tab is an archive, a "Create archive" job,
checksums, benchmark, settings, or Home. Created by g_App on the UI thread.
*/

class CMainWindow Z7_final: public IWindowHost
{
  struct CTab
  {
    UInt32 Id;
    winrt::Microsoft::UI::Xaml::Controls::TabViewItem Item;
    CTabPageRef Page;
  };

  winrt::Microsoft::UI::Xaml::Window _window;
  winrt::Microsoft::UI::Xaml::Controls::Grid _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TabView _tabView { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition _captionSpacer { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ProgressRing _activityRing { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _noActivityText { nullptr };
  HWND _hwnd;
  CDialogService _dialogs;
  COperationService _operations;
  std::vector<CTab> _tabs;
  UInt32 _nextTabId;
  bool _closeConfirmed;

  void CenterOnScreen();
  void UpdateCaptionSpacer();
  void UpdateActivity();
  void AddAccelerator(winrt::Windows::System::VirtualKey key, winrt::Windows::System::VirtualKeyModifiers modifiers,
      const std::function<void()> &action);
  void SetWindowTitle(const UString &tabTitle);

  int FindTab(const ITabPage *page) const;
  int FindTab(const winrt::Windows::Foundation::IInspectable &item) const;
  int GetSelectedTab() const;
  bool SelectExisting(NPageKind::EEnum kind);
  void AddTab(const CTabPageRef &page, bool replaceHome = false, bool insertAfterCurrent = false);
  void CloseTabAt(unsigned index);
  void CloseAllTabs();

  winrt::fire_and_forget OnClosing(winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs args);
  winrt::fire_and_forget HandleShare(winrt::Windows::ApplicationModel::DataTransfer::ShareTarget::ShareOperation share);
  winrt::fire_and_forget OnOpenAccelerator();
  winrt::fire_and_forget OnNewArchiveAccelerator();

  Z7_CLASS_NO_COPY(CMainWindow)
public:
  Z7_IFACE_IMP(IWindowHost)

  CMainWindow();
  winrt::Microsoft::UI::Xaml::Window GetWindow() const { return _window; }
  void Handle(const CLaunchRequest &request);
  void BringToFront();
};

#endif
