// LaunchRequest.h

#ifndef ZIP7_INC_LAUNCH_REQUEST_H
#define ZIP7_INC_LAUNCH_REQUEST_H

#include "../../Common/MyString.h"

namespace NLaunchVerb
{
  enum EEnum
  {
    kHome,
    kOpen,
    kExtract,       // "Extract files..." - ask for options first
    kExtractHere,
    kExtractSmart,  // "Extract to smart folder" (classic: Extract to "name\")
    kTest,
    kCompress,      // "Add to archive..." - full options in the main window
    kCompress7z,
    kCompressZip,
    kCompressShare, // compress to a temporary zip and open the Windows Share sheet
    kHash,
    kBenchmark,
    kSettings,
    kReveal         // notification button
  };
}

/*
What the app was asked to do, from any activation source: command line /
execution alias, File Explorer context menu (via the shell extension),
file-type association, Share target, jump list task or a notification.

Command line: SevenZip [verb] [--format NAME] [--algorithm NAME] paths...
Paths may be given as @listfile (UTF-16 / UTF-8 lines), which the shell
extension uses for big selections.
*/

struct CLaunchRequest
{
  NLaunchVerb::EEnum Verb;
  UStringVector Paths;
  UString Format;
  UString HashAlgorithm;
  winrt::Windows::ApplicationModel::DataTransfer::ShareTarget::ShareOperation ShareOperation { nullptr };

  CLaunchRequest(): Verb(NLaunchVerb::kHome) {}

  // verbs that run in their own process with a compact progress window (like classic 7zG)
  bool RunsInOwnWindow() const;

  // these never throw
  static void FromActivation(const winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments &args, CLaunchRequest &r);
  static void FromToast(const winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> &arguments,
      CLaunchRequest &r);
  static void Parse(const UStringVector &argv, bool skipExe, CLaunchRequest &r);
};

#endif
