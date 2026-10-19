// Windows11/Bindable.cpp

#include "StdAfx.h"

#include "Bindable.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Data;

namespace NWindows11 {

class CCustomProperty: public implements<CCustomProperty, ICustomProperty>
{
  hstring _name;
  CPropertyGetter _getter;
  CPropertySetter _setter;
public:
  CCustomProperty(const wchar_t *name, const CPropertyGetter &getter, const CPropertySetter &setter):
      _name(name), _getter(getter), _setter(setter)
      {}

  winrt::Windows::UI::Xaml::Interop::TypeName Type()
  {
    winrt::Windows::UI::Xaml::Interop::TypeName t;
    t.Name = L"Object";
    t.Kind = winrt::Windows::UI::Xaml::Interop::TypeKind::Primitive;
    return t;
  }
  hstring Name() { return _name; }
  bool CanRead() { return (bool)_getter; }
  bool CanWrite() { return (bool)_setter; }
  CInspectable GetValue(const CInspectable &) { return _getter ? _getter() : nullptr; }
  void SetValue(const CInspectable &, const CInspectable &value)
  {
    if (_setter)
      _setter(value);
  }
  CInspectable GetIndexedValue(const CInspectable &, const CInspectable &) { return nullptr; }
  void SetIndexedValue(const CInspectable &, const CInspectable &, const CInspectable &) {}
};

ICustomProperty CreateCustomProperty(const wchar_t *name, const CPropertyGetter &getter, const CPropertySetter &setter)
{
  return make<CCustomProperty>(name, getter, setter);
}

}
