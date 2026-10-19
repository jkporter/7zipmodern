// SettingsService.cpp

#include "StdAfx.h"

#include <ShlObj_core.h>

#include "SettingsService.h"

#include "../../Common/StringToInt.h"
#include "../../Common/UTFConvert.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileIO.h"

#include "../HString.h"

#include "ShellServices.h"

using namespace NWindows;
using namespace NFile;
using namespace winrt::Windows::Data::Json;

static const unsigned kMaxRecent = 30;

CAppSettings::CAppSettings():
    Theme(NAppTheme::kSystem),
    Backdrop(NBackdropKind::kMicaAlt),
    SmartExtractFolder(true),
    OpenFolderAfterExtract(true),
    OverwriteMode(NOverwriteMode::kAsk),
    ZonePropagation(NZonePropagation::kAll),
    RestoreSymbolicLinks(true),
    DefaultFormat(L"7z"),
    DefaultLevel(NCompressionLevel::kNormal),
    DefaultEncryptFileNames(true),
    ShowDetailsPane(true),
    ConfirmDelete(true),
    AutoOpenTarballs(true),
    MenuCascaded(true),
    MenuOpen(true),
    MenuExtract(true),
    MenuExtractHere(true),
    MenuExtractSmart(true),
    MenuTest(true),
    MenuCompress(true),
    MenuCompress7z(true),
    MenuCompressZip(true),
    MenuCompressShare(true),
    MenuHash(true),
    NotifyOnCompletion(true),
    EfficiencyModeForBackground(false),
    OfferSavedPasswords(true)
    {}


// ---------- enums are stored by name (System.Text.Json UseStringEnumConverter) ----------

struct CEnumName
{
  int Value;
  const char *Name;
};

static const CEnumName k_ThemeNames[] =
{
  { NAppTheme::kSystem, "System" },
  { NAppTheme::kLight, "Light" },
  { NAppTheme::kDark, "Dark" }
};

static const CEnumName k_BackdropNames[] =
{
  { NBackdropKind::kMica, "Mica" },
  { NBackdropKind::kMicaAlt, "MicaAlt" },
  { NBackdropKind::kAcrylic, "Acrylic" }
};

static const CEnumName k_OverwriteNames[] =
{
  { NOverwriteMode::kAsk, "Ask" },
  { NOverwriteMode::kOverwrite, "Overwrite" },
  { NOverwriteMode::kSkip, "Skip" },
  { NOverwriteMode::kRenameNew, "RenameNew" },
  { NOverwriteMode::kRenameExisting, "RenameExisting" }
};

static const CEnumName k_ZoneNames[] =
{
  { NZonePropagation::kOff, "Off" },
  { NZonePropagation::kAll, "All" },
  { NZonePropagation::kOfficeFilesOnly, "OfficeFilesOnly" }
};

static const CEnumName k_LevelNames[] =
{
  { NCompressionLevel::kStore, "Store" },
  { NCompressionLevel::kFastest, "Fastest" },
  { NCompressionLevel::kFast, "Fast" },
  { NCompressionLevel::kNormal, "Normal" },
  { NCompressionLevel::kMaximum, "Maximum" },
  { NCompressionLevel::kUltra, "Ultra" }
};

#define ENUM_NAMES(a) a, Z7_ARRAY_SIZE(a)

static const char *GetEnumName(const CEnumName *names, unsigned num, int value)
{
  for (unsigned i = 0; i < num; i++)
    if (names[i].Value == value)
      return names[i].Name;
  return names[0].Name;
}

