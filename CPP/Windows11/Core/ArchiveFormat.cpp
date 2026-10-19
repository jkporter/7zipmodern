// ArchiveFormat.cpp

#include "StdAfx.h"

#include "ArchiveFormat.h"

#include "../../Windows/PropVariant.h"
#include "../../Windows/Synchronization.h"

#include "OperationContext.h"
#include "PathSanitizer.h"
#include "PropertyFormatter.h"
#include "SevenZipEngine.h"

using namespace NWindows;

UString CArchiveFormat::GetDefaultExtension() const
{
  if (!Extensions.IsEmpty())
    return Extensions[0];
  UString s = Name;
  s.MakeLower_Ascii();
  return s;
}

bool CArchiveFormat::MatchesExtension(const UString &extension) const
{
  const wchar_t *ext = extension;
  while (*ext == '.')
    ext++;
  FOR_VECTOR (i, Extensions)
    if (Extensions[i].IsEqualTo_NoCase(ext))
      return true;
  return false;
}

bool CArchiveFormat::MatchesSignature(const Byte *header, size_t size) const
{
  FOR_VECTOR (i, Signatures)
  {
    const CByteBuffer &sig = Signatures[i];
    if (sig.Size() == 0 || SignatureOffset + sig.Size() > size)
      continue;
    if (memcmp(header + SignatureOffset, sig, sig.Size()) == 0)
      return true;
  }
  return false;
}

UString CArchiveFormat::GetDefaultItemName(const UString &archiveFileName) const
{
  UString ext = GetExtensionPart(archiveFileName);
  if (!ext.IsEmpty())
    ext.DeleteFrontal(1);
  const UString stem = GetFileNameWithoutExtension(archiveFileName);
  FOR_VECTOR (i, Extensions)
  {
    if (!Extensions[i].IsEqualTo_NoCase(ext))
      continue;
    const UString add = i < AddExtensions.Size() ? AddExtensions[i] : UString("*");
    if (add.IsEqualTo("*"))
      return stem;
    return stem + add;
  }
  if (ext.IsEmpty())
    return archiveFileName + L'~';
  return stem;
}


