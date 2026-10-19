// HashPage.h

#ifndef ZIP7_INC_HASH_PAGE_H
#define ZIP7_INC_HASH_PAGE_H

#include "../Bindable.h"

#include "HashViewModel.h"
#include "WindowHost.h"

// The "Checksums" tab (C# HashPage). Its markup binds to CHashViewModel.

class CHashPage:
  public NWindows11::CBindableT<CHashPage>,
  public ITabPage
{
  IWindowHost *_host;
  winrt::com_ptr<CHashViewModel> _vm;
  bool _closed;
  winrt::Microsoft::UI::Xaml::FrameworkElement _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::InfoBar _verifyBar { nullptr };
  winrt::event_token _vmChangedToken;

  void UpdateVerifyBar();
  void OnDragOver(const winrt::Microsoft::UI::Xaml::DragEventArgs &e);
  winrt::fire_and_forget OnDrop(winrt::Microsoft::UI::Xaml::DragEventArgs e);
  winrt::fire_and_forget OnAddFilesClick();
  winrt::fire_and_forget OnComputeClick();
  winrt::fire_and_forget OnPasteQuery();
  winrt::fire_and_forget OnExportClick();

public:
  Z7_IFACE_IMP(ITabPage)

  CHashPage(IWindowHost *host, const UStringVector &paths, const UString &algorithm);
  void Init();
};

#endif