static int ReadEnum(const JsonObject &obj, const wchar_t *key, const CEnumName *names, unsigned num, int defaultValue)
{
  const IJsonValue v = obj.TryLookup(key);
  if (!v)
    return defaultValue;
  if (v.ValueType() == JsonValueType::Number)
  {
    // the converter also accepts numbers
    const int n = (int)v.GetNumber();
    for (unsigned i = 0; i < num; i++)
      if (names[i].Value == n)
        return n;
    return defaultValue;
  }
  if (v.ValueType() != JsonValueType::String)
    return defaultValue;
  const UString s = NWindows11::ToUString(v.GetString());
  for (unsigned i = 0; i < num; i++)
    if (s.IsEqualTo_Ascii_NoCase(names[i].Name))
      return names[i].Value;
  return defaultValue;
}

static bool ReadBool(const JsonObject &obj, const wchar_t *key, bool defaultValue)
{
  const IJsonValue v = obj.TryLookup(key);
  if (!v || v.ValueType() != JsonValueType::Boolean)
    return defaultValue;
  return v.GetBoolean();
}

static UString ReadString(const JsonObject &obj, const wchar_t *key, const UString &defaultValue)
{
  const IJsonValue v = obj.TryLookup(key);
  if (!v || v.ValueType() != JsonValueType::String)
    return defaultValue;
  return NWindows11::ToUString(v.GetString());
}


// ---------- DateTime (.NET writes local time with the UTC offset: 2026-10-18T14:03:12.1234567+02:00) ----------

static bool ReadDigits(const wchar_t *&s, unsigned num, UInt32 &value)
{
  value = 0;
  for (unsigned i = 0; i < num; i++)
  {
    const wchar_t c = s[i];
    if (c < '0' || c > '9')
      return false;
    value = value * 10 + (UInt32)(c - '0');
  }
  s += num;
  return true;
}