namespace NFormatRegistry {

static NSynchronization::CCriticalSection g_CS;
static CObjectVector<CArchiveFormat> g_Formats;
static volatile bool g_Loaded = false;

static void SplitExtensions(const UString &s, UStringVector &dest)
{
  dest.Clear();
  UString cur;
  for (unsigned i = 0;; i++)
  {
    const wchar_t c = s[i];
    if (c == 0 || c == ' ')
    {
      if (!cur.IsEmpty())
      {
        cur.MakeLower_Ascii();
        dest.Add(cur);
        cur.Empty();
      }
      if (c == 0)
        break;
    }
    else
      cur += c;
  }
}

static bool ReadString(UInt32 index, PROPID propID, UString &s)
{
  NCOM::CPropVariant prop;
  if (NSevenZipEngine::GetHandlerProperty(index, propID, prop) != S_OK)
    return false;
  return PropToString(prop, s);
}

static bool ReadBinary(UInt32 index, PROPID propID, CByteBuffer &buf)
{
  buf.Free();
  NCOM::CPropVariant prop;
  if (NSevenZipEngine::GetHandlerProperty(index, propID, prop) != S_OK)
    return false;
  if (prop.vt != VT_BSTR || !prop.bstrVal)
    return false;
  // 7-Zip stores binary blobs (GUIDs, signatures) as byte-length BSTRs.
  buf.CopyFrom((const Byte *)prop.bstrVal, ::SysStringByteLen(prop.bstrVal));
  return true;
}

static bool ReadBool(UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  bool v = false;
  if (NSevenZipEngine::GetHandlerProperty(index, propID, prop) == S_OK)
    PropToBool(prop, v);
  return v;
}

static UInt32 ReadUInt32(UInt32 index, PROPID propID)
{
  NCOM::CPropVariant prop;
  UInt32 v = 0;
  if (NSevenZipEngine::GetHandlerProperty(index, propID, prop) == S_OK)
    PropToUInt32(prop, v);
  return v;
}

static HRESULT LoadFormats()
{
  UInt32 count;
  RINOK(NSevenZipEngine::GetNumberOfFormats(count))
  for (UInt32 i = 0; i < count; i++)
  {
    CByteBuffer clsid;
    if (!ReadBinary(i, NArchive::NHandlerPropID::kClassID, clsid) || clsid.Size() < sizeof(GUID))
      continue;
    CArchiveFormat &f = g_Formats.AddNew();
    f.Index = i;
    if (!ReadString(i, NArchive::NHandlerPropID::kName, f.Name) || f.Name.IsEmpty())
    {
      f.Name = "#";
      f.Name.Add_UInt32(i);
    }
    memcpy(&f.ClassId, clsid, sizeof(GUID));
    UString s;
    ReadString(i, NArchive::NHandlerPropID::kExtension, s);
    SplitExtensions(s, f.Extensions);
    ReadString(i, NArchive::NHandlerPropID::kAddExtension, s);
    SplitExtensions(s, f.AddExtensions);
    f.CanUpdate = ReadBool(i, NArchive::NHandlerPropID::kUpdate);
    f.Flags = ReadUInt32(i, NArchive::NHandlerPropID::kFlags);
    f.SignatureOffset = ReadUInt32(i, NArchive::NHandlerPropID::kSignatureOffset);

    CByteBuffer sig;
    if (ReadBinary(i, NArchive::NHandlerPropID::kSignature, sig) && sig.Size() != 0)
      f.Signatures.Add(sig);
    if (ReadBinary(i, NArchive::NHandlerPropID::kMultiSignature, sig) && sig.Size() != 0)
    {
      // Multi-signature blob: sequence of [len][bytes...].
      for (size_t pos = 0; pos < sig.Size();)
      {
        const size_t len = sig[pos++];
        if (pos + len > sig.Size())
          break;
        CByteBuffer &one = f.Signatures.AddNew();
        one.CopyFrom(sig + pos, len);
        pos += len;
      }
    }
  }
  return S_OK;
}

HRESULT Load(UString &errorMessage)
{
  NSynchronization::CCriticalSectionLock lock(g_CS);
  if (g_Loaded)
    return S_OK;
  const HRESULT res = NSevenZipEngine::EnsureLoaded(FString(), errorMessage);
  if (res != S_OK)
    return res;
  g_Formats.Clear();
  const HRESULT res2 = LoadFormats();
  if (res2 != S_OK)
  {
    FormatHResultMessage(res2, UString("Reading the engine's format list"), errorMessage);
    g_Formats.Clear();
    return res2;
  }
  g_Loaded = true;
  return S_OK;
}

const CObjectVector<CArchiveFormat> &All()
{
  if (!g_Loaded)
  {
    UString errorMessage;
    Load(errorMessage);
  }
  return g_Formats;
}

const CArchiveFormat *Find(const UString &name)
{
  const CObjectVector<CArchiveFormat> &all = All();
  FOR_VECTOR (i, all)
    if (all[i].Name.IsEqualTo_NoCase(name))
      return &all[i];
  return NULL;
}

static int CompareNoCase(void *const *a, void *const *b, void * /* param */)
{
  return MyStringCompareNoCase(*(const UString *)*a, *(const UString *)*b);
}

void GetAllExtensions(UStringVector &extensions)
{
  extensions.Clear();
  const CObjectVector<CArchiveFormat> &all = All();
  FOR_VECTOR (i, all)
  {
    const UStringVector &exts = all[i].Extensions;
    FOR_VECTOR (k, exts)
    {
      bool found = false;
      FOR_VECTOR (j, extensions)
        if (extensions[j].IsEqualTo_NoCase(exts[k]))
        {
          found = true;
          break;
        }
      if (!found)
        extensions.Add(exts[k]);
    }
  }
  extensions.Sort(CompareNoCase, NULL);
}

static bool IsDigit(wchar_t c) { return c >= '0' && c <= '9'; }

bool LooksLikeArchive(const UString &path)
{
  const UString ext = GetExtensionPart(path);
  if (ext.Len() < 2)
    return false;
  // Numbered split volumes (.001) and multi-part rar are handled by Split/Rar.
  if (ext.Len() == 4 && IsDigit(ext[1]) && IsDigit(ext[2]) && IsDigit(ext[3]))
    return true;
  const CObjectVector<CArchiveFormat> &all = All();
  FOR_VECTOR (i, all)
    if (!all[i].Is_Flag(NArcInfoFlags::kPreArc) && all[i].MatchesExtension(ext))
      return true;
  return false;
}

}
