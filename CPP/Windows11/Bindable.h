// Windows11/Bindable.h

#ifndef ZIP7_INC_WINDOWS11_BINDABLE_H
#define ZIP7_INC_WINDOWS11_BINDABLE_H

#include "WinUI.h"

/*
XAML loaded at runtime (XamlReader) can't use x:Bind, so view models are
exposed to {Binding} through ICustomPropertyProvider: each bindable property
is registered by name with a getter (and an optional setter for TwoWay
bindings), and RaisePropertyChanged() notifies the bound controls.
Getters, setters and RaisePropertyChanged() are called on the UI thread.
*/

namespace NWindows11 {

typedef winrt::Windows::Foundation::IInspectable CInspectable;
typedef std::function<CInspectable()> CPropertyGetter;
typedef std::function<void(const CInspectable &value)> CPropertySetter;

winrt::Microsoft::UI::Xaml::Data::ICustomProperty CreateCustomProperty(
    const wchar_t *name, const CPropertyGetter &getter, const CPropertySetter &setter);

template <class D, class... I>
class CBindableT: public winrt::implements<D,
    winrt::Microsoft::UI::Xaml::Data::ICustomPropertyProvider,
    winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged,
    I...>
{
  struct CProp
  {
    winrt::hstring Name;
    winrt::Microsoft::UI::Xaml::Data::ICustomProperty Property;
  };
  std::vector<CProp> _props;
  winrt::event<winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> _propertyChanged;

protected:
  void AddProperty(const wchar_t *name, const CPropertyGetter &getter, const CPropertySetter &setter = nullptr)
  {
    CProp p;
    p.Name = name;
    p.Property = CreateCustomProperty(name, getter, setter);
    _props.push_back(p);
  }

public:
  void RaisePropertyChanged(const wchar_t *name)
  {
    _propertyChanged(*this, winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventArgs(name));
  }

  // ICustomPropertyProvider
  winrt::Microsoft::UI::Xaml::Data::ICustomProperty GetCustomProperty(const winrt::hstring &name)
  {
    for (const CProp &p: _props)
      if (p.Name == name)
        return p.Property;
    return nullptr;
  }
  winrt::Microsoft::UI::Xaml::Data::ICustomProperty GetIndexedProperty(
      const winrt::hstring &, const winrt::Windows::UI::Xaml::Interop::TypeName &)
  {
    return nullptr;
  }
  winrt::hstring GetStringRepresentation() { return winrt::hstring(); }
  winrt::Windows::UI::Xaml::Interop::TypeName Type()
  {
    winrt::Windows::UI::Xaml::Interop::TypeName t;
    t.Name = L"Object";
    t.Kind = winrt::Windows::UI::Xaml::Interop::TypeKind::Primitive;
    return t;
  }

  // INotifyPropertyChanged
  winrt::event_token PropertyChanged(const winrt::Microsoft::UI::Xaml::Data::PropertyChangedEventHandler &handler)
  {
    return _propertyChanged.add(handler);
  }
  void PropertyChanged(const winrt::event_token &token) noexcept
  {
    _propertyChanged.remove(token);
  }
};

}

#endif
