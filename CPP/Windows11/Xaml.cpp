// Windows11/Xaml.cpp

#include "StdAfx.h"

#include "../Common/UTFConvert.h"

#include "HString.h"
#include "Xaml.h"

using namespace winrt;

namespace NWindows11 {
namespace NXaml {

Windows::Foundation::IInspectable LoadResource(UINT resourceId, HMODULE module)
{
  if (!module)
    module = ::GetModuleHandleW(NULL);
  const HRSRC res = ::FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"XAML");
  if (!res)
    throw_last_error();
  const HGLOBAL global = ::LoadResource(module, res);
  if (!global)
    throw_last_error();
  const char *data = (const char *)::LockResource(global);
  const DWORD size = ::SizeofResource(module, res);
  if (!data || size == 0)
    throw hresult_error(E_FAIL, L"Empty XAML resource");

  AString utf8;
  utf8.SetFrom(data, (unsigned)size);
  UString markup;
  ConvertUTF8ToUnicode(utf8, markup);
  // skip UTF-8 BOM
  if (!markup.IsEmpty() && markup[0] == 0xFEFF)
    markup.DeleteFrontal(1);
  return Microsoft::UI::Xaml::Markup::XamlReader::Load(ToHString(markup));
}

}}
