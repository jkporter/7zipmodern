// Windows11/Control/Converters.cpp

#include "StdAfx.h"

#include "Converters.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::Foundation;

namespace NWindows11 {
namespace NControl {

namespace NConverterKind
{
  enum EEnum
  {
    kVisibleIf,
    kCollapsedIf,
    kVisibleIfNotEmpty,
    kVisibleIfEmpty,
    kNot
  };
}

static bool IsEmptyValue(const IInspectable &value)
{
  if (!value)
    return true;
  if (const auto s = value.try_as<IReference<hstring> >())
    return s.Value().empty();
  const auto v = value.try_as<IPropertyValue>();
  if (!v)
    return false;
  const PropertyType type = v.Type();
  if (type == PropertyType::Empty) return true;
  if (type == PropertyType::String) return v.GetString().empty();
  if (type == PropertyType::Boolean) return !v.GetBoolean();
  if (type == PropertyType::Int32) return v.GetInt32() == 0;
  if (type == PropertyType::UInt32) return v.GetUInt32() == 0;
  if (type == PropertyType::Int64) return v.GetInt64() == 0;
  if (type == PropertyType::UInt64) return v.GetUInt64() == 0;
  if (type == PropertyType::Double) return v.GetDouble() == 0;
  return false;
}

class CConverter: public implements<CConverter, Data::IValueConverter>
{
  NConverterKind::EEnum _kind;
public:
  CConverter(NConverterKind::EEnum kind): _kind(kind) {}

  IInspectable Convert(const IInspectable &value, const winrt::Windows::UI::Xaml::Interop::TypeName &, const IInspectable &, const hstring &)
  {
    if (_kind == NConverterKind::kNot)
      return box_value(!unbox_value_or<bool>(value, false));
    bool b;
    if (_kind == NConverterKind::kVisibleIf)
      b = unbox_value_or<bool>(value, false);
    else if (_kind == NConverterKind::kCollapsedIf)
      b = !unbox_value_or<bool>(value, false);
    else if (_kind == NConverterKind::kVisibleIfNotEmpty)
      b = !IsEmptyValue(value);
    else
      b = IsEmptyValue(value);
    return box_value(b ? Visibility::Visible : Visibility::Collapsed);
  }

  IInspectable ConvertBack(const IInspectable &value, const winrt::Windows::UI::Xaml::Interop::TypeName &, const IInspectable &, const hstring &)
  {
    if (_kind == NConverterKind::kNot)
      return box_value(!unbox_value_or<bool>(value, false));
    throw hresult_not_implemented();
  }
};

void AddConverters(const ResourceDictionary &resources)
{
  resources.Insert(box_value(L"VisibleIf"), make<CConverter>(NConverterKind::kVisibleIf));
  resources.Insert(box_value(L"CollapsedIf"), make<CConverter>(NConverterKind::kCollapsedIf));
  resources.Insert(box_value(L"VisibleIfNotEmpty"), make<CConverter>(NConverterKind::kVisibleIfNotEmpty));
  resources.Insert(box_value(L"VisibleIfEmpty"), make<CConverter>(NConverterKind::kVisibleIfEmpty));
  resources.Insert(box_value(L"Not"), make<CConverter>(NConverterKind::kNot));
}

}}
