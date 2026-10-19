// SevenZipTest.cpp
// Round-trips real archives through 7z.dll (was SevenZip.Core.Tests, xUnit).
// Each test prints its name and PASS/FAIL; the exit code is the number of failures.

#include "StdAfx.h"

#include <stdio.h>

#include "../../Common/MyWindows.h"
#include "../../Common/MyInitGuid.h"

#include "../../Common/IntToString.h"
#include "../../Common/StringConvert.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"
#include "../../Windows/FileName.h"

#include "../Core/ArchiveSession.h"
#include "../Core/ArchiveWriter.h"
#include "../Core/BenchmarkRunner.h"
#include "../Core/ExtractEngine.h"
#include "../Core/HashEngine.h"
#include "../Core/PathSanitizer.h"
#include "../Core/PropertyFormatter.h"
#include "../Core/SevenZipEngine.h"

#ifdef _WIN32
extern
HINSTANCE g_hInstance;
HINSTANCE g_hInstance = NULL;
#endif

using namespace NWindows;
using namespace NFile;

// ---------- tiny runner ----------

static bool g_TestFailed;
static AString g_FailText;

static void Fail(const char *expr, int line)
{
  if (g_TestFailed)
    return;
  g_TestFailed = true;
  g_FailText = "line ";
  g_FailText.Add_UInt32((UInt32)line);
  g_FailText += ": ";
  g_FailText += expr;
}

#define CHECK(x) { if (!(x)) { Fail(#x, __LINE__); return; } }
#define CHECK_HR(x) { const HRESULT hr_ = (x); if (hr_ != S_OK) { \
    AString s_ (#x " -> 0x"); char b_[16]; ConvertUInt32ToHex8Digits((UInt32)hr_, b_); s_ += b_; \
    s_ += " "; s_ += UnicodeStringToMultiByte(errorMessage, CP_OEMCP); \
    Fail(s_, __LINE__); return; } }

static void Print(const UString &s)
{
  fputs(UnicodeStringToMultiByte(s, CP_OEMCP), stdout);
}

// ---------- helpers ----------

class CTempDir
{
  FString _path;
  Z7_CLASS_NO_COPY(CTempDir)
public:
  CTempDir()
  {
    NDir::MyGetTempPath(_path);
    NName::NormalizeDirPathPrefix(_path);
    _path += FTEXT("7zw-tests\\");
    GUID guid;
    ::CoCreateGuid(&guid);
    char s[64];
    RawLeGuidToString((const Byte *)&guid, s);
    _path += us2fs(UString(s));
    NDir::CreateComplexDir(_path);
  }
  ~CTempDir() { NDir::RemoveDirWithSubItems(_path); }
  const FString &Path() const { return _path; }
  FString operator[](const char *relative) const
  {
    FString s = _path;
    s.Add_PathSepar();
    s += us2fs(UString(relative));
    return s;
  }
};

static FString Combine(const FString &a, const char *b)
{
  FString s = a;
  if (!IS_PATH_SEPAR(s.Back()))
    s.Add_PathSepar();
  s += us2fs(UString(b));
  return s;
}

static bool WriteBytes(const FString &path, const void *data, size_t size)
{
  NIO::COutFile f;
  return f.Create_ALWAYS(path) && f.WriteFull(data, size);
}

static bool WriteText(const FString &path, const AString &s)
{
  return WriteBytes(path, s.Ptr(), s.Len());
}

static bool ReadAll(const FString &path, CByteBuffer &buf)
{
  buf.Free();
  NIO::CInFile f;
  UInt64 len;
  if (!f.Open(path) || !f.GetLength(len))
    return false;
  buf.Alloc((size_t)len);
  size_t processed;
  return f.ReadFull(buf, (size_t)len, processed) && processed == len;
}

static bool SameContent(const FString &a, const FString &b)
{
  CByteBuffer x, y;
  return ReadAll(a, x) && ReadAll(b, y) && x == y;
}

static bool FileExists(const FString &path) { return NFind::DoesFileExist_Raw(path); }
static bool DirExists(const FString &path) { return NFind::DoesDirExist(path); }

class CScriptedPrompts Z7_final: public IOperationPrompts
{
  bool _password_Defined;
  UString _password;
  NOverwriteChoice::EEnum _overwrite;
public:
  LONG PasswordRequests;
  LONG OverwriteRequests;

