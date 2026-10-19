// Windows11/HString.h

#ifndef ZIP7_INC_WINDOWS11_HSTRING_H
#define ZIP7_INC_WINDOWS11_HSTRING_H

#include "../Common/MyString.h"

#include "WinUI.h"

// Conversions between 7-Zip strings and C++/WinRT strings at the WinRT boundary.

namespace NWindows11 {

inline winrt::hstring ToHString(const UString &s) { return winrt::hstring(s.Ptr(), (winrt::hstring::size_type)s.Len()); }
inline winrt::hstring ToHString(const wchar_t *s) { return winrt::hstring(s); }

inline UString ToUString(const winrt::hstring &s)
{
  UString u;
  u.SetFrom(s.c_str(), (unsigned)s.size());
  return u;
}

inline winrt::Windows::Foundation::IInspectable BoxString(const UString &s) { return winrt::box_value(ToHString(s)); }

inline UString UnboxString(const winrt::Windows::Foundation::IInspectable &v)
{
  return ToUString(winrt::unbox_value_or<winrt::hstring>(v, winrt::hstring()));
}

}

#endif
