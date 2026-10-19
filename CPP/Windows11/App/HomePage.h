// HomePage.h

#ifndef ZIP7_INC_HOME_PAGE_H
#define ZIP7_INC_HOME_PAGE_H

#include "../Bindable.h"

#include "WindowHost.h"

/*
The Home tab: the four action tiles, a drop zone and the recent archives.
This is the reference page for the page contract (see PAGE_CONTRACT.md).
*/

class CHomePage:
  public NWindows11::CBindableT<CHomePage>,
  public ITabPage
{
  IWindowHost *_host;
  bool _closed;
  winrt::Microsoft::UI::Xaml::Controls::UserControl _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ListView _recentList { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _emptyRecentText { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::HyperlinkButton _clearRecentButton { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Border _dropZone { nullptr };

  void LoadRecent();
  void ResetDropZone();
  winrt::fire_and_forget OnOpen();
  winrt::fire_and_forget OnCompress();
  winrt::fire_and_forget OnDrop(winrt::Microsoft::UI::Xaml::DragEventArgs e);

public:
  Z7_IFACE_IMP(ITabPage)

  CHomePage(IWindowHost *host): _host(host), _closed(false) {}
  void Init();

  // called by the recent items' commands
  void RevealRecent(const UString &path);
  void RemoveRecent(const UString &path);
};

#endif