  CScriptedPrompts(const char *password, NOverwriteChoice::EEnum overwrite = NOverwriteChoice::kReplace):
      _password_Defined(password != NULL), _overwrite(overwrite), PasswordRequests(0), OverwriteRequests(0)
  {
    if (password)
      _password = password;
  }

  Z7_IFACE_IMP(IOperationPrompts)
};

HRESULT CScriptedPrompts::RequestPassword(const UString & /* archiveName */, bool /* retry */, UString &password)
{
  ::InterlockedIncrement(&PasswordRequests);
  if (!_password_Defined)
    return E_ABORT;
  password = _password;
  return S_OK;
}

HRESULT CScriptedPrompts::ResolveOverwrite(const COverwriteRequest & /* request */, NOverwriteChoice::EEnum &choice)
{
  ::InterlockedIncrement(&OverwriteRequests);
  choice = _overwrite;
  return S_OK;
}

static FString MakeSourceTree(const CTempDir &t)
{
  const FString src = t["src"];
  NDir::CreateComplexDir(Combine(Combine(src, "docs"), "empty"));
  WriteText(Combine(src, "readme.txt"), AString("Hello from 7-Zip on Windows 11!"));
  AString notes;
  for (unsigned i = 0; i < 5000; i++)
    notes += "compressible line\n";
  WriteText(Combine(Combine(src, "docs"), "notes.md"), notes);
  CByteBuffer rnd(300000);
  UInt32 x = 1;
  for (size_t i = 0; i < rnd.Size(); i++)
  {
    x = x * 1103515245 + 12345;
    rnd[i] = (Byte)(x >> 16);
  }
  WriteBytes(Combine(Combine(src, "docs"), "random.bin"), rnd, rnd.Size());
  return src;
}

static bool HasEntryNamed(const CArchiveSession &s, const char *name)
{
  FOR_VECTOR (i, s.Entries)
    if (s.Entries[i].GetName().IsEqualTo(name))
      return true;
  return false;
}

static bool HasEntryPath(const CArchiveSession &s, const wchar_t *path)
{
  FOR_VECTOR (i, s.Entries)
    if (s.Entries[i].Path == path)
      return true;
  return false;
}

static FStringVector OneSource(const FString &path)
{
  FStringVector v;
  v.Add(path);
  return v;
}

// ---------- tests ----------

static void EngineLoadsAndListsFormats()
{
  const CObjectVector<CArchiveFormat> &formats = NFormatRegistry::All();
  bool has7z = false, hasZip = false, hasRar5 = false;
  FOR_VECTOR (i, formats)
  {
    const CArchiveFormat &f = formats[i];
    if (f.Name.IsEqualTo("7z") && f.CanUpdate) has7z = true;
    if (f.Name.IsEqualTo_Ascii_NoCase("zip") && f.CanUpdate) hasZip = true;
    if (f.Name.IsEqualTo_Ascii_NoCase("Rar5")) hasRar5 = true;
  }
  CHECK(has7z)
  CHECK(hasZip)
  CHECK(hasRar5)
  CHECK(formats.Size() > 40)
  UInt32 major, minor;
  CHECK(NSevenZipEngine::GetEngineVersion(major, minor))
}

static const char *g_Format;

