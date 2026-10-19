// CompressionSettings.h

#ifndef ZIP7_INC_COMPRESSION_SETTINGS_H
#define ZIP7_INC_COMPRESSION_SETTINGS_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

namespace NCompressionLevel
{
  enum EEnum
  {
    kStore = 0,
    kFastest = 1,
    kFast = 3,
    kNormal = 5,
    kMaximum = 7,
    kUltra = 9
  };
}

namespace NUpdateMode
{
  enum EEnum
  {
    kAddAndReplace, // add new files, replace existing ones with the same name
    kUpdateAndAdd,  // add new files, replace existing ones only when the file on disk is newer
    kFreshen,       // only refresh files that already exist in the archive
    kSynchronize    // make the archive mirror the source (removes items that no longer exist on disk)
  };
}

namespace NCompressPathMode
{
  enum EEnum
  {
    kRelative, // paths relative to the folder that contains each selected item
    kFull      // full paths without the drive letter
  };
}

// Everything the "Create archive" page can configure. Undefined values mean "engine default".
struct CCompressionSettings
{
  // Engine format name ("7z", "zip", "tar", "gzip", "bzip2", "xz", "wim")
  // or a tarball combo ("tar.gz", "tar.xz", "tar.bz2").
  UString Format;
  NCompressionLevel::EEnum Level;
  UString Method; // "" = format default
  bool DictionarySize_Defined;
  bool WordSize_Defined;
  bool SolidBlockSize_Defined;
  bool Threads_Defined;
  bool MemoryLimitPercent_Defined;
  bool VolumeSize_Defined;
  bool EncryptFileNames;
  bool StoreCreationTime;
  bool StoreAccessTime;
  bool FollowLinks; // follow directory symbolic links/junctions instead of storing them as empty folders
  UInt64 DictionarySize;
  UInt32 WordSize;
  UInt64 SolidBlockSize;  // 0 = non-solid, (UInt64)(Int64)-1 = fully solid, otherwise block size in bytes
  UInt32 Threads;
  UInt32 MemoryLimitPercent; // limit engine memory to a percentage of RAM
  UInt64 VolumeSize;  // split into volumes of this many bytes
  UString Password;   // "" = no password
  UString ZipEncryption; // "AES256" (default when "") or "ZipCrypto" for zip
  NUpdateMode::EEnum UpdateMode;
  NCompressPathMode::EEnum PathMode;
  UString ExtraParameters; // free-form engine switches, e.g. "qs=on f=off" (the "Parameters" box)

  CCompressionSettings();
  bool IsTarball() const { return Format.IsPrefixedBy_Ascii_NoCase("tar."); }

  static CCompressionSettings QuickZip();
  static CCompressionSettings Quick7z();
};


struct CMethodOption
{
  const wchar_t *Name;
  const wchar_t *DisplayName;
};

// What each creatable format supports (mirrors the classic CompressDialog g_Formats table).
struct CFormatCapabilities
{
  const wchar_t *Id;
  const wchar_t *DisplayName;
  const wchar_t *Extension;
  const NCompressionLevel::EEnum *Levels;
  unsigned NumLevels;
  const CMethodOption *Methods;
  unsigned NumMethods;
  bool Solid;
  bool MultiThread;
  bool Encryption;
  bool EncryptFileNames;
  bool SingleFile;
  const wchar_t *Description;

  NCompressionLevel::EEnum GetMaxLevel() const { return Levels[NumLevels - 1]; }
  bool IsTarballOrSingle() const;
};

namespace NFormatCapabilities {

unsigned GetCount();
const CFormatCapabilities &GetByIndex(unsigned index);
// The format with this id (case-insensitive), or the first one (7z).
const CFormatCapabilities &Get(const UString &id);
// By the archive file name ("x.tar.gz", "x.tgz"...); NULL if unknown.
const CFormatCapabilities *FromFileName(const UString &fileName);

// Dictionary choices offered for a method (bytes). (method) "" = LZMA2.
void GetDictionarySizes(const UString &method, CRecordVector<UInt64> &sizes);
// Default dictionary per level, matching the engine's LZMA2 defaults.
UInt64 GetDefaultDictionary(const UString &method, NCompressionLevel::EEnum level);
// Rough memory estimate (bytes) for compressing and decompressing, mirroring 7-Zip's dialog hints.
void EstimateMemory(const UString &method, UInt64 dictionary, UInt32 threads, UInt64 &compress, UInt64 &decompress);

}

#endif
