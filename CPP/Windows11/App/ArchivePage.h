// ArchivePage.h

#ifndef ZIP7_INC_ARCHIVE_PAGE_H
#define ZIP7_INC_ARCHIVE_PAGE_H

#include "../Bindable.h"

#include "ArchiveViewModel.h"
#include "WindowHost.h"

/*
Shared, responsive column widths of the archive list header and rows (C# ColumnLayout).
Bindable: ModifiedWidth, TypeWidth, SizeWidth, PackedWidth (GridLength).
The page binds the ColumnDefinitions to it from code, because runtime-loaded
markup can't reference app types as resources. It is a separate object (not
the page) so that the bindings don't keep the page alive.
*/

class CColumnLayout: public NWindows11::CBindableT<CColumnLayout>
{
  double _widths[4];
public:
  CColumnLayout();
  void Fit(double width);
  // columns 1..4 of (grid) follow the layout
  void Bind(const winrt::Microsoft::UI::Xaml::Controls::Grid &grid);
};


// An open archive tab (C# ArchivePage). DataContext of the markup = CArchiveViewModel.

class CArchivePage:
  public NWindows11::CBindableT<CArchivePage>,
  public ITabPage
{
  IWindowHost *_host;
  winrt::com_ptr<CArchiveViewModel> _vm;
  winrt::com_ptr<CColumnLayout> _columns;
  bool _closed;
  winrt::event_token _vmChangedToken;

  winrt::Microsoft::UI::Xaml::FrameworkElement _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::CommandBar _toolbar { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::AutoSuggestBox _searchBox { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::BreadcrumbBar _breadcrumbs { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ListView _itemsList { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer _detailsPane { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition _detailsColumn { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::AppBarToggleButton _detailsToggle { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::AppBarButton _renameButton { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::AppBarButton _deleteButton { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _extractSmartItem { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _menuOpen { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _menuOpenWith { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _menuOpenInside { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _menuRename { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem _menuDelete { nullptr };

  void OnClick(const wchar_t *name, const std::function<void()> &action);
  void OnMenuClick(const wchar_t *name, const std::function<void()> &action);
  void AddAccelerator(winrt::Windows::System::VirtualKey key, winrt::Windows::System::VirtualKeyModifiers modifiers,
      const std::function<void(const winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs &)> &action);

  CItemVector GetSelection() const;
  bool IsDetailsToggleChecked() const;
  bool IsSearchBoxFocused() const;
  void OnViewModelChanged(const winrt::hstring &propertyName);

  // layout
  void OnSizeChanged(double width);
  void ApplyDetailsVisibility();
  void OnDetailsToggleClick();
  void MovePaneFocus(int step);

  // list
  void OnSelectionChanged();
  void OnItemsDoubleTapped(const winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs &e);
  void OnItemsKeyDown(const winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs &e);
  void OnItemRightTapped(const winrt::Microsoft::UI::Xaml::Input::RightTappedRoutedEventArgs &e);
  void OnItemMenuOpening();
  void OnContainerContentChanging(const winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs &args);

  // commands
  winrt::fire_and_forget ExtractWithDialogAsync(CItemVector selection);
  winrt::fire_and_forget AddFilesAsync(bool folders);
  winrt::fire_and_forget ShareAsync();
  winrt::fire_and_forget CopyToClipboardAsync();
  winrt::fire_and_forget PasteAsync(winrt::Windows::ApplicationModel::DataTransfer::DataPackageView content);
  void OnMenuProperties();
  void OnCopyPath();

  // drag & drop
  void OnDragItemsStarting(const winrt::Microsoft::UI::Xaml::Controls::DragItemsStartingEventArgs &e);
  winrt::fire_and_forget ProvideDragDataAsync(CItemVector items,
      winrt::Windows::ApplicationModel::DataTransfer::DataProviderRequest request,
      winrt::Windows::ApplicationModel::DataTransfer::DataProviderDeferral deferral);
  void OnDragOver(const winrt::Microsoft::UI::Xaml::DragEventArgs &e);
  winrt::fire_and_forget OnDropAsync(winrt::Microsoft::UI::Xaml::DragEventArgs e);

public:
  Z7_IFACE_IMP(ITabPage)

  CArchivePage(IWindowHost *host, const winrt::com_ptr<CArchiveViewModel> &vm);
  void Init();
};

#endif