static void RoundTripsAFolder()
{
  const UString format (g_Format);
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const CFormatCapabilities &caps = NFormatCapabilities::Get(format);
  FString archive = t.Path();
  archive += FTEXT("\\out.");
  archive += caps.Extension;

  UString errorMessage;
  CCompressionSettings settings;
  settings.Format = format;
  settings.Level = caps.GetMaxLevel();
  CCompressResult result;
  {
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, result, errorMessage))
  }
  CHECK(FileExists(archive))
  CHECK(result.Files == 3)

  CArchiveSession session;
  {
    COperationContext ctx;
    CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  }
  CArchiveSession inner;
  CArchiveSession *target = &session;
  if (format.IsPrefixedBy("tar."))
  {
    // the C# test's OpenInnerTar
    CHECK(session.Entries.Size() == 1)
    const FString tmp = t["inner"];
    CExtractOptions options;
    options.Destination = tmp;
    CExtractResult r;
    COperationContext ctx;
    CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, r, errorMessage))
    NFind::CEnumerator e;
    e.SetDirPrefix(tmp + FTEXT("\\"));
    NFind::CFileInfo fi;
    bool found;
    FStringVector names;
    while (e.Next(fi, found) && found)
      names.Add(fi.Name);
    CHECK(names.Size() == 1)
    CHECK(names[0].Len() > 4 && StringsAreEqualNoCase_Ascii(names[0].RightPtr(4), ".tar"))
    CArchiveOpenOptions o;
    o.Parent = &session;
    COperationContext ctx2;
    CHECK_HR(inner.Open(Combine(tmp, "") + names[0], o, ctx2, errorMessage))
    target = &inner;
  }

  const CArchiveFolder *srcFolder = target->Root.FindFolder(UString("src"));
  CHECK(srcFolder != NULL)
  bool hasReadme = false;
  FOR_VECTOR (i, srcFolder->Files)
    if (srcFolder->Files[i]->GetName().IsEqualTo("readme.txt"))
      hasReadme = true;
  CHECK(hasReadme)
  CHECK(srcFolder->Navigate(UString("docs\\empty")) != NULL)

  const FString dest = t["dest"];
  CExtractOptions options;
  options.Destination = dest;
  CExtractResult extract;
  COperationContext ctx;
  CHECK_HR(NExtractEngine::Extract(*target, NULL, options, ctx, extract, errorMessage))
  CHECK(extract.Errors == 0)
  const FString srcDocs = Combine(src, "docs");
  const FString destDocs = Combine(Combine(dest, "src"), "docs");
  CHECK(SameContent(Combine(srcDocs, "random.bin"), Combine(destDocs, "random.bin")))
  CHECK(SameContent(Combine(srcDocs, "notes.md"), Combine(destDocs, "notes.md")))
  CHECK(DirExists(Combine(destDocs, "empty")))
}

static void EncryptedHeadersRequirePassword()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["secret.7z"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    settings.Password = "p@ss";
    settings.EncryptFileNames = true;
    CCompressResult result;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, result, errorMessage))
  }
  {
    CScriptedPrompts prompts(NULL);
    COperationContext ctx(NULL, &prompts);
    CArchiveSession s;
    CHECK(s.Open(archive, CArchiveOpenOptions(), ctx, errorMessage) == E_ABORT)
  }
  {
    CScriptedPrompts prompts("nope");
    COperationContext ctx(NULL, &prompts);
    CArchiveSession s;
    CHECK(s.Open(archive, CArchiveOpenOptions(), ctx, errorMessage) == k_HRESULT_WrongPassword)
  }
  CScriptedPrompts prompts("p@ss");
  CArchiveSession session;
  {
    COperationContext ctx(NULL, &prompts);
    CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  }
  CHECK(session.HeadersEncrypted)
  CHECK(prompts.PasswordRequests == 1)

  CExtractOptions options;
  options.Destination = t["d"];
  CExtractResult result;
  COperationContext ctx(NULL, &prompts);
  CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, result, errorMessage))
  CHECK(result.Errors == 0)
  CHECK(prompts.PasswordRequests == 1) // password re-used from the session
}

static void WrongPasswordOnZipIsReported()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["secret.zip"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    settings.Format = "zip";
    settings.Password = "right";
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, r, errorMessage))
  }
  CArchiveSession session;
  {
    COperationContext ctx;
    CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  }
  CHECK(session.HasEncryptedItems)
  CExtractOptions options;
  options.Destination = t["d"];
  options.Password_Defined = true;
  options.Password = "wrong";
  CExtractResult result;
  COperationContext ctx;
  CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, result, errorMessage))
  CHECK(result.WrongPassword)
  CHECK(result.Errors > 0)
}

static void UpdateAddsRenamesAndDeletes()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["u.7z"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, r, errorMessage))
  }
  const FString extra = t["extra.txt"];
  CHECK(WriteText(extra, AString("added later")))

  {
    CArchiveSession session;
    {
      COperationContext ctx;
      CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
    }
    int readme = -1;
    FOR_VECTOR (i, session.Entries)
      if (session.Entries[i].GetName().IsEqualTo("readme.txt"))
        readme = (int)session.Entries[i].Index;
    CHECK(readme >= 0)
    CHECK(session.Root.Navigate(UString("src\\docs")) != NULL)
    CUpdatePlan plan;
    plan.Add.Add(extra);
    plan.TargetFolder = "src\\docs";
    plan.Delete.Add((UInt32)readme);
    CRenamePair &rename = plan.Rename.AddNew();
    rename.From = "src\\docs\\empty";
    rename.To = "src\\docs\\still-empty";
    plan.NewFolders.Add(UString("src\\new folder"));
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Update(session, plan, NULL, ctx, r, errorMessage))
  }

  CArchiveSession reopened;
  COperationContext ctx;
  CHECK_HR(reopened.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  CHECK(!HasEntryNamed(reopened, "readme.txt"))
  CHECK(HasEntryPath(reopened, L"src\\docs\\extra.txt"))
  CHECK(reopened.Root.Navigate(UString("src\\docs\\still-empty")) != NULL)
  CHECK(reopened.Root.Navigate(UString("src\\docs\\empty")) == NULL)
  CHECK(reopened.Root.Navigate(UString("src\\new folder")) != NULL)
  CHECK(HasEntryPath(reopened, L"src\\docs\\random.bin"))
}

