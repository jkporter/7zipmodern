// LaunchRequest.cpp

#include "StdAfx.h"

#include "LaunchRequest.h"

#include "../../Common/CommandLineParser.h"
#include "../../Common/UTFConvert.h"

#include "../../Windows/FileDir.h"
#include "../../Windows/FileFind.h"
#include "../../Windows/FileIO.h"

#include "../HString.h"

#include "../Core/ArchiveFormat.h"

using namespace NWindows;
using namespace NFile;
using namespace winrt::Microsoft::Windows::AppLifecycle;
using namespace winrt::Windows::ApplicationModel::Activation;

struct CVerbName
{
  const char *Name;
  NLaunchVerb::EEnum Verb;
};

static const CVerbName k_Verbs[] =
{
  { "open", NLaunchVerb::kOpen },
  { "extract", NLaunchVerb::kExtract },
  { "extract-here", NLaunchVerb::kExtractHere },
  { "x-here", NLaunchVerb::kExtractHere },
  { "extract-smart", NLaunchVerb::kExtractSmart },
  { "extract-to", NLaunchVerb::kExtractSmart },
  { "test", NLaunchVerb::kTest },
  { "compress", NLaunchVerb::kCompress },
  { "add", NLaunchVerb::kCompress },
  { "compress-7z", NLaunchVerb::kCompress7z },
  { "compress-zip", NLaunchVerb::kCompressZip },
  { "compress-share", NLaunchVerb::kCompressShare },
  { "hash", NLaunchVerb::kHash },
  { "benchmark", NLaunchVerb::kBenchmark },
  { "settings", NLaunchVerb::kSettings }
};

static bool ParseVerb(const UString &s, NLaunchVerb::EEnum &verb)
{
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_Verbs); i++)
    if (s.IsEqualTo_Ascii_NoCase(k_Verbs[i].Name))
    {
      verb = k_Verbs[i].Verb;
      return true;
    }
  return false;
}


bool CLaunchRequest::RunsInOwnWindow() const
{
  return Verb == NLaunchVerb::kExtract
      || Verb == NLaunchVerb::kExtractHere
      || Verb == NLaunchVerb::kExtractSmart
      || Verb == NLaunchVerb::kTest
      || Verb == NLaunchVerb::kCompress7z
      || Verb == NLaunchVerb::kCompressZip
      || Verb == NLaunchVerb::kCompressShare
      || Verb == NLaunchVerb::kReveal;
}


void CLaunchRequest::FromToast(const winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> &arguments,
    CLaunchRequest &r)
{
  r = CLaunchRequest();
  try
  {
    if (arguments && arguments.HasKey(L"action") && arguments.Lookup(L"action") == L"reveal" && arguments.HasKey(L"path"))
    {
      r.Verb = NLaunchVerb::kReveal;
      r.Paths.Add(NWindows11::ToUString(arguments.Lookup(L"path")));
    }
  }
  catch (...) {}
}


static void SplitCommandLineWithExe(const UString &commandLine, UStringVector &argv)
{
  argv.Clear();
  // CommandLineToArgvW rules: the first argument (program name) is special,
  // the others follow the usual quoting rules.
  int num = 0;
  LPWSTR *list = ::CommandLineToArgvW(commandLine, &num);
  if (!list)
    return;
  for (int i = 0; i < num; i++)
    argv.Add(UString(list[i]));
  ::LocalFree(list);
}


void CLaunchRequest::FromActivation(const AppActivationArguments &args, CLaunchRequest &r)
{
  r = CLaunchRequest();
  try
  {
    const ExtendedActivationKind kind = args.Kind();
    if (kind == ExtendedActivationKind::File)
    {
      const auto file = args.Data().try_as<IFileActivatedEventArgs>();
      if (file)
      {
        for (const auto &item: file.Files())
        {
          const winrt::hstring path = item.Path();
          if (!path.empty())
            r.Paths.Add(NWindows11::ToUString(path));
        }
        r.Verb = NLaunchVerb::kOpen;
        NLaunchVerb::EEnum verb;
        if (ParseVerb(NWindows11::ToUString(file.Verb()), verb))
          r.Verb = verb;
        return;
      }
    }
    else if (kind == ExtendedActivationKind::ShareTarget)
    {
      const auto share = args.Data().try_as<IShareTargetActivatedEventArgs>();
      if (share)
      {
        r.Verb = NLaunchVerb::kCompress;
        r.ShareOperation = share.ShareOperation();
        return;
      }
    }
    else if (kind == ExtendedActivationKind::AppNotification)
    {
      const auto toast = args.Data().try_as<winrt::Microsoft::Windows::AppNotifications::AppNotificationActivatedEventArgs>();
      if (toast)
      {
        FromToast(toast.Arguments(), r);
        return;
      }
    }
    else if (kind == ExtendedActivationKind::Launch)
    {
      const auto launch = args.Data().try_as<ILaunchActivatedEventArgs>();
      if (launch)
      {
        UStringVector argv;
        const UString s = NWindows11::ToUString(launch.Arguments());
        if (!s.IsEmpty())
          NCommandLineParser::SplitCommandLine(s, argv);
        Parse(argv, false, r);
        return;
      }
    }
  }
  catch (...)
  {
    r = CLaunchRequest();
    return;
  }
  UStringVector argv;
  SplitCommandLineWithExe(UString(::GetCommandLineW()), argv);
  Parse(argv, true, r);
}


