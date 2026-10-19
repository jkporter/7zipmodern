// PropertyFormatter.cpp

#include "StdAfx.h"

#include "PropertyFormatter.h"

#include "../../Common/IntToString.h"

bool PropToUInt64(const PROPVARIANT &prop, UInt64 &value)
{
  switch (prop.vt)
  {
    case VT_UI1: value = prop.bVal; return true;
    case VT_UI2: value = prop.uiVal; return true;
    case VT_UI4: value = prop.ulVal; return true;
    case VT_UI8: value = (UInt64)prop.uhVal.QuadPart; return true;
    case VT_I2: value = (UInt64)(Int64)prop.iVal; return true;
    case VT_I4: value = (UInt64)(Int64)prop.lVal; return true;
    case VT_I8: value = (UInt64)prop.hVal.QuadPart; return true;
    default: value = 0; return false;
  }
}

bool PropToUInt32(const PROPVARIANT &prop, UInt32 &value)
{
  UInt64 v;
  const bool res = PropToUInt64(prop, v);
  value = (UInt32)v;
  return res;
}

bool PropToBool(const PROPVARIANT &prop, bool &value)
{
  value = false;
  if (prop.vt != VT_BOOL)
    return false;
  value = (prop.boolVal != VARIANT_FALSE);
  return true;
}

bool PropToString(const PROPVARIANT &prop, UString &value)
{
  value.Empty();
  if (prop.vt != VT_BSTR || !prop.bstrVal)
    return false;
  value.SetFromBstr(prop.bstrVal);
  return true;
}

bool PropToFileTime(const PROPVARIANT &prop, FILETIME &value)
{
  value.dwLowDateTime = value.dwHighDateTime = 0;
  if (prop.vt != VT_FILETIME)
    return false;
  if ((Int32)prop.filetime.dwHighDateTime < 0
      || (prop.filetime.dwHighDateTime == 0 && prop.filetime.dwLowDateTime == 0))
    return false;
  value = prop.filetime;
  return true;
}

namespace NPropertyFormatter {

struct CNumberFormat
{
  wchar_t Decimal[8];
  wchar_t Thousand[8];
  NUMBERFMTW Fmt;

  CNumberFormat()
  {
    if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, Decimal, Z7_ARRAY_SIZE(Decimal)) == 0)
      MyStringCopy(Decimal, L".");
    if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, Thousand, Z7_ARRAY_SIZE(Thousand)) == 0)
      MyStringCopy(Thousand, L",");
    wchar_t grouping[16];
    UINT group = 3;
    if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SGROUPING, grouping, Z7_ARRAY_SIZE(grouping)) != 0)
    {
      // "3;0" -> 3, "3;2;0" -> 32 (the NUMBERFMT encoding)
      group = 0;
      for (const wchar_t *p = grouping; *p; p++)
        if (*p >= '1' && *p <= '9')
          group = group * 10 + (UINT)(*p - '0');
    }
    DWORD leadingZero = 1;
    ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ILZERO | LOCALE_RETURN_NUMBER,
        (LPWSTR)(void *)&leadingZero, sizeof(leadingZero) / sizeof(wchar_t));
    Fmt.NumDigits = 0;
    Fmt.LeadingZero = leadingZero;
    Fmt.Grouping = group;
    Fmt.lpDecimalSep = Decimal;
    Fmt.lpThousandSep = Thousand;
    Fmt.NegativeOrder = 1;
  }
};

static UString FormatDigits(const wchar_t *digits, unsigned numDecimals)
{
  static const CNumberFormat g_Format;
  NUMBERFMTW fmt = g_Format.Fmt;
  fmt.NumDigits = numDecimals;
  wchar_t buf[96];
  if (::GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, digits, &fmt, buf, Z7_ARRAY_SIZE(buf)) == 0)
    return UString(digits);
  return UString(buf);
}

UString FormatNumber(UInt64 value)
{
  wchar_t s[32];
  ConvertUInt64ToString(value, s);
  return FormatDigits(s, 0);
}

