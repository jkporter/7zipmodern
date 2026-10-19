// CompressPage.h

#ifndef ZIP7_INC_COMPRESS_PAGE_H
#define ZIP7_INC_COMPRESS_PAGE_H

#include "../Bindable.h"

#include "CompressViewModel.h"
#include "WindowHost.h"

// The "Create archive" tab (C# CompressPage). Its markup binds to CCompressViewModel.

class CCompressPage:
  public NWindows11::CBindableT<CCompressPage>,
  public ITabPage
{
  IWindowHost *_host;
  winrt::com_ptr<CCompressViewModel> _vm;
  bool _closed;
  winrt::Microsoft::UI::Xaml::FrameworkElement _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::CheckBox _rememberCheck { nullptr };
  winrt::event_token _vmChangedToken;

  void AddSource(const UString &path);
  void OnDragOver(const winrt::Microsoft::UI::Xaml::DragEventArgs &e);
  winrt::fire_and_forget OnDrop(winrt::Microsoft::UI::Xaml::DragEventArgs e);
  winrt::fire_and_forget OnAddFilesClick();
  winrt::fire_and_forget OnAddFolderClick();
  winrt::fire_and_forget OnBrowseClick();
  winrt::fire_and_forget OnCreateClick();

public:
  Z7_IFACE_IMP(ITabPage)

  CCompressPage(IWindowHost *host, const UStringVector &paths, const UString &targetArchive);
  void Init();
};

#endif
