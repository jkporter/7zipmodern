// Windows11/Application.h

#ifndef ZIP7_INC_WINDOWS11_APPLICATION_H
#define ZIP7_INC_WINDOWS11_APPLICATION_H

#include "WinUI.h"

// WindowsAppSDK-VersionInfo.h declares a global namespace Microsoft, which
// is ambiguous with winrt::Microsoft after "using namespace winrt;".
// So include this header only in the file that has WinMain.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace NWindows11 {

/*
Base class for a WinUI Application without the XAML compiler.
XamlReader resolves control types through the application's
IXamlMetadataProvider; this one forwards to the provider of the WinUI
controls library. OnLaunched() in the derived class should call
AddControlsResources() before it loads any markup.
*/

template <class D>
class CApplicationT: public winrt::Microsoft::UI::Xaml::ApplicationT<D,
    winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
{
  winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider _provider;
protected:
  void AddControlsResources()
  {
    this->Resources().MergedDictionaries().Append(winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources());
  }
public:
  winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(const winrt::Windows::UI::Xaml::Interop::TypeName &type)
  {
    return _provider.GetXamlType(type);
  }
  winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(const winrt::hstring &fullName)
  {
    return _provider.GetXamlType(fullName);
  }
  winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions()
  {
    return _provider.GetXmlnsDefinitions();
  }
};

/*
Makes the Windows App SDK runtime (framework package) available to an
unpackaged process. Does nothing when the process has package identity.
*/

class CWindowsAppRuntime
{
  bool _initialized;
public:
  CWindowsAppRuntime(): _initialized(false) {}
  ~CWindowsAppRuntime()
  {
    if (_initialized)
      MddBootstrapShutdown();
  }
  HRESULT Initialize()
  {
    PACKAGE_VERSION minVersion;
    minVersion.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
    const HRESULT hres = MddBootstrapInitialize2(WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, minVersion,
        (MddBootstrapInitializeOptions)(
            MddBootstrapInitializeOptions_OnNoMatch_ShowUI |
            MddBootstrapInitializeOptions_OnPackageIdentity_NOOP));
    _initialized = SUCCEEDED(hres);
    return hres;
  }
};

}

#endif