static void ReadListFile(const FString &listFile, UStringVector &paths)
{
  NIO::CInFile file;
  if (!file.Open(listFile))
    return;
  UInt64 length;
  if (!file.GetLength(length) || length > ((UInt64)1 << 28))
    return;
  CByteBuffer buf((size_t)length);
  size_t processed;
  if (!file.ReadFull(buf, (size_t)length, processed))
    return;
  file.Close();

  UString text;
  const Byte *p = buf;
  if (processed >= 2 && p[0] == 0xFF && p[1] == 0xFE)
    text.SetFrom((const wchar_t *)(const void *)(p + 2), (unsigned)((processed - 2) / 2));
  else
  {
    if (processed >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
    {
      p += 3;
      processed -= 3;
    }
    AString utf8;
    utf8.SetFrom((const char *)p, (unsigned)processed);
    ConvertUTF8ToUnicode(utf8, text);
  }

  // List files written by the shell extension live in %TEMP% and are single-use.
  FString fullPath, tempPath;
  if (NDir::MyGetFullPathName(listFile, fullPath) && NDir::MyGetTempPath(tempPath)
      && fs2us(fullPath).IsPrefixedBy_NoCase(fs2us(tempPath)))
    NDir::DeleteFileAlways(listFile);

  UString line;
  for (unsigned i = 0;; i++)
  {
    const wchar_t c = (i < text.Len()) ? text[i] : (wchar_t)'\n';
    if (c == '\n' || c == '\r')
    {
      line.Trim();
      if (!line.IsEmpty())
        paths.Add(line);
      line.Empty();
      if (i >= text.Len())
        break;
    }
    else
      line += c;
  }
}


static bool IsExistingArchiveFile(const UString &path)
{
  return NFind::DoesFileExist_FollowLink(us2fs(path)) && NFormatRegistry::LooksLikeArchive(path);
}


void CLaunchRequest::Parse(const UStringVector &argv, bool skipExe, CLaunchRequest &r)
{
  r = CLaunchRequest();
  UStringVector args;
  for (unsigned i = skipExe ? 1 : 0; i < argv.Size(); i++)
    args.Add(argv[i]);
  if (args.Size() > 0)
  {
    const UString &first = args[0];
    if (first.Len() > 4 && StringsAreEqualNoCase_Ascii(first.RightPtr(4), ".exe")
        && NFind::DoesFileExist_FollowLink(us2fs(first)) && !NFormatRegistry::LooksLikeArchive(first))
      args.Delete(0);
  }

  bool verbDefined = false;
  NLaunchVerb::EEnum verb = NLaunchVerb::kHome;
  FOR_VECTOR (i, args)
  {
    const UString &a = args[i];
    if ((a.IsEqualTo("--format") || a.IsEqualTo("-t")) && i + 1 < args.Size())
    {
      r.Format = args[++i];
      continue;
    }
    if ((a.IsEqualTo("--algorithm") || a.IsEqualTo("-a")) && i + 1 < args.Size())
    {
      r.HashAlgorithm = args[++i];
      continue;
    }
    if (!verbDefined && r.Paths.IsEmpty() && ParseVerb(a, verb))
    {
      verbDefined = true;
      continue;
    }
    if (a.Len() > 1 && a[0] == '@' && NFind::DoesFileExist_FollowLink(us2fs(a.Ptr(1))))
    {
      ReadListFile(us2fs(a.Ptr(1)), r.Paths);
      continue;
    }
    if (!a.IsEmpty())
    {
      FString full;
      if (NDir::MyGetFullPathName(us2fs(a), full))
        r.Paths.Add(fs2us(full));
      else
        r.Paths.Add(a);
    }
  }

  if (!verbDefined)
  {
    // Bare paths: archives open, anything else goes to "Create archive".
    if (r.Paths.IsEmpty())
      verb = NLaunchVerb::kHome;
    else
    {
      verb = NLaunchVerb::kOpen;
      FOR_VECTOR (i, r.Paths)
        if (!IsExistingArchiveFile(r.Paths[i]))
        {
          verb = NLaunchVerb::kCompress;
          break;
        }
    }
  }
  r.Verb = verb;
}