static bool ParseDateTime(const UString &text, FILETIME &utc)
{
  const wchar_t *s = text;
  UInt32 year, month, day, hour, minute, second;
  if (!ReadDigits(s, 4, year) || *s++ != '-'
      || !ReadDigits(s, 2, month) || *s++ != '-'
      || !ReadDigits(s, 2, day) || (*s != 'T' && *s != ' '))
    return false;
  s++;
  if (!ReadDigits(s, 2, hour) || *s++ != ':'
      || !ReadDigits(s, 2, minute))
    return false;
  second = 0;
  if (*s == ':')
  {
    s++;
    if (!ReadDigits(s, 2, second))
      return false;
  }
  UInt32 ticks = 0; // 100 ns
  if (*s == '.')
  {
    s++;
    unsigned n = 0;
    for (; *s >= '0' && *s <= '9'; s++, n++)
      if (n < 7)
        ticks = ticks * 10 + (UInt32)(*s - '0');
    for (; n < 7; n++)
      ticks *= 10;
  }

  SYSTEMTIME st;
  st.wYear = (WORD)year;
  st.wMonth = (WORD)month;
  st.wDay = (WORD)day;
  st.wDayOfWeek = 0;
  st.wHour = (WORD)hour;
  st.wMinute = (WORD)minute;
  st.wSecond = (WORD)second;
  st.wMilliseconds = 0;

  bool isUtc = false;
  Int64 offsetMinutes = 0;
  if (*s == 'Z')
    isUtc = true;
  else if (*s == '+' || *s == '-')
  {
    const bool minus = (*s == '-');
    s++;
    UInt32 oh, om;
    if (!ReadDigits(s, 2, oh) || *s++ != ':' || !ReadDigits(s, 2, om))
      return false;
    offsetMinutes = (Int64)(oh * 60 + om);
    if (minus)
      offsetMinutes = -offsetMinutes;
    isUtc = true;
  }

  if (!isUtc)
  {
    // no offset: local time
    SYSTEMTIME u;
    if (!::TzSpecificLocalTimeToSystemTime(NULL, &st, &u))
      return false;
    st = u;
  }
  FILETIME ft;
  if (!::SystemTimeToFileTime(&st, &ft))
    return false;
  UInt64 v = ((UInt64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  v += ticks;
  v = (UInt64)((Int64)v - offsetMinutes * 60 * 10000000);
  utc.dwLowDateTime = (DWORD)v;
  utc.dwHighDateTime = (DWORD)(v >> 32);
  return true;
}

static void AddDigits(UString &s, UInt32 value, unsigned num)
{
  wchar_t temp[16];
  for (unsigned i = num; i != 0;)
  {
    temp[--i] = (wchar_t)('0' + value % 10);
    value /= 10;
  }
  temp[num] = 0;
  s += temp;
}

static UString FormatDateTime(const FILETIME &utc)
{
  UString s;
  SYSTEMTIME u, l;
  if (!::FileTimeToSystemTime(&utc, &u))
    return s;
  if (!::SystemTimeToTzSpecificLocalTime(NULL, &u, &l))
    l = u;
  FILETIME lft;
  if (!::SystemTimeToFileTime(&l, &lft))
    return s;
  const UInt64 vu = ((UInt64)utc.dwHighDateTime << 32) | utc.dwLowDateTime;
  const UInt64 vl = ((UInt64)lft.dwHighDateTime << 32) | lft.dwLowDateTime;
  const UInt32 ticks = (UInt32)(vu % 10000000);
  // the local SYSTEMTIME has whole milliseconds; the offset is a whole number of minutes
  const Int64 offsetMinutes = ((Int64)(vl / 10000000) - (Int64)(vu / 10000000)) / 60;

  AddDigits(s, l.wYear, 4); s.Add_Char('-');
  AddDigits(s, l.wMonth, 2); s.Add_Char('-');
  AddDigits(s, l.wDay, 2); s.Add_Char('T');
  AddDigits(s, l.wHour, 2); s.Add_Char(':');
  AddDigits(s, l.wMinute, 2); s.Add_Char(':');
  AddDigits(s, l.wSecond, 2); s.Add_Char('.');
  AddDigits(s, ticks, 7);
  const UInt32 absOffset = (UInt32)(offsetMinutes < 0 ? -offsetMinutes : offsetMinutes);
  s.Add_Char(offsetMinutes < 0 ? '-' : '+');
  AddDigits(s, absOffset / 60, 2); s.Add_Char(':');
  AddDigits(s, absOffset % 60, 2);
  return s;
}


// ---------- JSON writer (indented, like WriteIndented = true) ----------

class CJsonWriter
{
  UString _text;
  bool _first;
  unsigned _indent;

  void NewItem()
  {
    if (!_first)
      _text.Add_Char(',');
    _first = false;
    _text += "\r\n";
    for (unsigned i = 0; i < _indent; i++)
      _text += "  ";
  }
  void Key(const char *key)
  {
    NewItem();
    _text.Add_Char('\"');
    _text += key;
    _text += "\": ";
  }
public:
  CJsonWriter(): _first(true), _indent(0) {}
  const UString &GetText() const { return _text; }

  void BeginObject() { _text.Add_Char('{'); _first = true; _indent++; }
  void EndObject()
  {
    _indent--;
    _text += "\r\n";
    for (unsigned i = 0; i < _indent; i++)
      _text += "  ";
    _text.Add_Char('}');
    _first = false;
  }
  void BeginArray(const char *key) { Key(key); _text.Add_Char('['); _first = true; _indent++; }
  void EndArray()
  {
    _indent--;
    if (!_first)
    {
      _text += "\r\n";
      for (unsigned i = 0; i < _indent; i++)
        _text += "  ";
    }
    _text.Add_Char(']');
    _first = false;
  }
  void BeginArrayObject() { NewItem(); BeginObject(); }

  void Bool(const char *key, bool v) { Key(key); _text += (v ? "true" : "false"); }
  void String(const char *key, const UString &v)
  {
    Key(key);
    _text += NWindows11::ToUString(JsonValue::CreateStringValue(NWindows11::ToHString(v)).Stringify());
  }
  void Enum(const char *key, const CEnumName *names, unsigned num, int value)
  {
    Key(key);
    _text.Add_Char('\"');
    _text += GetEnumName(names, num, value);
    _text.Add_Char('\"');
  }
};


// ---------- CSettingsService ----------

static FString GetSettingsFolder()
{
  if (NShellActions::IsPackaged())
  {
    try
    {
      UString path = NWindows11::ToUString(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
      if (!path.IsEmpty())
        return us2fs(path);
    }
    catch (...) {}
  }
  FString folder;
  PWSTR p = NULL;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &p)))
  {
    folder = us2fs(p);
    ::CoTaskMemFree(p);
  }
  folder.Add_PathSepar();
  folder += "7-Zip WinUI";
  return folder;
}

