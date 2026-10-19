// HashEngine.cpp

#include "StdAfx.h"

#include "HashEngine.h"

#include "../../Common/IntToString.h"
#include "../../Common/MyBuffer.h"
#include "../../Common/MyCom.h"

#include "../../Windows/ErrorMsg.h"
#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/PropVariant.h"
#include "../../Windows/Synchronization.h"

#include "../../7zip/ICoder.h"

#include "PathSanitizer.h"
#include "PropertyFormatter.h"
#include "SevenZipEngine.h"
#include "SharedInFileStream.h"

using namespace NWindows;
using namespace NFile;

const CHashDigest *CFileHashResult::FindDigest(const UString &algorithm) const
{
  FOR_VECTOR (i, Digests)
    if (Digests[i].Algorithm.IsEqualTo_NoCase(algorithm))
      return &Digests[i];
  return NULL;
}

namespace NHashEngine {

static NSynchronization::CCriticalSection g_CS;
static CObjectVector<CHashAlgorithmInfo> g_Algorithms;
static bool g_Loaded = false;

static const char * const kPreferred[] =
  { "CRC32", "CRC64", "SHA256", "SHA1", "SHA512", "SHA384", "SHA3-256", "BLAKE2sp", "XXH64", "MD5" };

static unsigned GetRank(const UString &name)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kPreferred); i++)
    if (name.IsEqualTo_Ascii_NoCase(kPreferred[i]))
      return i;
  return 100;
}

static int CompareAlgorithms(void *const *a, void *const *b, void * /* param */)
{
  const CHashAlgorithmInfo &x = *(const CHashAlgorithmInfo *)*a;
  const CHashAlgorithmInfo &y = *(const CHashAlgorithmInfo *)*b;
  const unsigned rx = GetRank(x.Name), ry = GetRank(y.Name);
  if (rx != ry)
    return rx < ry ? -1 : 1;
  return MyStringCompareNoCase(x.Name, y.Name);
}

const CObjectVector<CHashAlgorithmInfo> &GetAlgorithms()
{
  NSynchronization::CCriticalSectionLock lock(g_CS);
  if (g_Loaded)
    return g_Algorithms;
  CMyComPtr<IHashers> hashers;
  if (NSevenZipEngine::GetHashers(hashers) != S_OK)
    return g_Algorithms;
  const UInt32 n = hashers->GetNumHashers();
  for (UInt32 i = 0; i < n; i++)
  {
    UString name;
    UInt32 size = 0;
    {
      NCOM::CPropVariant prop;
      if (hashers->GetHasherProp(i, NMethodPropID::kName, &prop) == S_OK)
        PropToString(prop, name);
    }
    {
      NCOM::CPropVariant prop;
      if (hashers->GetHasherProp(i, NMethodPropID::kDigestSize, &prop) == S_OK)
        PropToUInt32(prop, size);
    }
    if (name.IsEmpty() || size == 0)
      continue;
    CHashAlgorithmInfo &a = g_Algorithms.AddNew();
    a.Index = i;
    a.Name = name;
    a.DigestSize = size;
  }
  g_Algorithms.Sort(CompareAlgorithms, NULL);
  g_Loaded = true;
  return g_Algorithms;
}

const CHashAlgorithmInfo *Find(const UString &name)
{
  const CObjectVector<CHashAlgorithmInfo> &all = GetAlgorithms();
  UString name2 = name;
  name2.RemoveChar(L'-');
  FOR_VECTOR (i, all)
  {
    if (all[i].Name.IsEqualTo_NoCase(name))
      return &all[i];
    UString s = all[i].Name;
    s.RemoveChar(L'-');
    if (s.IsEqualTo_NoCase(name2))
      return &all[i];
  }
  return NULL;
}

static UString FinalHex(IHasher *hasher, UInt32 digestSize)
{
  Byte digest[256];
  memset(digest, 0, sizeof(digest));
  if (digestSize > sizeof(digest))
    digestSize = sizeof(digest);
  hasher->Final(digest);
  // CRC/XXH style digests (<= 8 bytes) are printed as little-endian numbers, like 7-Zip does.
  if (digestSize <= 8)
    for (UInt32 i = 0; i < digestSize / 2; i++)
    {
      const Byte t = digest[i];
      digest[i] = digest[digestSize - 1 - i];
      digest[digestSize - 1 - i] = t;
    }
  char s[sizeof(digest) * 2 + 1];
  ConvertDataToHex_Upper(s, digest, digestSize);
  return UString(s);
}

struct CFileToHash
{
  FString Full;
  UString Relative;
};

static int CompareFilesToHash(void *const *a, void *const *b, void * /* param */)
{
  return MyStringCompareNoCase(((const CFileToHash *)*a)->Relative, ((const CFileToHash *)*b)->Relative);
}

static void EnumerateFolder(const FString &dir, const UString &relative, CObjectVector<CFileToHash> &files)
{
  NFind::CEnumerator enumerator;
  enumerator.SetDirPrefix(dir);
  NFind::CFileInfo fi;
  bool found;
  // inaccessible folders are ignored
  while (enumerator.Next(fi, found) && found)
  {
    const FString full = dir + fi.Name;
    UString rel = relative;
    rel += fs2us(fi.Name);
    if (fi.IsDir())
    {
      // don't follow folder links: they can form cycles
      if (!fi.HasReparsePoint())
      {
        FString sub = full;
        sub.Add_PathSepar();
        rel += L'\\';
        EnumerateFolder(sub, rel, files);
      }
    }
    else
    {
      CFileToHash &f = files.AddNew();
      f.Full = full;
      f.Relative = rel;
    }
  }
}

