// ItemViewModel.cpp

#include "StdAfx.h"

#include "ItemViewModel.h"

#include "../../Common/IntToString.h"

#include "../HString.h"

#include "../Core/PropertyFormatter.h"

#include "ShellServices.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Data;
using namespace NWindows11;

namespace NItemProp
{
  enum EEnum
  {
    kName,
    kPath,
    kLocation,
    kIsFolder,
    kIsEncrypted,
    kIcon,
    kTypeName,
    kSizeText,
    kPackedText,
    kModifiedText,
    kRatioText,
    kCrcText,
    kMethod,
    kAutomationName,
    kNumProps
  };
}

static const wchar_t * const k_ItemPropNames[NItemProp::kNumProps] =
{
  L"Name",
  L"Path",
  L"Location",
  L"IsFolder",
  L"IsEncrypted",
  L"Icon",
  L"TypeName",
  L"SizeText",
  L"PackedText",
  L"ModifiedText",
  L"RatioText",
  L"CrcText",
  L"Method",
  L"AutomationName"
};

static winrt::Windows::UI::Xaml::Interop::TypeName GetObjectTypeName()
{
  winrt::Windows::UI::Xaml::Interop::TypeName t;
  t.Name = L"Object";
  t.Kind = winrt::Windows::UI::Xaml::Interop::TypeKind::Primitive;
  return t;
}

// One property of the shared table: reads the value from the target row.
class CItemProperty: public implements<CItemProperty, ICustomProperty>
{
  unsigned _id;
public:
  CItemProperty(unsigned id): _id(id) {}

  winrt::Windows::UI::Xaml::Interop::TypeName Type() { return GetObjectTypeName(); }
  hstring Name() { return k_ItemPropNames[_id]; }
  bool CanRead() { return true; }
  bool CanWrite() { return false; }
  CInspectable GetValue(const CInspectable &target)
  {
    CItemViewModel *item = CItemViewModel::FromObject(target);
    return item ? item->GetPropertyValue(_id) : nullptr;
  }
  void SetValue(const CInspectable &, const CInspectable &) {}
  CInspectable GetIndexedValue(const CInspectable &, const CInspectable &) { return nullptr; }
  void SetIndexedValue(const CInspectable &, const CInspectable &, const CInspectable &) {}
};

// Created once on the UI thread and never released (it would outlive the XAML runtime otherwise).
static void *g_ItemProps[NItemProp::kNumProps];


UString FormatShortDateTime(const FILETIME &utc)
{
  SYSTEMTIME st, local;
  if (!::FileTimeToSystemTime(&utc, &st))
    return UString();
  if (!::SystemTimeToTzSpecificLocalTime(NULL, &st, &local))
    local = st;
  wchar_t date[80], time[80];
  if (::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, NULL, date, Z7_ARRAY_SIZE(date), NULL) == 0)
    date[0] = 0;
  if (::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, time, Z7_ARRAY_SIZE(time)) == 0)
    time[0] = 0;
  UString s (date);
  s.Add_Space_if_NotEmpty();
  s += time;
  return s;
}


CItemViewModel::CItemViewModel():
    _typeName_Defined(false),
    Entry(NULL),
    Folder(NULL),
    IsFolder(false),
    IsEncrypted(false),
    PackedSize_Defined(false),
    Modified_Defined(false),
    ChildCount(0),
    Size(0),
    PackedSize(0)
{
  Modified.dwLowDateTime = 0;
  Modified.dwHighDateTime = 0;
}

com_ptr<CItemViewModel> CItemViewModel::FromFolder(CArchiveSessionHolder *session,
    const CArchiveFolder &folder, bool showLocation)
{
  com_ptr<CItemViewModel> item = make_self<CItemViewModel>();
  item->_session.copy_from(session);
  item->Folder = &folder;
  item->Entry = folder.Entry;
  item->Name = folder.Name;
  item->Path = folder.Path;
  item->IsFolder = true;
  const CFolderMeasure m = folder.Measure();
  item->Size = m.Size;
  item->PackedSize = m.Packed;
  item->PackedSize_Defined = true;
  if (folder.Entry && folder.Entry->Modified_Defined)
  {
    item->Modified = folder.Entry->Modified;
    item->Modified_Defined = true;
  }
  else
    item->Modified_Defined = folder.GetLatestModified(item->Modified);
  item->ChildCount = folder.GetNumFolders() + folder.Files.Size();
  if (showLocation && folder.Parent)
    item->Location = folder.Parent->Path;
  return item;
}

