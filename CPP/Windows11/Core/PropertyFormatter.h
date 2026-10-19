// PropertyFormatter.h

#ifndef ZIP7_INC_PROPERTY_FORMATTER_H
#define ZIP7_INC_PROPERTY_FORMATTER_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../7zip/PropID.h"

// One "name: value" row of the Properties pane.
struct CPropertyItem
{
  UString Name;
  UString Value;
};

typedef CObjectVector<CPropertyItem> CPropertyItemVector;

// PROPVARIANT readers (signed integers are accepted like unsigned ones).
bool PropToUInt64(const PROPVARIANT &prop, UInt64 &value);
bool PropToUInt32(const PROPVARIANT &prop, UInt32 &value);
bool PropToBool(const PROPVARIANT &prop, bool &value);
bool PropToString(const PROPVARIANT &prop, UString &value);  // VT_BSTR only
bool PropToFileTime(const PROPVARIANT &prop, FILETIME &value); // VT_FILETIME > 0 only

namespace NPropertyFormatter {

// Locale-aware formatting ("1,234", "1,234.5") with the user's separators.
UString FormatNumber(UInt64 value);
UString FormatDecimal(double value, unsigned numDecimals);

// "1 byte", "512 bytes", "1.50 MB", "12.3 GB", "123 KB"
UString FormatSize(UInt64 bytes);
// "1.50 MB (1,572,864 bytes)"
UString FormatSizeExact(UInt64 bytes);
// "1 file", "2 files", "1,024 files". Pass (plural) when it isn't singular + "s".
UString FormatCount(Int64 count, const wchar_t *singular, const wchar_t *plural = NULL);
// "RHSDA..." or an em dash when no attribute is set
UString FormatAttributes(UInt32 attrib);
// "drwxr-xr-x"
UString FormatPosixMode(UInt32 mode);
// UTC -> local time in the user's short date + long time format
UString FormatFileTime(const FILETIME &utc);
UString FormatValue(PROPID propID, const PROPVARIANT &value);

// Display name for an item / archive property (kpid*). Falls back to (engineName) or "Property N".
UString GetPropertyDisplayName(PROPID propID, const wchar_t *engineName = NULL);

// Names of the kpv_ErrorFlags_* bits that are set.
void DescribeErrorFlags(UInt32 flags, UStringVector &names);

}

#endif