static bool ReadTextFile(CFSTR path, UString &text)
{
  NIO::CInFile file;
  if (!file.Open(path))
    return false;
  UInt64 length;
  if (!file.GetLength(length) || length > (1 << 24))
    return false;
  CByteBuffer buf((size_t)length);
  size_t processed;
  if (!file.ReadFull(buf, (size_t)length, processed) || processed != length)
    return false;
  const Byte *p = buf;
  size_t size = processed;
  if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE)
  {
    text.SetFrom((const wchar_t *)(const void *)(p + 2), (unsigned)((size - 2) / 2));
    return true;
  }
  if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
  {
    p += 3;
    size -= 3;
  }
  AString utf8;
  utf8.SetFrom((const char *)p, (unsigned)size);
  ConvertUTF8ToUnicode(utf8, text);
  return true;
}

static bool WriteFileAtomic(const FString &path, const void *data, size_t size)
{
  FString tmp (path);
  tmp += ".tmp";
  {
    NIO::COutFile file;
    if (!file.Create_ALWAYS(tmp))
      return false;
    if (!file.WriteFull(data, size))
      return false;
  }
  return ::MoveFileExW(fs2us(tmp), fs2us(path), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}


void CSettingsService::Load()
{
  Folder = GetSettingsFolder();
  NDir::CreateComplexDir(Folder);
  Folder.Add_PathSepar();
  _file = Folder;
  _file += "settings.json";

  UString text;
  if (ReadTextFile(_file, text))
  {
    // a corrupt file gives the defaults rather than refusing to launch
    JsonObject obj { nullptr };
    if (JsonObject::TryParse(NWindows11::ToHString(text), obj) && obj)
    {
      CAppSettings &s = Current;
      try
      {
        s.Theme = (NAppTheme::EEnum)ReadEnum(obj, L"Theme", ENUM_NAMES(k_ThemeNames), s.Theme);
        s.Backdrop = (NBackdropKind::EEnum)ReadEnum(obj, L"Backdrop", ENUM_NAMES(k_BackdropNames), s.Backdrop);
        s.SmartExtractFolder = ReadBool(obj, L"SmartExtractFolder", s.SmartExtractFolder);
        s.OpenFolderAfterExtract = ReadBool(obj, L"OpenFolderAfterExtract", s.OpenFolderAfterExtract);
        s.OverwriteMode = (NOverwriteMode::EEnum)ReadEnum(obj, L"OverwriteMode", ENUM_NAMES(k_OverwriteNames), s.OverwriteMode);
        s.ZonePropagation = (NZonePropagation::EEnum)ReadEnum(obj, L"ZonePropagation", ENUM_NAMES(k_ZoneNames), s.ZonePropagation);
        s.RestoreSymbolicLinks = ReadBool(obj, L"RestoreSymbolicLinks", s.RestoreSymbolicLinks);
        s.DefaultFormat = ReadString(obj, L"DefaultFormat", s.DefaultFormat);
        s.DefaultLevel = (NCompressionLevel::EEnum)ReadEnum(obj, L"DefaultLevel", ENUM_NAMES(k_LevelNames), s.DefaultLevel);
        s.DefaultEncryptFileNames = ReadBool(obj, L"DefaultEncryptFileNames", s.DefaultEncryptFileNames);
        s.ShowDetailsPane = ReadBool(obj, L"ShowDetailsPane", s.ShowDetailsPane);
        s.ConfirmDelete = ReadBool(obj, L"ConfirmDelete", s.ConfirmDelete);
        s.AutoOpenTarballs = ReadBool(obj, L"AutoOpenTarballs", s.AutoOpenTarballs);
        s.MenuCascaded = ReadBool(obj, L"MenuCascaded", s.MenuCascaded);
        s.MenuOpen = ReadBool(obj, L"MenuOpen", s.MenuOpen);
        s.MenuExtract = ReadBool(obj, L"MenuExtract", s.MenuExtract);
        s.MenuExtractHere = ReadBool(obj, L"MenuExtractHere", s.MenuExtractHere);
        s.MenuExtractSmart = ReadBool(obj, L"MenuExtractSmart", s.MenuExtractSmart);
        s.MenuTest = ReadBool(obj, L"MenuTest", s.MenuTest);
        s.MenuCompress = ReadBool(obj, L"MenuCompress", s.MenuCompress);
        s.MenuCompress7z = ReadBool(obj, L"MenuCompress7z", s.MenuCompress7z);
        s.MenuCompressZip = ReadBool(obj, L"MenuCompressZip", s.MenuCompressZip);
        s.MenuCompressShare = ReadBool(obj, L"MenuCompressShare", s.MenuCompressShare);
        s.MenuHash = ReadBool(obj, L"MenuHash", s.MenuHash);
        s.NotifyOnCompletion = ReadBool(obj, L"NotifyOnCompletion", s.NotifyOnCompletion);
        s.EfficiencyModeForBackground = ReadBool(obj, L"EfficiencyModeForBackground", s.EfficiencyModeForBackground);
        s.OfferSavedPasswords = ReadBool(obj, L"OfferSavedPasswords", s.OfferSavedPasswords);

        const IJsonValue recent = obj.TryLookup(L"Recent");
        if (recent && recent.ValueType() == JsonValueType::Array)
        {
          for (const IJsonValue &item: recent.GetArray())
          {
            if (item.ValueType() != JsonValueType::Object || s.Recent.Size() >= kMaxRecent)
              continue;
            const JsonObject o = item.GetObject();
            CRecentArchive r;
            r.Path = ReadString(o, L"Path", UString());
            if (r.Path.IsEmpty())
              continue;
            if (!ParseDateTime(ReadString(o, L"LastOpened", UString()), r.LastOpened))
              ::GetSystemTimeAsFileTime(&r.LastOpened);
            s.Recent.Add(r);
          }
        }
      }
      catch (...)
      {
        Current = CAppSettings();
      }
    }
  }
  WriteShellConfig();
}


void CSettingsService::Save()
{
  NSynchronization::CCriticalSectionLock lock(_cs);
  const CAppSettings &s = Current;
  CJsonWriter w;
  w.BeginObject();
  w.Enum("Theme", ENUM_NAMES(k_ThemeNames), s.Theme);
  w.Enum("Backdrop", ENUM_NAMES(k_BackdropNames), s.Backdrop);
  w.Bool("SmartExtractFolder", s.SmartExtractFolder);
  w.Bool("OpenFolderAfterExtract", s.OpenFolderAfterExtract);
  w.Enum("OverwriteMode", ENUM_NAMES(k_OverwriteNames), s.OverwriteMode);
  w.Enum("ZonePropagation", ENUM_NAMES(k_ZoneNames), s.ZonePropagation);
  w.Bool("RestoreSymbolicLinks", s.RestoreSymbolicLinks);
  w.String("DefaultFormat", s.DefaultFormat);
  w.Enum("DefaultLevel", ENUM_NAMES(k_LevelNames), s.DefaultLevel);
  w.Bool("DefaultEncryptFileNames", s.DefaultEncryptFileNames);
  w.Bool("ShowDetailsPane", s.ShowDetailsPane);
  w.Bool("ConfirmDelete", s.ConfirmDelete);
  w.Bool("AutoOpenTarballs", s.AutoOpenTarballs);
  w.Bool("MenuCascaded", s.MenuCascaded);
  w.Bool("MenuOpen", s.MenuOpen);
  w.Bool("MenuExtract", s.MenuExtract);
  w.Bool("MenuExtractHere", s.MenuExtractHere);
  w.Bool("MenuExtractSmart", s.MenuExtractSmart);
  w.Bool("MenuTest", s.MenuTest);
  w.Bool("MenuCompress", s.MenuCompress);
  w.Bool("MenuCompress7z", s.MenuCompress7z);
  w.Bool("MenuCompressZip", s.MenuCompressZip);
  w.Bool("MenuCompressShare", s.MenuCompressShare);
  w.Bool("MenuHash", s.MenuHash);
  w.Bool("NotifyOnCompletion", s.NotifyOnCompletion);
  w.Bool("EfficiencyModeForBackground", s.EfficiencyModeForBackground);
  w.Bool("OfferSavedPasswords", s.OfferSavedPasswords);
  w.BeginArray("Recent");
  FOR_VECTOR (i, s.Recent)
  {
    const CRecentArchive &r = s.Recent[i];
    w.BeginArrayObject();
    w.String("Path", r.Path);
    w.String("LastOpened", FormatDateTime(r.LastOpened));
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();

  AString utf8;
  ConvertUnicodeToUTF8(w.GetText(), utf8);
  WriteFileAtomic(_file, utf8.Ptr(), utf8.Len());
  WriteShellConfig();
}


// Tiny key=value file the Explorer extension reads (it must not parse JSON).

void CSettingsService::WriteShellConfig()
{
  const CAppSettings &s = Current;
  UString text (L"\xFEFF"); // UTF-16 LE with BOM, as C# Encoding.Unicode wrote it
  #define W(k, v) text += k; text.Add_Char('='); text.Add_Char((v) ? '1' : '0'); text += "\r\n";
  W("Cascaded", s.MenuCascaded)
  W("Open", s.MenuOpen)
  W("Extract", s.MenuExtract)
  W("ExtractHere", s.MenuExtractHere)
  W("ExtractSmart", s.MenuExtractSmart)
  W("Test", s.MenuTest)
  W("Compress", s.MenuCompress)
  W("Compress7z", s.MenuCompress7z)
  W("CompressZip", s.MenuCompressZip)
  W("CompressShare", s.MenuCompressShare)
  W("Hash", s.MenuHash)
  #undef W
  FString path (Folder);
  path += "contextmenu.ini";
  NIO::COutFile file;
  if (file.Create_ALWAYS(path))
    file.WriteFull(text.Ptr(), text.Len() * sizeof(wchar_t));
}


void CSettingsService::AddRecent(const UString &path)
{
  CObjectVector<CRecentArchive> &list = Current.Recent;
  for (unsigned i = list.Size(); i != 0;)
  {
    i--;
    if (list[i].Path.IsEqualTo_NoCase(path))
      list.Delete(i);
  }
  CRecentArchive r;
  r.Path = path;
  ::GetSystemTimeAsFileTime(&r.LastOpened);
  list.Insert(0, r);
  while (list.Size() > kMaxRecent)
    list.DeleteBack();
  Save();
  NShellActions::AddToRecentDocuments(path);
}


void CSettingsService::RemoveRecent(const UString &path)
{
  CObjectVector<CRecentArchive> &list = Current.Recent;
  for (unsigned i = list.Size(); i != 0;)
  {
    i--;
    if (list[i].Path.IsEqualTo_NoCase(path))
      list.Delete(i);
  }
  Save();
}


void CSettingsService::ClearRecent()
{
  Current.Recent.Clear();
  Save();
}