com_ptr<CItemViewModel> CItemViewModel::FromEntry(CArchiveSessionHolder *session,
    const CArchiveEntry &entry, bool showLocation)
{
  com_ptr<CItemViewModel> item = make_self<CItemViewModel>();
  item->_session.copy_from(session);
  item->Entry = &entry;
  item->Name = entry.GetName();
  item->Path = entry.Path;
  item->Size = entry.Size;
  item->PackedSize_Defined = entry.PackedSize_Defined;
  item->PackedSize = entry.PackedSize;
  item->Modified_Defined = entry.Modified_Defined;
  item->Modified = entry.Modified;
  item->IsEncrypted = entry.IsEncrypted;
  if (showLocation)
  {
    const int slash = entry.Path.ReverseFind(L'\\');
    if (slash > 0)
      item->Location = entry.Path.Left((unsigned)slash);
  }
  return item;
}

CItemViewModel *CItemViewModel::FromObject(const CInspectable &obj)
{
  if (!obj || !obj.try_as<IArchiveItemMarker>())
    return NULL;
  return get_self<CItemViewModel>(obj.as<ICustomPropertyProvider>());
}

const UString &CItemViewModel::GetTypeName()
{
  if (!_typeName_Defined)
  {
    _typeName = NShellIcons::GetTypeName(Name, IsFolder);
    _typeName_Defined = true;
  }
  return _typeName;
}

winrt::Microsoft::UI::Xaml::Media::ImageSource CItemViewModel::GetIcon() const
{
  return IsFolder ? NShellIcons::GetFolderIcon() : NShellIcons::GetIcon(Name);
}

UString CItemViewModel::GetSizeText() const
{
  if (IsFolder)
    return NPropertyFormatter::FormatCount(ChildCount, L"item");
  return NPropertyFormatter::FormatSize(Size);
}

UString CItemViewModel::GetPackedText() const
{
  if (IsFolder || !PackedSize_Defined)
    return UString();
  return NPropertyFormatter::FormatSize(PackedSize);
}

UString CItemViewModel::GetModifiedText() const
{
  if (!Modified_Defined)
    return UString();
  return FormatShortDateTime(Modified);
}

UString CItemViewModel::GetRatioText() const
{
  if (IsFolder || !PackedSize_Defined || Size == 0)
    return UString();
  double ratio = 100.0 * (double)PackedSize / (double)Size;
  if (ratio > 999)
    ratio = 999;
  wchar_t s[32];
  ConvertUInt64ToString((UInt64)(ratio + 0.5), s);
  UString r (s);
  r.Add_Char('%');
  return r;
}

UString CItemViewModel::GetCrcText() const
{
  if (!Entry || !Entry->Crc_Defined)
    return UString();
  char s[16];
  ConvertUInt32ToHex8Digits(Entry->Crc, s);
  for (char *p = s; *p; p++)
    *p = MyCharUpper_Ascii(*p);
  return UString(s);
}

UString CItemViewModel::GetAutomationName()
{
  UString s (Name);
  if (IsFolder)
  {
    s += L", folder";
    return s;
  }
  s += L", ";
  s += GetTypeName();
  s += L", ";
  s += GetSizeText();
  return s;
}

CInspectable CItemViewModel::GetPropertyValue(unsigned id)
{
  switch (id)
  {
    case NItemProp::kName: return BoxString(Name);
    case NItemProp::kPath: return BoxString(Path);
    case NItemProp::kLocation: return BoxString(Location);
    case NItemProp::kIsFolder: return box_value(IsFolder);
    case NItemProp::kIsEncrypted: return box_value(IsEncrypted);
    case NItemProp::kIcon: return GetIcon();
    case NItemProp::kTypeName: return BoxString(GetTypeName());
    case NItemProp::kSizeText: return BoxString(GetSizeText());
    case NItemProp::kPackedText: return BoxString(GetPackedText());
    case NItemProp::kModifiedText: return BoxString(GetModifiedText());
    case NItemProp::kRatioText: return BoxString(GetRatioText());
    case NItemProp::kCrcText: return BoxString(GetCrcText());
    case NItemProp::kMethod: return BoxString(Entry && !IsFolder ? Entry->Method : UString());
    case NItemProp::kAutomationName: return BoxString(GetAutomationName());
    default: return nullptr;
  }
}

ICustomProperty CItemViewModel::GetCustomProperty(const hstring &name)
{
  for (unsigned i = 0; i < NItemProp::kNumProps; i++)
    if (name == k_ItemPropNames[i])
    {
      if (!g_ItemProps[i])
        g_ItemProps[i] = detach_abi(make<CItemProperty>(i));
      ICustomProperty p { nullptr };
      copy_from_abi(p, g_ItemProps[i]);
      return p;
    }
  return nullptr;
}

hstring CItemViewModel::GetStringRepresentation()
{
  return ToHString(Name);
}

winrt::Windows::UI::Xaml::Interop::TypeName CItemViewModel::Type()
{
  return GetObjectTypeName();
}


CPropertyRow::CPropertyRow(const UString &name, const UString &value):
    Name(name),
    Value(value)
{
  AddProperty(L"Name", [this] { return BoxString(Name); });
  AddProperty(L"Value", [this] { return BoxString(Value); });
}
