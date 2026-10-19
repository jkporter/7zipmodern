// WindowHost.h

#ifndef ZIP7_INC_WINDOW_HOST_H
#define ZIP7_INC_WINDOW_HOST_H

#include "../../Common/MyString.h"

#include "../../7zip/IDecl.h"

class CDialogService;
class COperationService;
class CArchiveViewModel;

namespace NPageKind
{
  enum EEnum
  {
    kHome,
    kArchive,
    kCompress,
    kHash,
    kBenchmark,
    kSettings
  };
}

Z7_PURE_INTERFACES_BEGIN

/*
Everything that lives in a tab of the main window.
The object that implements ITabPage is a WinRT object (normally
NWindows11::CBindableT<CFooPage>); the window keeps it alive while the tab
exists (see CUiObjectRef below). All calls are made on the UI thread.
  GetRoot()         the page's root element (loaded with NXaml::Load), the tab content
  GetTabTitle()     tab header and tooltip
  GetTabIcon()      tab icon (FontIconSource / ImageIconSource)
  GetArchivePath()  file of a top-level archive shown by the page, or empty;
                    the window uses it to select an existing tab instead of opening a file twice
  OnTabClosed()     the tab is being closed (also when the window closes);
                    after it the window clears GetRoot().DataContext()
*/

#define Z7_IFACEN_ITabPage(x) \
  virtual NPageKind::EEnum GetPageKind() x \
  virtual winrt::Microsoft::UI::Xaml::FrameworkElement GetRoot() x \
  virtual UString GetTabTitle() x \
  virtual winrt::Microsoft::UI::Xaml::Controls::IconSource GetTabIcon() x \
  virtual UString GetArchivePath() x \
  virtual void OnTabClosed() x \

Z7_IFACE_DECL_PURE(ITabPage)

/*
What pages need from the window that hosts them (MainWindow; OperationWindow
implements it too and hands interactive requests to the main instance).
Empty strings mean "not specified" (C# null).
*/

#define Z7_IFACEN_IWindowHost(x) \
  virtual HWND GetHwnd() x \
  virtual winrt::Microsoft::UI::WindowId GetWindowId() x \
  virtual CDialogService &Dialogs() x \
  virtual COperationService &Operations() x \
  virtual void OpenArchive(const UString &path, const UString &format) x \
  virtual void OpenNestedArchive(CArchiveViewModel *parent, const UString &extractedPath, const UString &pathInParent) x \
  virtual void OpenCompress(const UStringVector &paths, const UString &targetArchive) x \
  virtual void OpenHash(const UStringVector &paths, const UString &algorithm) x \
  virtual void OpenBenchmark() x \
  virtual void OpenSettings() x \
  virtual void UpdateTab(ITabPage *page, const UString &title) x \
  virtual void CloseTab(ITabPage *page) x \

Z7_IFACE_DECL_PURE(IWindowHost)

Z7_PURE_INTERFACES_END


/*
A strong reference to a WinRT object plus a C++ interface that the object
implements (there is no RTTI / QueryInterface for C++ interfaces).
  winrt::com_ptr<CHomePage> page = winrt::make_self<CHomePage>(host);
  CTabPageRef ref = MakeUiObjectRef<ITabPage>(page);
*/

template <class I>
struct CUiObjectRef
{
  winrt::Windows::Foundation::IInspectable Object;
  I *Interface;

  CUiObjectRef(): Object(nullptr), Interface(NULL) {}
  bool IsEmpty() const { return Interface == NULL; }
  I *operator->() const { return Interface; }
};

template <class I, class T>
CUiObjectRef<I> MakeUiObjectRef(const winrt::com_ptr<T> &object)
{
  CUiObjectRef<I> r;
  r.Object = object.template as<winrt::Windows::Foundation::IInspectable>();
  r.Interface = static_cast<I *>(object.get());
  return r;
}

typedef CUiObjectRef<ITabPage> CTabPageRef;


/*
Windows App SDK pickers (they work elevated and unpackaged, and return plain paths).
Call them on the UI thread. An empty result means the user cancelled.
*/

namespace NPickers {

typedef winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> >
    CPathsOperation;

// filter: extensions like L".txt" (empty vector = all files); title: "" = default
CPathsOperation PickFilesAsync(winrt::Microsoft::UI::WindowId window, UString title = UString(),
    UStringVector filter = UStringVector());
winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PickArchiveAsync(winrt::Microsoft::UI::WindowId window);
CPathsOperation PickFoldersAsync(winrt::Microsoft::UI::WindowId window, UString title = UString());
winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PickFolderAsync(winrt::Microsoft::UI::WindowId window,
    UString start = UString(), UString title = UString());
// extension without the dot: L"7z"
winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PickSaveAsync(winrt::Microsoft::UI::WindowId window,
    UString suggestedPath, UString typeLabel, UString extension);

void ToUStringVector(const winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> &paths, UStringVector &result);

}

#endif