struct CHasherItem
{
  const CHashAlgorithmInfo *Info;
  CMyComPtr<IHasher> Hasher;
};

HRESULT ComputeFiles(const FStringVector &paths, const CObjectVector<CHashAlgorithmInfo> &algorithms,
    COperationContext &ctx, CObjectVector<CFileHashResult> &results, IHashFileCallback *onFile, UString &errorMessage)
{
  errorMessage.Empty();
  results.Clear();
  CObjectVector<CFileToHash> files;
  FOR_VECTOR (i, paths)
  {
    FString full;
    if (!NDir::MyGetFullPathName(paths[i], full))
      full = paths[i];
    NFind::CFileInfo fi;
    if (!fi.Find(full))
      continue;
    if (!fi.IsDir())
    {
      CFileToHash &f = files.AddNew();
      f.Full = full;
      f.Relative = GetFileNamePart(fs2us(full));
    }
    else
    {
      while (full.Len() > 3 && IS_PATH_SEPAR(full.Back()))
        full.DeleteBack();
      UString rel = GetFileNamePart(fs2us(full));
      if (!rel.IsEmpty())
        rel += L'\\';
      FString dir = full;
      if (!IS_PATH_SEPAR(dir.Back()))
        dir.Add_PathSepar();
      EnumerateFolder(dir, rel, files);
    }
  }
  files.Sort(CompareFilesToHash, NULL);

  ctx.TotalFiles = files.Size();
  ctx.TotalBytes = 0;
  FOR_VECTOR (i, files)
  {
    NFind::CFileInfo fi;
    if (fi.Find(files[i].Full))
      ctx.TotalBytes += fi.Size;
  }

  CMyComPtr<IHashers> hashers;
  {
    const HRESULT res = NSevenZipEngine::GetHashers(hashers);
    if (res != S_OK)
    {
      FormatHResultMessage(res, UString("Loading the hashers"), errorMessage);
      return res;
    }
  }
  CObjectVector<CHasherItem> instances;
  FOR_VECTOR (i, algorithms)
  {
    CMyComPtr<IHasher> h;
    if (hashers->CreateHasher(algorithms[i].Index, &h) == S_OK && h)
    {
      CHasherItem &item = instances.AddNew();
      item.Info = &algorithms[i];
      item.Hasher = h;
    }
  }

  const size_t kBufSize = (size_t)1 << 20;
  CByteBuffer buffer(kBufSize);

  FOR_VECTOR (fileIndex, files)
  {
    RINOK(ctx.Checkpoint())
    const CFileToHash &file = files[fileIndex];
    ctx.SetCurrentItem(file.Relative);
    CFileHashResult &result = results.AddNew();
    result.Path = file.Full;
    result.RelativePath = file.Relative;

    NIO::CInFile in;
    bool ok = NSharedFile::OpenRead(in, file.Full);
    DWORD error = ok ? 0 : ::GetLastError();
    if (ok)
    {
      FOR_VECTOR (k, instances)
        instances[k].Hasher->Init();
      UInt64 size = 0;
      for (;;)
      {
        UInt32 read = 0;
        if (!in.Read(buffer, (UInt32)kBufSize, read))
        {
          ok = false;
          error = ::GetLastError();
          break;
        }
        if (read == 0)
          break;
        RINOK(ctx.Checkpoint())
        FOR_VECTOR (k, instances)
          instances[k].Hasher->Update(buffer, read);
        size += read;
        ctx.CompletedBytes += read;
        ctx.Report();
      }
      if (ok)
      {
        result.Size = size;
        FOR_VECTOR (k, instances)
        {
          CHashDigest &d = result.Digests.AddNew();
          d.Algorithm = instances[k].Info->Name;
          d.Hex = FinalHex(instances[k].Hasher, instances[k].Info->DigestSize);
        }
      }
    }
    if (!ok)
    {
      result.Size = 0;
      result.Digests.Clear();
      result.Error = NError::MyFormatMessage(error);
      result.Error.Trim();
      ctx.AddMessage(NMessageSeverity::kError, result.Error, fs2us(file.Full));
    }
    ctx.CompletedFiles++;
    if (onFile)
      onFile->HashFile_Done(result);
  }
  ctx.Report(true);
  return S_OK;
}

HRESULT ComputeBytes(const void *data, size_t size, const CHashAlgorithmInfo &algorithm, UString &hex)
{
  hex.Empty();
  CMyComPtr<IHashers> hashers;
  RINOK(NSevenZipEngine::GetHashers(hashers))
  CMyComPtr<IHasher> h;
  RINOK(hashers->CreateHasher(algorithm.Index, &h))
  if (!h)
    return E_NOTIMPL;
  h->Init();
  const Byte *p = (const Byte *)data;
  while (size != 0)
  {
    const UInt32 cur = size > ((UInt32)1 << 30) ? ((UInt32)1 << 30) : (UInt32)size;
    h->Update(p, cur);
    p += cur;
    size -= cur;
  }
  hex = FinalHex(h, algorithm.DigestSize);
  return S_OK;
}

UString FormatChecksumFile(const CObjectVector<CFileHashResult> &results, const UString &algorithm)
{
  UString s;
  FOR_VECTOR (i, results)
  {
    const CHashDigest *d = results[i].FindDigest(algorithm);
    if (!d)
      continue;
    UString hex = d->Hex;
    hex.MakeLower_Ascii();
    s += hex;
    s += " *";
    UString rel = results[i].RelativePath;
    rel.Replace(L'\\', L'/');
    s += rel;
    s.Add_LF();
  }
  return s;
}

}