UString FormatDecimal(double value, unsigned numDecimals)
{
  UInt64 scale = 1;
  for (unsigned i = 0; i < numDecimals; i++)
    scale *= 10;
  if (value < 0)
    value = 0;
  const UInt64 scaled = (UInt64)(value * (double)scale + 0.5);
  UString s;
  s.Add_UInt64(scaled / scale);
  if (numDecimals != 0)
  {
    s.Add_Dot();
    wchar_t frac[32];
    ConvertUInt64ToString(scaled % scale, frac);
    for (unsigned k = MyStringLen(frac); k < numDecimals; k++)
      s += L'0';
    s += frac;
  }
  return FormatDigits(s, numDecimals);
}

UString FormatSize(UInt64 bytes)
{
  static const char * const kUnits[] = { "bytes", "KB", "MB", "GB", "TB", "PB" };
  UString s;
  if (bytes < 1024)
  {
    if (bytes == 1)
      return UString("1 byte");
    s = FormatNumber(bytes);
    s += " bytes";
    return s;
  }
  double v = (double)bytes;
  unsigned u = 0;
  while (v >= 1024 && u < Z7_ARRAY_SIZE(kUnits) - 1)
  {
    v /= 1024;
    u++;
  }
  s = FormatDecimal(v, v >= 100 ? 0 : v >= 10 ? 1 : 2);
  s.Add_Space();
  s += kUnits[u];
  return s;
}

UString FormatSizeExact(UInt64 bytes)
{
  UString s = FormatSize(bytes);
  s += " (";
  s += FormatNumber(bytes);
  s += " bytes)";
  return s;
}

UString FormatCount(Int64 count, const wchar_t *singular, const wchar_t *plural)
{
  UString s;
  if (count < 0)
  {
    s += L'-';
    s += FormatNumber((UInt64)-count);
  }
  else
    s = FormatNumber((UInt64)count);
  s.Add_Space();
  if (count == 1)
    s += singular;
  else if (plural)
    s += plural;
  else
  {
    s += singular;
    s += L's';
  }
  return s;
}

UString FormatAttributes(UInt32 attrib)
{
  // High 16 bits may carry POSIX mode when bit 15 (FILE_ATTRIBUTE_UNIX_EXTENSION) is set.
  UString s;
  if (attrib & FILE_ATTRIBUTE_READONLY) s += L'R';
  if (attrib & FILE_ATTRIBUTE_HIDDEN) s += L'H';
  if (attrib & FILE_ATTRIBUTE_SYSTEM) s += L'S';
  if (attrib & FILE_ATTRIBUTE_DIRECTORY) s += L'D';
  if (attrib & FILE_ATTRIBUTE_ARCHIVE) s += L'A';
  if (attrib & FILE_ATTRIBUTE_REPARSE_POINT) s += L'L';
  if (attrib & FILE_ATTRIBUTE_COMPRESSED) s += L'C';
  if (attrib & FILE_ATTRIBUTE_ENCRYPTED) s += L'E';
  if (s.IsEmpty())
    s = L"\x2014";
  return s;
}

UString FormatPosixMode(UInt32 mode)
{
  wchar_t s[11];
  switch (mode & 0xF000)
  {
    case 0x4000: s[0] = 'd'; break;
    case 0xA000: s[0] = 'l'; break;
    case 0x2000: s[0] = 'c'; break;
    case 0x6000: s[0] = 'b'; break;
    case 0x1000: s[0] = 'p'; break;
    case 0xC000: s[0] = 's'; break;
    default: s[0] = '-';
  }
  const char *rwx = "rwxrwxrwx";
  for (unsigned i = 0; i < 9; i++)
    s[i + 1] = (mode & ((UInt32)1 << (8 - i))) ? (wchar_t)rwx[i] : '-';
  s[10] = 0;
  return UString(s);
}