static void SplitVolumesOpenAsOneArchive()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["vol.7z"];
  UString errorMessage;
  CCompressionSettings settings;
  settings.Level = NCompressionLevel::kStore;
  settings.VolumeSize_Defined = true;
  settings.VolumeSize = 100000;
  CCompressResult result;
  {
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, result, errorMessage))
  }
  CHECK(result.OutputFiles.Size() >= 3)
  CHECK(result.OutputFiles[0].Len() > 7
      && StringsAreEqualNoCase_Ascii(result.OutputFiles[0].RightPtr(7), ".7z.001"))

  CArchiveSession session;
  {
    COperationContext ctx;
    CHECK_HR(session.Open(result.OutputFiles[0], CArchiveOpenOptions(), ctx, errorMessage))
  }
  CHECK(session.Format->Name.IsEqualTo("7z"))
  CHECK(session.ContainerFormat.IsEqualTo("Split"))
  CExtractResult test;
  COperationContext ctx;
  CHECK_HR(NExtractEngine::Test(session, NULL, ctx, test, errorMessage))
  CHECK(test.Errors == 0)
  CHECK(test.FilesProcessed == 3)
}

static bool ComponentsAre(const char *path, const char *c1, const char *c2)
{
  UStringVector v;
  NPathSanitizer::GetSafeComponents(UString(path), v);
  if (v.Size() != (c2 ? 2u : 1u))
    return false;
  if (!v[0].IsEqualTo(c1))
    return false;
  return !c2 || v[1].IsEqualTo(c2);
}

static void ExtractionIsConfinedToDestination()
{
  CHECK(ComponentsAre("..\\..\\etc\\passwd", "etc", "passwd"))
  CHECK(ComponentsAre("C:\\Windows\\x", "Windows", "x"))
  CHECK(ComponentsAre("CON.txt", "_CON.txt", NULL))
  CHECK(ComponentsAre("a?b.", "a_b_", NULL))
  UStringVector c;
  FString result;
  c.Add(UString(".."));
  c.Add(UString("x"));
  CHECK(!NPathSanitizer::CombineSafe(FString(FTEXT("C:\\dest")), c, result))
  c.Clear();
  c.Add(UString("x"));
  CHECK(NPathSanitizer::CombineSafe(FString(FTEXT("C:\\dest")), c, result))
  CHECK(result == FTEXT("C:\\dest\\x"))
}

static void OverwritePromptsAndKeepBoth()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["o.zip"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    settings.Format = "zip";
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(Combine(src, "readme.txt")), settings, ctx, r, errorMessage))
  }
  CArchiveSession session;
  {
    COperationContext ctx;
    CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  }
  const FString dest = t["d"];
  CExtractOptions options;
  options.Destination = dest;
  {
    CExtractResult r;
    COperationContext ctx;
    CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, r, errorMessage))
  }
  CScriptedPrompts prompts(NULL, NOverwriteChoice::kKeepBoth);
  {
    CExtractResult r;
    COperationContext ctx(NULL, &prompts);
    CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, r, errorMessage))
  }
  CHECK(prompts.OverwriteRequests == 1)
  CHECK(FileExists(Combine(dest, "readme (2).txt")))
}

static void ZoneIdentifierPropagates()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = t["web.zip"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    settings.Format = "zip";
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(Combine(src, "readme.txt")), settings, ctx, r, errorMessage))
  }
  CHECK(WriteText(archive + FTEXT(":Zone.Identifier"), AString("[ZoneTransfer]\r\nZoneId=3\r\n")))

  CArchiveSession session;
  {
    COperationContext ctx;
    CHECK_HR(session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage))
  }
  const FString dest = t["d"];
  CExtractOptions options;
  options.Destination = dest;
  CExtractResult r;
  COperationContext ctx;
  CHECK_HR(NExtractEngine::Extract(session, NULL, options, ctx, r, errorMessage))
  CByteBuffer zone;
  CHECK(ReadAll(Combine(dest, "readme.txt") + FTEXT(":Zone.Identifier"), zone))
  AString text;
  text.SetFrom_CalcLen((const char *)(const Byte *)zone, (unsigned)zone.Size());
  CHECK(text.Find("ZoneId=3") >= 0)
}

