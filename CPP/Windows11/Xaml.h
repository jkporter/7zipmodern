// Windows11/Xaml.h

#ifndef ZIP7_INC_WINDOWS11_XAML_H
#define ZIP7_INC_WINDOWS11_XAML_H

#include "WinUI.h"

/*
The markup of each window, page and dialog is a .xaml file that resource.rc
compiles into the module as a resource of type XAML (UTF-8):
  IDX_ARCHIVE_PAGE  XAML  "ArchivePage.xaml"
Load() parses it with XamlReader at runtime, much like a dialog template, and
FindName() gets controls by x:Name, as CDialog::GetItem() does by control ID.
Such markup can't use x:Class, x:Bind, event attributes or app-defined types.
*/

namespace NWindows11 {
namespace NXaml {

winrt::Windows::Foundation::IInspectable LoadResource(UINT resourceId, HMODULE module = NULL);

template <class T>
T Load(UINT resourceId, HMODULE module = NULL)
{
  return LoadResource(resourceId, module).as<T>();
}

template <class T>
T FindName(const winrt::Microsoft::UI::Xaml::FrameworkElement &root, const wchar_t *name)
{
  return root.FindName(name).as<T>();
}

}}

#endif
