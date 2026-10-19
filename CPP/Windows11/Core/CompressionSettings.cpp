// CompressionSettings.cpp

#include "StdAfx.h"

#include "CompressionSettings.h"

CCompressionSettings::CCompressionSettings():
    Format("7z"),
    Level(NCompressionLevel::kNormal),
    DictionarySize_Defined(false),
    WordSize_Defined(false),
    SolidBlockSize_Defined(false),
    Threads_Defined(false),
    MemoryLimitPercent_Defined(false),
    VolumeSize_Defined(false),
    EncryptFileNames(false),
    StoreCreationTime(false),
    StoreAccessTime(false),
    FollowLinks(false),
    DictionarySize(0),
    WordSize(0),
    SolidBlockSize(0),
    Threads(0),
    MemoryLimitPercent(0),
    VolumeSize(0),
    UpdateMode(NUpdateMode::kAddAndReplace),
    PathMode(NCompressPathMode::kRelative)
{
}

CCompressionSettings CCompressionSettings::QuickZip()
{
  CCompressionSettings s;
  s.Format = "zip";
  s.Level = NCompressionLevel::kNormal;
  return s;
}

CCompressionSettings CCompressionSettings::Quick7z()
{
  CCompressionSettings s;
  s.Format = "7z";
  s.Level = NCompressionLevel::kNormal;
  return s;
}

bool CFormatCapabilities::IsTarballOrSingle() const
{
  return SingleFile || IsString1PrefixedByString2(Id, L"tar.");
}

using namespace NCompressionLevel;

static const NCompressionLevel::EEnum kAllLevels[] = { kStore, kFastest, kFast, kNormal, kMaximum, kUltra };
static const NCompressionLevel::EEnum kNoStore[] = { kFastest, kFast, kNormal, kMaximum, kUltra };
static const NCompressionLevel::EEnum kGzipLevels[] = { kFastest, kNormal, kMaximum, kUltra };
static const NCompressionLevel::EEnum kStoreOnly[] = { kStore };

static const CMethodOption k7zMethods[] =
{
  { L"LZMA2", L"LZMA2" }, { L"LZMA", L"LZMA" }, { L"PPMd", L"PPMd" }, { L"BZip2", L"BZip2" },
  { L"Deflate", L"Deflate" }, { L"Deflate64", L"Deflate64" }, { L"Copy", L"Store (no compression)" }
};
static const CMethodOption kZipMethods[] =
{
  { L"Deflate", L"Deflate" }, { L"Deflate64", L"Deflate64" }, { L"BZip2", L"BZip2" },
  { L"LZMA", L"LZMA" }, { L"PPMd", L"PPMd" }
};
static const CMethodOption kDeflateMethods[] = { { L"Deflate", L"Deflate" } };
static const CMethodOption kLzma2Methods[] = { { L"LZMA2", L"LZMA2" } };
static const CMethodOption kBZip2Methods[] = { { L"BZip2", L"BZip2" } };
static const CMethodOption kTarMethods[] = { { L"GNU", L"GNU" }, { L"POSIX", L"POSIX" } };

#define LEVELS(a) a, Z7_ARRAY_SIZE(a)
#define METHODS(a) a, Z7_ARRAY_SIZE(a)

static const CFormatCapabilities kFormats[] =
{
  { L"7z", L"7z", L"7z", LEVELS(kAllLevels), METHODS(k7zMethods),
    true, true, true, true, false,
    L"Best compression. AES-256 encryption with optional encrypted file names." },
  { L"zip", L"ZIP", L"zip", LEVELS(kAllLevels), METHODS(kZipMethods),
    false, true, true, false, false,
    L"Opens everywhere, including Windows' built-in compressed folders." },
  { L"tar.gz", L"TAR.GZ", L"tar.gz", LEVELS(kGzipLevels), METHODS(kDeflateMethods),
    false, false, false, false, false,
    L"Tarball compressed with gzip. Common on Linux and macOS." },
  { L"tar.xz", L"TAR.XZ", L"tar.xz", LEVELS(kNoStore), METHODS(kLzma2Methods),
    true, true, false, false, false,
    L"Tarball compressed with xz (LZMA2). Small and widely supported on Linux." },
  { L"tar.bz2", L"TAR.BZ2", L"tar.bz2", LEVELS(kNoStore), METHODS(kBZip2Methods),
    false, true, false, false, false,
    L"Tarball compressed with bzip2." },
  { L"tar", L"TAR", L"tar", LEVELS(kStoreOnly), METHODS(kTarMethods),
    false, false, false, false, false,
    L"Uncompressed tape archive; keeps POSIX metadata." },
  { L"wim", L"WIM", L"wim", LEVELS(kStoreOnly), NULL, 0,
    false, false, false, false, false,
    L"Windows Imaging Format." },
  { L"xz", L"XZ", L"xz", LEVELS(kNoStore), METHODS(kLzma2Methods),
    true, true, false, false, true,
    L"Compresses a single file with LZMA2." },
  { L"gzip", L"GZIP", L"gz", LEVELS(kGzipLevels), METHODS(kDeflateMethods),
    false, false, false, false, true,
    L"Compresses a single file with Deflate." },
  { L"bzip2", L"BZIP2", L"bz2", LEVELS(kNoStore), METHODS(kBZip2Methods),
    false, true, false, false, true,
    L"Compresses a single file with BZip2." }
};