static void HashesMatchKnownVectors()
{
  const CHashAlgorithmInfo *crc = NHashEngine::Find(UString("CRC32"));
  const CHashAlgorithmInfo *sha = NHashEngine::Find(UString("SHA256"));
  CHECK(crc != NULL)
  CHECK(sha != NULL)
  UString hex;
  CHECK(NHashEngine::ComputeBytes("abc", 3, *crc, hex) == S_OK)
  CHECK(hex.IsEqualTo("352441C2"))
  CHECK(NHashEngine::ComputeBytes("abc", 3, *sha, hex) == S_OK)
  CHECK(hex.IsEqualTo("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"))
  bool hasBlake = false;
  const CObjectVector<CHashAlgorithmInfo> &all = NHashEngine::GetAlgorithms();
  FOR_VECTOR (i, all)
    if (all[i].Name.IsPrefixedBy_Ascii_NoCase("BLAKE2"))
      hasBlake = true;
  CHECK(hasBlake)
}

// A program that keeps a file open with DELETE access (to rename or delete it later)
// must not stop 7-Zip from opening, compressing or hashing that file.
static void FilesHeldWithDeleteAccessCanBeRead()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString archive = Combine(t.Path(), "held.7z");
  UString errorMessage;
  CCompressionSettings settings;
  settings.Format = "7z";
  CCompressResult result;
  {
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(archive, OneSource(src), settings, ctx, result, errorMessage))
  }

  const HANDLE held = ::CreateFileW(fs2us(archive), GENERIC_READ | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
  CHECK(held != INVALID_HANDLE_VALUE)
  bool opened = false, compressed = false, hashed = false;
  {
    CArchiveSession session;
    COperationContext ctx;
    opened = (session.Open(archive, CArchiveOpenOptions(), ctx, errorMessage) == S_OK);
  }
  {
    CCompressionSettings zip;
    zip.Format = "zip";
    CCompressResult r2;
    COperationContext ctx;
    compressed = (NArchiveWriter::Create(Combine(t.Path(), "copy.zip"), OneSource(archive),
        zip, ctx, r2, errorMessage) == S_OK && r2.Files == 1);
  }
  {
    CObjectVector<CHashAlgorithmInfo> algorithms;
    algorithms.Add(*NHashEngine::Find(UString("CRC32")));
    CObjectVector<CFileHashResult> hashes;
    COperationContext ctx;
    hashed = (NHashEngine::ComputeFiles(OneSource(archive), algorithms, ctx, hashes, NULL, errorMessage) == S_OK
        && hashes.Size() == 1 && hashes[0].Error.IsEmpty());
  }
  ::CloseHandle(held);
  CHECK(opened)
  CHECK(compressed)
  CHECK(hashed)
}

static void SmartDestinationAvoidsDoubleFolders()
{
  CTempDir t;
  const FString src = MakeSourceTree(t);
  const FString single = t["single.zip"];
  UString errorMessage;
  {
    CCompressionSettings settings;
    settings.Format = "zip";
    CCompressResult r;
    COperationContext ctx;
    CHECK_HR(NArchiveWriter::Create(single, OneSource(src), settings, ctx, r, errorMessage))
  }
  {
    CArchiveSession s;
    COperationContext ctx;
    CHECK_HR(s.Open(single, CArchiveOpenOptions(), ctx, errorMessage))
    CHECK(NExtractPlanner::GetSmartDestination(s, t.Path()) == t.Path())
  }
  CHECK(NExtractPlanner::GetFolderNameFor(fs2us(t["loose.tar.gz"])).IsEqualTo("loose"))
  CHECK(NExtractPlanner::GetFolderNameFor(UString("data.7z.001")).IsEqualTo("data"))
}

static void BenchmarkRoundTrips()
{
  CByteBuffer data;
  NBenchmarkRunner::GenerateData((size_t)4 << 20, data);
  CBenchmarkPass pass;
  UString errorMessage;
  COperationContext ctx;
  CHECK_HR(NBenchmarkRunner::RunPass(1, data, (UInt64)1 << 22, 2, ctx, pass, errorMessage))
  CHECK(pass.CompressMBps > 0 && pass.DecompressMBps > 0)
  CHECK(pass.Ratio >= 0.05 && pass.Ratio <= 0.9)
}

