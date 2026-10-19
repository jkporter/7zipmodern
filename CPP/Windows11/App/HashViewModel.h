// HashViewModel.h

#ifndef ZIP7_INC_HASH_VIEW_MODEL_H
#define ZIP7_INC_HASH_VIEW_MODEL_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Bindable.h"
#include "../Command.h"

#include "../Core/HashEngine.h"

#include "CompressViewModel.h"
#include "WindowHost.h"

// Puts plain text on the clipboard (UI thread).
void CopyTextToClipboard(const UString &text);

// A toggle in the algorithm list. Bindable: Name, IsSelected (TwoWay).
class CAlgorithmOption: public NWindows11::CBindableT<CAlgorithmOption>
{
public:
  const CHashAlgorithmInfo Info;
  bool IsSelected;

  CAlgorithmOption(const CHashAlgorithmInfo &info, bool selected);
  static CAlgorithmOption *FromObject(const winrt::Windows::Foundation::IInspectable &item);
};

// One digest of a file. Bindable: Algorithm, Value, Matches, CopyLabel, CopyCommand.
class CDigestRow: public NWindows11::CBindableT<CDigestRow>
{
  bool _matches;
  winrt::com_ptr<NWindows11::CCommand> _copyCommand;
public:
  const UString Algorithm;
  const UString Value;

  CDigestRow(const UString &algorithm, const UString &value);
  bool GetMatches() const { return _matches; }
  void SetMatches(bool matches);
  static CDigestRow *FromObject(const winrt::Windows::Foundation::IInspectable &item);
};

// The results of one file. Bindable: Name, SizeText, Digests (CDigestRow objects).
class CFileHashItem: public NWindows11::CBindableT<CFileHashItem>
{
public:
  const UString Name;
  const UString SizeText;
  CUiItemVector Digests;

  CFileHashItem(const CFileHashResult &result);
  static CFileHashItem *FromObject(const winrt::Windows::Foundation::IInspectable &item);
};

namespace NVerifyState
{
  enum EEnum
  {
    kNone,   // nothing to compare (C# null)
    kMatch,
    kNoMatch
  };
}

/*
The Checksums page (successor of the CRC SHA context submenu + its results dialog).
Lists (ItemsSource set by the page): Sources (CSourceItem), Algorithms (CAlgorithmOption),
Results (CFileHashItem).
Bindable: IsWorking, Expected (TwoWay), VerifyText, HasVerifyText, VerifyOk (NVerifyState as int),
HasSources, HasResults.
UI thread only.
*/

class CHashViewModel: public NWindows11::CBindableT<CHashViewModel>
{
  FStringVector _sourcePaths;            // parallel to Sources
  CObjectVector<CFileHashResult> _results; // parallel to Results
  bool _isWorking;
  UString _expected;
  UString _verifyText;
  NVerifyState::EEnum _verifyState;

  void SetIsWorking(bool value);
  void RaiseCounts();

public:
  CUiItemVector Sources;
  CUiItemVector Algorithms;
  CUiItemVector Results;
  bool Closed; // the tab was closed: don't use the host after an await

  CHashViewModel(const UStringVector &paths, const UString &algorithm);

  bool GetIsWorking() const { return _isWorking; }
  unsigned GetNumSources() const { return _sourcePaths.Size(); }
  unsigned GetNumResults() const { return _results.Size(); }
  const CFileHashResult &GetResult(unsigned index) const { return _results[index]; }
  NVerifyState::EEnum GetVerifyState() const { return _verifyState; }

  void AddSource(const UString &path);
  void ClearSources();
  void ClearResults();
  void SetExpected(const UString &text); // also updates the bound text box

  // Paste a published checksum: every digest is compared, regardless of algorithm.
  void Verify();
  // called on the UI thread for each file as it is hashed
  void AddResult(const CFileHashResult &result);

  winrt::Windows::Foundation::IAsyncAction ComputeAsync(IWindowHost *host);

  // the selected algorithm whose name starts with "SHA", else the first selected one; NULL = none
  const CAlgorithmOption *GetExportAlgorithm() const;
  UString ExportText(const UString &algorithm) const;
  UString CopyAllText() const;
};

#endif