UString FormatFileTime(const FILETIME &utc)
{
  SYSTEMTIME st, local;
  if (!::FileTimeToSystemTime(&utc, &st))
    return UString();
  if (!::SystemTimeToTzSpecificLocalTime(NULL, &st, &local))
    local = st;
  wchar_t date[80], time[80];
  if (::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, NULL, date, Z7_ARRAY_SIZE(date), NULL) == 0)
    date[0] = 0;
  if (::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, NULL, time, Z7_ARRAY_SIZE(time)) == 0)
    time[0] = 0;
  UString s = date;
  s.Add_Space_if_NotEmpty();
  s += time;
  return s;
}

static UString FormatHex(UInt64 v)
{
  char s[32];
  ConvertUInt64ToHex(v, s);
  for (char *p = s; *p; p++)
    *p = MyCharUpper_Ascii(*p);
  UString r ("0x");
  r += s;
  return r;
}

UString FormatValue(PROPID propID, const PROPVARIANT &value)
{
  UInt64 v;
  switch (propID)
  {
    case kpidSize: case kpidPackSize: case kpidPhySize: case kpidHeadersSize: case kpidTotalSize:
    case kpidFreeSpace: case kpidClusterSize: case kpidUnpackSize: case kpidTotalPhySize:
    case kpidDictionarySize: case kpidTailSize: case kpidEmbeddedStubSize: case kpidVirtualSize:
    case kpidAltStreamsSize:
      if (PropToUInt64(value, v))
        return FormatSizeExact(v);
      break;
    case kpidCRC:
      if (PropToUInt64(value, v))
      {
        char s[16];
        ConvertUInt32ToHex8Digits((UInt32)v, s);
        return UString(s);
      }
      break;
    case kpidAttrib:
      if (PropToUInt64(value, v))
        return FormatAttributes((UInt32)v);
      break;
    case kpidPosixAttrib:
      if (PropToUInt64(value, v))
        return FormatPosixMode((UInt32)v);
      break;
    case kpidVa: case kpidOffset: case kpidPosition:
      if (PropToUInt64(value, v))
        return FormatHex(v);
      break;
    case kpidErrorFlags: case kpidWarningFlags:
      if (PropToUInt64(value, v))
      {
        if ((UInt32)v == 0)
          return UString("None");
        UStringVector names;
        DescribeErrorFlags((UInt32)v, names);
        UString s;
        FOR_VECTOR (i, names)
        {
          if (i != 0)
            s += ", ";
          s += names[i];
        }
        return s;
      }
      break;
    default: break;
  }

  switch (value.vt)
  {
    case VT_EMPTY: return UString();
    case VT_BOOL: return UString(value.boolVal != VARIANT_FALSE ? "Yes" : "No");
    case VT_BSTR:
    {
      UString s;
      PropToString(value, s);
      return s;
    }
    case VT_FILETIME:
    {
      FILETIME ft;
      if (PropToFileTime(value, ft))
        return FormatFileTime(ft);
      return UString();
    }
    case VT_UI1: case VT_UI2: case VT_UI4: case VT_UI8:
      PropToUInt64(value, v);
      return FormatNumber(v);
    case VT_I2: case VT_I4: case VT_I8:
    {
      PropToUInt64(value, v);
      const Int64 i = (Int64)v;
      if (i >= 0)
        return FormatNumber(v);
      UString s ("-");
      s += FormatNumber((UInt64)-i);
      return s;
    }
    default:
    {
      UString s ("(type ");
      s.Add_UInt32(value.vt);
      s += L')';
      return s;
    }
  }
}

struct CPropName
{
  PROPID Id;
  const char *Name;
};