namespace NFormatCapabilities {

unsigned GetCount()
{
  return Z7_ARRAY_SIZE(kFormats);
}

const CFormatCapabilities &GetByIndex(unsigned index)
{
  return kFormats[index];
}

const CFormatCapabilities &Get(const UString &id)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kFormats); i++)
    if (id.IsEqualTo_NoCase(kFormats[i].Id))
      return kFormats[i];
  return kFormats[0];
}

static bool EndsWith(const UString &s, const wchar_t *suffix)
{
  const unsigned len = MyStringLen(suffix);
  return s.Len() >= len && wcscmp(s.RightPtr(len), suffix) == 0;
}

const CFormatCapabilities *FromFileName(const UString &fileName)
{
  UString lower = fileName;
  lower.MakeLower_Ascii();
  // longest extension first, so "tar.gz" wins over "gz"
  const CFormatCapabilities *best = NULL;
  unsigned bestLen = 0;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kFormats); i++)
  {
    UString suffix (L".");
    suffix += kFormats[i].Extension;
    if (EndsWith(lower, suffix) && suffix.Len() > bestLen)
    {
      best = &kFormats[i];
      bestLen = suffix.Len();
    }
  }
  if (best)
    return best;
  if (EndsWith(lower, L".tgz")) return &Get(UString("tar.gz"));
  if (EndsWith(lower, L".txz")) return &Get(UString("tar.xz"));
  if (EndsWith(lower, L".tbz2") || EndsWith(lower, L".tbz")) return &Get(UString("tar.bz2"));
  return NULL;
}

static const UInt64 kKB = 1024;
static const UInt64 kMB = 1024 * kKB;

void GetDictionarySizes(const UString &method, CRecordVector<UInt64> &sizes)
{
  sizes.Clear();
  if (method.IsEmpty() || method.IsEqualTo_Ascii_NoCase("LZMA") || method.IsEqualTo_Ascii_NoCase("LZMA2"))
  {
    static const UInt64 k[] = { 64 * kKB, 256 * kKB, 1 * kMB, 2 * kMB, 4 * kMB, 8 * kMB, 16 * kMB, 32 * kMB,
        64 * kMB, 128 * kMB, 256 * kMB, 512 * kMB, 1024 * kMB, 1536 * kMB };
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(k); i++) sizes.Add(k[i]);
  }
  else if (method.IsEqualTo_Ascii_NoCase("PPMD"))
  {
    static const UInt64 k[] = { 1 * kMB, 2 * kMB, 4 * kMB, 8 * kMB, 16 * kMB, 32 * kMB, 64 * kMB,
        128 * kMB, 256 * kMB, 512 * kMB, 1024 * kMB };
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(k); i++) sizes.Add(k[i]);
  }
  else if (method.IsEqualTo_Ascii_NoCase("BZIP2"))
  {
    for (unsigned i = 1; i <= 9; i++)
      sizes.Add(i * 100 * kKB);
  }
  else if (method.IsEqualTo_Ascii_NoCase("DEFLATE"))
    sizes.Add(32 * kKB);
  else if (method.IsEqualTo_Ascii_NoCase("DEFLATE64"))
    sizes.Add(64 * kKB);
}

UInt64 GetDefaultDictionary(const UString &method, NCompressionLevel::EEnum level)
{
  if (method.IsEqualTo_Ascii_NoCase("PPMD"))
    return level >= kUltra ? 192 * kMB : level >= kMaximum ? 64 * kMB : level >= kNormal ? 16 * kMB : 4 * kMB;
  if (method.IsEqualTo_Ascii_NoCase("BZIP2"))
    return level >= kFast ? 900 * kKB : 100 * kKB;
  if (method.IsEqualTo_Ascii_NoCase("DEFLATE"))
    return 32 * kKB;
  if (method.IsEqualTo_Ascii_NoCase("DEFLATE64"))
    return 64 * kKB;
  switch (level)
  {
    case kStore: return 0;
    case kFastest: return 256 * kKB;
    case kFast: return 4 * kMB;
    case kNormal: return 16 * kMB;
    case kMaximum:
    case kUltra:
    default: return 64 * kMB;
  }
}

void EstimateMemory(const UString &method, UInt64 dictionary, UInt32 threads, UInt64 &compress, UInt64 &decompress)
{
  if (threads < 1)
    threads = 1;
  if (method.IsEqualTo_Ascii_NoCase("PPMD"))
  {
    compress = decompress = dictionary + 2 * kMB;
    return;
  }
  if (method.IsEqualTo_Ascii_NoCase("BZIP2"))
  {
    compress = (UInt64)threads * 10 * kMB;
    decompress = 7 * kMB;
    return;
  }
  if (method.IsEqualTo_Ascii_NoCase("DEFLATE") || method.IsEqualTo_Ascii_NoCase("DEFLATE64"))
  {
    compress = (UInt64)threads * 4 * kMB;
    decompress = 2 * kMB;
    return;
  }
  if (method.IsEqualTo_Ascii_NoCase("COPY"))
  {
    compress = decompress = kMB;
    return;
  }
  // LZMA2: bt4 match finder ~ 11.5 x dict per encoder, two threads per encoder.
  const UInt64 perEncoder = dictionary * 23 / 2 + 6 * kMB;
  UInt64 encoders = threads / 2;
  if (encoders < 1)
    encoders = 1;
  if (method.IsEqualTo_Ascii_NoCase("LZMA"))
    encoders = 1;
  compress = perEncoder * encoders + 2 * kMB;
  decompress = dictionary + 2 * kMB;
}

}