static void FormatCount_UsesSingularOnlyForOne()
{
  CHECK(NPropertyFormatter::FormatCount(0, L"file").IsEqualTo("0 files"))
  CHECK(NPropertyFormatter::FormatCount(1, L"file").IsEqualTo("1 file"))
  CHECK(NPropertyFormatter::FormatCount(2, L"folder").IsEqualTo("2 folders"))
  CHECK(NPropertyFormatter::FormatCount(1, L"entry", L"entries").IsEqualTo("1 entry"))
  CHECK(NPropertyFormatter::FormatCount(3, L"entry", L"entries").IsEqualTo("3 entries"))
  // like the C# test ($"{1234:N0} items"): the number uses the user's digit grouping
  UString expected = NPropertyFormatter::FormatNumber(1234);
  CHECK(expected.Len() >= 4 && expected[0] == '1' && expected.Back() == '4')
  expected += " items";
  CHECK(NPropertyFormatter::FormatCount(1234, L"item") == expected)
}

// ---------- main ----------

typedef void (*TestFunc)();

struct CTest
{
  const char *Name;
  TestFunc Func;
  const char *Format;
};

static const CTest kTests[] =
{
  { "EngineLoadsAndListsFormats", EngineLoadsAndListsFormats, NULL },
  { "RoundTripsAFolder(7z)", RoundTripsAFolder, "7z" },
  { "RoundTripsAFolder(zip)", RoundTripsAFolder, "zip" },
  { "RoundTripsAFolder(tar)", RoundTripsAFolder, "tar" },
  { "RoundTripsAFolder(tar.gz)", RoundTripsAFolder, "tar.gz" },
  { "RoundTripsAFolder(tar.xz)", RoundTripsAFolder, "tar.xz" },
  { "RoundTripsAFolder(tar.bz2)", RoundTripsAFolder, "tar.bz2" },
  { "RoundTripsAFolder(wim)", RoundTripsAFolder, "wim" },
  { "EncryptedHeadersRequirePassword", EncryptedHeadersRequirePassword, NULL },
  { "WrongPasswordOnZipIsReported", WrongPasswordOnZipIsReported, NULL },
  { "UpdateAddsRenamesAndDeletes", UpdateAddsRenamesAndDeletes, NULL },
  { "SplitVolumesOpenAsOneArchive", SplitVolumesOpenAsOneArchive, NULL },
  { "ExtractionIsConfinedToDestination", ExtractionIsConfinedToDestination, NULL },
  { "OverwritePromptsAndKeepBoth", OverwritePromptsAndKeepBoth, NULL },
  { "ZoneIdentifierPropagates", ZoneIdentifierPropagates, NULL },
  { "HashesMatchKnownVectors", HashesMatchKnownVectors, NULL },
  { "FilesHeldWithDeleteAccessCanBeRead", FilesHeldWithDeleteAccessCanBeRead, NULL },
  { "SmartDestinationAvoidsDoubleFolders", SmartDestinationAvoidsDoubleFolders, NULL },
  { "BenchmarkRoundTrips", BenchmarkRoundTrips, NULL },
  { "FormatCount_UsesSingularOnlyForOne", FormatCount_UsesSingularOnlyForOne, NULL }
};

int Z7_CDECL main(int /* numArgs */, const char * /* args */[])
{
  UString errorMessage;
  if (NSevenZipEngine::EnsureLoaded(FString(), errorMessage) != S_OK)
  {
    Print(errorMessage);
    fputs("\n", stdout);
    return (int)Z7_ARRAY_SIZE(kTests);
  }
  int numFailed = 0;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(kTests); i++)
  {
    const CTest &test = kTests[i];
    g_TestFailed = false;
    g_FailText.Empty();
    g_Format = test.Format;
    test.Func();
    printf("%-40s %s", test.Name, g_TestFailed ? "FAIL" : "PASS");
    if (g_TestFailed)
    {
      printf("  (%s)", g_FailText.Ptr());
      numFailed++;
    }
    printf("\n");
    fflush(stdout);
  }
  printf("\n%d of %u tests failed\n", numFailed, (unsigned)Z7_ARRAY_SIZE(kTests));
  return numFailed;
}