static const CPropName kPropNames[] =
{
  { kpidPath, "Path" }, { kpidName, "Name" }, { kpidExtension, "Extension" }, { kpidIsDir, "Folder" },
  { kpidSize, "Size" }, { kpidPackSize, "Packed size" }, { kpidAttrib, "Attributes" },
  { kpidCTime, "Created" }, { kpidATime, "Accessed" }, { kpidMTime, "Modified" }, { kpidSolid, "Solid" },
  { kpidCommented, "Commented" }, { kpidEncrypted, "Encrypted" }, { kpidSplitBefore, "Split before" },
  { kpidSplitAfter, "Split after" }, { kpidDictionarySize, "Dictionary size" }, { kpidCRC, "CRC" },
  { kpidType, "Type" }, { kpidIsAnti, "Anti" }, { kpidMethod, "Method" }, { kpidHostOS, "Host OS" },
  { kpidFileSystem, "File system" }, { kpidUser, "User" }, { kpidGroup, "Group" }, { kpidBlock, "Block" },
  { kpidComment, "Comment" }, { kpidPosition, "Position" }, { kpidPrefix, "Path prefix" },
  { kpidNumSubDirs, "Folders" }, { kpidNumSubFiles, "Files" }, { kpidUnpackVer, "Version" },
  { kpidVolume, "Volume" }, { kpidIsVolume, "Multivolume" }, { kpidOffset, "Offset" }, { kpidLinks, "Links" },
  { kpidNumBlocks, "Blocks" }, { kpidNumVolumes, "Volumes" }, { kpidBit64, "64-bit" },
  { kpidBigEndian, "Big-endian" }, { kpidCpu, "CPU" }, { kpidPhySize, "Physical size" },
  { kpidHeadersSize, "Headers size" }, { kpidChecksum, "Checksum" }, { kpidCharacts, "Characteristics" },
  { kpidVa, "Virtual address" }, { kpidId, "ID" }, { kpidShortName, "Short name" },
  { kpidCreatorApp, "Creator application" }, { kpidSectorSize, "Sector size" }, { kpidPosixAttrib, "Mode" },
  { kpidSymLink, "Symbolic link" }, { kpidError, "Error" }, { kpidTotalSize, "Total size" },
  { kpidFreeSpace, "Free space" }, { kpidClusterSize, "Cluster size" }, { kpidVolumeName, "Label" },
  { kpidLocalName, "Local name" }, { kpidProvider, "Provider" }, { kpidNtSecure, "NT security" },
  { kpidIsAltStream, "Alternate stream" }, { kpidIsAux, "Auxiliary" }, { kpidIsDeleted, "Deleted" },
  { kpidIsTree, "Tree" }, { kpidSha1, "SHA-1" }, { kpidSha256, "SHA-256" }, { kpidErrorType, "Error type" },
  { kpidNumErrors, "Errors" }, { kpidWarning, "Warning" }, { kpidNumStreams, "Streams" },
  { kpidNumAltStreams, "Alternate streams" }, { kpidAltStreamsSize, "Alternate streams size" },
  { kpidVirtualSize, "Virtual size" }, { kpidUnpackSize, "Unpack size" },
  { kpidTotalPhySize, "Total physical size" }, { kpidVolumeIndex, "Volume index" },
  { kpidSubType, "Subtype" }, { kpidShortComment, "Short comment" }, { kpidCodePage, "Code page" },
  { kpidTailSize, "Tail size" }, { kpidEmbeddedStubSize, "Embedded stub size" },
  { kpidNtReparse, "Reparse data" }, { kpidHardLink, "Hard link" }, { kpidINode, "iNode" },
  { kpidStreamId, "Stream ID" }, { kpidReadOnly, "Read-only" }, { kpidOutName, "Out name" },
  { kpidCopyLink, "Copy link" }, { kpidChangeTime, "Changed" }, { kpidUserId, "User ID" },
  { kpidGroupId, "Group ID" }
};

UString GetPropertyDisplayName(PROPID propID, const wchar_t *engineName)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kPropNames); i++)
    if (kPropNames[i].Id == propID)
      return UString(kPropNames[i].Name);
  if (engineName && *engineName)
    return UString(engineName);
  UString s ("Property ");
  s.Add_UInt32(propID);
  return s;
}

void DescribeErrorFlags(UInt32 flags, UStringVector &names)
{
  static const char * const kNames[] =
  {
    "Not an archive",
    "Headers error",
    "Encrypted headers error",
    "Unavailable start of archive",
    "Unconfirmed start of archive",
    "Unexpected end of archive",
    "There is data after the end of archive",
    "Unsupported method",
    "Unsupported feature",
    "Data error",
    "CRC error"
  };
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kNames); i++)
    if (flags & ((UInt32)1 << i))
      names.Add(UString(kNames[i]));
}

}
