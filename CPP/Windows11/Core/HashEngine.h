// HashEngine.h

#ifndef ZIP7_INC_HASH_ENGINE_H
#define ZIP7_INC_HASH_ENGINE_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../7zip/IDecl.h"

#include "OperationContext.h"

struct CHashAlgorithmInfo
{
  UInt32 Index;      // hasher index in the engine's IHashers
  UString Name;      // "CRC32", "SHA256", "BLAKE2sp", ...
  UInt32 DigestSize; // bytes
};

struct CHashDigest
{
  UString Algorithm;
  UString Hex; // upper case, 7-Zip's byte order
};

struct CFileHashResult
{
  FString Path;
  UString RelativePath;
  UInt64 Size;
  CObjectVector<CHashDigest> Digests;
  UString Error; // "" if the file was read

  CFileHashResult(): Size(0) {}
  const CHashDigest *FindDigest(const UString &algorithm) const;
};

Z7_PURE_INTERFACES_BEGIN

// Called on the worker thread after each file.
#define Z7_IFACEN_IHashFileCallback(x) \
  virtual void HashFile_Done(const CFileHashResult &result) x \

Z7_IFACE_DECL_PURE(IHashFileCallback)

Z7_PURE_INTERFACES_END

// Checksums through the engine's own hashers (CRC32, CRC64, SHA-1/256/384/512, SHA3, BLAKE2sp, XXH64, MD5...).
namespace NHashEngine {

// Ordered as shown in the UI (CRC32, CRC64, SHA256, SHA1, SHA512, SHA384, SHA3-256, BLAKE2sp, XXH64, MD5),
// then anything else the engine offers by name. Empty if the engine is missing.
const CObjectVector<CHashAlgorithmInfo> &GetAlgorithms();
// Exact name, or the name without '-' ("SHA3256" finds "SHA3-256"). NULL if missing.
const CHashAlgorithmInfo *Find(const UString &name);

/* Expands folders and hashes every file. Unreadable files get CFileHashResult::Error
   and an error message in (ctx). Returns E_ABORT if cancelled. */
HRESULT ComputeFiles(const FStringVector &paths, const CObjectVector<CHashAlgorithmInfo> &algorithms,
    COperationContext &ctx, CObjectVector<CFileHashResult> &results, IHashFileCallback *onFile, UString &errorMessage);

HRESULT ComputeBytes(const void *data, size_t size, const CHashAlgorithmInfo &algorithm, UString &hex);

// A checksum file in the common "hash *name" format (sha256sum compatible); '\n' line ends.
UString FormatChecksumFile(const CObjectVector<CFileHashResult> &results, const UString &algorithm);

}

#endif
