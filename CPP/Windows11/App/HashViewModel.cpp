// HashViewModel.cpp

#include "StdAfx.h"

#include "HashViewModel.h"

#include "../HString.h"

#include "../Core/PropertyFormatter.h"

#include "DialogService.h"
#include "OperationService.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace NWindows11;

typedef winrt::Windows::Foundation::IInspectable CObject;

static CUiItemVector NewItemVector() { return single_threaded_observable_vector<CObject>(); }

void CopyTextToClipboard(const UString &text)
{
  winrt::Windows::ApplicationModel::DataTransfer::DataPackage package;
  package.SetText(ToHString(text));
  winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
}


// ---------- items ----------

CAlgorithmOption::CAlgorithmOption(const CHashAlgorithmInfo &info, bool selected):
    Info(info),
    IsSelected(selected)
{
  AddProperty(L"Name", [this] { return BoxString(Info.Name); });
  AddProperty(L"IsSelected", [this] { return box_value(IsSelected); },
      [this](const CObject &v) { IsSelected = unbox_value_or<bool>(v, false); });
}

CAlgorithmOption *CAlgorithmOption::FromObject(const CObject &item)
{
  return get_self<CAlgorithmOption>(item.as<Data::ICustomPropertyProvider>());
}

CDigestRow::CDigestRow(const UString &algorithm, const UString &value):
    _matches(false),
    Algorithm(algorithm),
    Value(value)
{
  _copyCommand = make_self<CCommand>([this] { CopyTextToClipboard(Value); });
  AddProperty(L"Algorithm", [this] { return BoxString(Algorithm); });
  AddProperty(L"Value", [this] { return BoxString(Value); });
  AddProperty(L"Matches", [this] { return box_value(_matches); });
  AddProperty(L"CopyLabel", [this]
  {
    UString s (L"Copy ");
    s += Algorithm;
    return BoxString(s);
  });
  AddProperty(L"CopyCommand", [this]() -> CObject { return _copyCommand.as<Input::ICommand>(); });
}

void CDigestRow::SetMatches(bool matches)
{
  if (matches == _matches)
    return;
  _matches = matches;
  RaisePropertyChanged(L"Matches");
}

CDigestRow *CDigestRow::FromObject(const CObject &item)
{
  return get_self<CDigestRow>(item.as<Data::ICustomPropertyProvider>());
}

static UString GetSizeText(const CFileHashResult &r)
{
  return r.Error.IsEmpty() ? NPropertyFormatter::FormatSize(r.Size) : r.Error;
}

CFileHashItem::CFileHashItem(const CFileHashResult &result):
    Name(result.RelativePath),
    SizeText(GetSizeText(result)),
    Digests(NewItemVector())
{
  FOR_VECTOR (i, result.Digests)
    Digests.Append(make<CDigestRow>(result.Digests[i].Algorithm, result.Digests[i].Hex));
  AddProperty(L"Name", [this] { return BoxString(Name); });
  AddProperty(L"SizeText", [this] { return BoxString(SizeText); });
  AddProperty(L"Digests", [this]() -> CObject { return Digests; });
}

CFileHashItem *CFileHashItem::FromObject(const CObject &item)
{
  return get_self<CFileHashItem>(item.as<Data::ICustomPropertyProvider>());
}


// ---------- CHashViewModel ----------

static UString RemoveDashes(const UString &s)
{
  UString r = s;
  r.RemoveChar(L'-');
  return r;
}

CHashViewModel::CHashViewModel(const UStringVector &paths, const UString &algorithm):
    _isWorking(false),
    _verifyState(NVerifyState::kNone),
    Sources(NewItemVector()),
    Algorithms(NewItemVector()),
    Results(NewItemVector()),
    Closed(false)
{
  FOR_VECTOR (i, paths)
    AddSource(paths[i]);

  UStringVector preferred;
  if (algorithm.IsEmpty())
  {
    preferred.Add(UString(L"CRC32"));
    preferred.Add(UString(L"SHA256"));
  }
  else
    preferred.Add(algorithm);
  const CObjectVector<CHashAlgorithmInfo> &all = NHashEngine::GetAlgorithms();
  FOR_VECTOR (i, all)
  {
    const UString name = RemoveDashes(all[i].Name);
    bool selected = (algorithm == L"*");
    FOR_VECTOR (k, preferred)
      if (RemoveDashes(preferred[k]).IsEqualTo_NoCase(name))
        selected = true;
    Algorithms.Append(make<CAlgorithmOption>(all[i], selected));
  }

  AddProperty(L"IsWorking", [this] { return box_value(_isWorking); });
  AddProperty(L"Expected", [this] { return BoxString(_expected); },
      [this](const CObject &v)
      {
        const UString s = UnboxString(v);
        if (s == _expected)
          return;
        _expected = s;
        Verify();
      });
  AddProperty(L"VerifyText", [this] { return BoxString(_verifyText); });
  AddProperty(L"HasVerifyText", [this] { return box_value(!_verifyText.IsEmpty()); });
  AddProperty(L"VerifyOk", [this] { return box_value((int32_t)_verifyState); });
  AddProperty(L"HasSources", [this] { return box_value(!_sourcePaths.IsEmpty()); });
  AddProperty(L"HasResults", [this] { return box_value(!_results.IsEmpty()); });
}

void CHashViewModel::RaiseCounts()
{
  RaisePropertyChanged(L"HasSources");
  RaisePropertyChanged(L"HasResults");
}

void CHashViewModel::SetIsWorking(bool value)
{
  _isWorking = value;
  RaisePropertyChanged(L"IsWorking");
}

void CHashViewModel::AddSource(const UString &path)
{
  _sourcePaths.Add(us2fs(path));
  Sources.Append(make<CSourceItem>(path));
  RaisePropertyChanged(L"HasSources");
}

void CHashViewModel::ClearSources()
{
  _sourcePaths.Clear();
  Sources.Clear();
  RaisePropertyChanged(L"HasSources");
}

void CHashViewModel::ClearResults()
{
  _results.Clear();
  Results.Clear();
  RaisePropertyChanged(L"HasResults");
}

void CHashViewModel::SetExpected(const UString &text)
{
  if (text == _expected)
    return;
  _expected = text;
  RaisePropertyChanged(L"Expected");
  Verify();
}

void CHashViewModel::AddResult(const CFileHashResult &result)
{
  _results.Add(result);
  Results.Append(make<CFileHashItem>(result));
  if (_results.Size() == 1)
    RaisePropertyChanged(L"HasResults");
}

static bool IsHexDigit(wchar_t c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

void CHashViewModel::Verify()
{
  UString expected;
  for (unsigned i = 0; i < _expected.Len(); i++)
    if (IsHexDigit(_expected[i]))
      expected += _expected[i];
  const bool canMatch = (expected.Len() >= 8);

  bool any = false;
  UString hitAlgorithm, hitName;
  for (unsigned i = 0; i < Results.Size(); i++)
  {
    const CFileHashItem *item = CFileHashItem::FromObject(Results.GetAt(i));
    for (unsigned k = 0; k < item->Digests.Size(); k++)
    {
      CDigestRow *row = CDigestRow::FromObject(item->Digests.GetAt(k));
      const bool matches = canMatch && row->Value.IsEqualTo_NoCase(expected);
      row->SetMatches(matches);
      if (matches && !any)
      {
        any = true;
        hitAlgorithm = row->Algorithm;
        hitName = item->Name;
      }
    }
  }

  if (!canMatch)
  {
    _verifyState = NVerifyState::kNone;
    _verifyText.Empty();
  }
  else if (any)
  {
    _verifyState = NVerifyState::kMatch;
    _verifyText = L"Match: ";
    _verifyText += hitAlgorithm;
    _verifyText += L" of ";
    _verifyText += hitName;
  }
  else
  {
    _verifyState = NVerifyState::kNoMatch;
    _verifyText = (Results.Size() == 0) ?
        L"Compute checksums first." :
        L"No match. The file may be damaged or different.";
  }
  RaisePropertyChanged(L"VerifyOk");
  RaisePropertyChanged(L"VerifyText");
  RaisePropertyChanged(L"HasVerifyText");
}


// Receives each finished file on the worker thread and adds it to the list on the UI thread.

class CHashFileSink Z7_final: public IHashFileCallback
{
  com_ptr<CHashViewModel> _owner;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue _dispatcher;
public:
  Z7_IFACE_IMP(IHashFileCallback)

  CHashFileSink(const com_ptr<CHashViewModel> &owner, const winrt::Microsoft::UI::Dispatching::DispatcherQueue &dispatcher):
      _owner(owner), _dispatcher(dispatcher)
      {}
};

void CHashFileSink::HashFile_Done(const CFileHashResult &result)
{
  const com_ptr<CHashViewModel> owner = _owner;
  const CFileHashResult copy = result;
  _dispatcher.TryEnqueue([owner, copy] { owner->AddResult(copy); });
}


winrt::Windows::Foundation::IAsyncAction CHashViewModel::ComputeAsync(IWindowHost *host)
{
  auto strong = get_strong();
  CObjectVector<CHashAlgorithmInfo> algorithms;
  for (unsigned i = 0; i < Algorithms.Size(); i++)
  {
    const CAlgorithmOption *a = CAlgorithmOption::FromObject(Algorithms.GetAt(i));
    if (a->IsSelected)
      algorithms.Add(a->Info);
  }
  if (algorithms.IsEmpty() || _sourcePaths.IsEmpty())
    co_return;

  ClearResults();
  SetIsWorking(true);
  const FStringVector paths = _sourcePaths;
  const winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher = host->Dialogs().GetDispatcher();
  com_ptr<COperationViewModel> op = make_self<COperationViewModel>(NOperationKind::kHash, UString(L"Computing checksums"));
  CObjectVector<CFileHashResult> results;
  CHashFileSink sink(strong, dispatcher);
  try
  {
    const HRESULT hr = co_await host->Operations().RunAsync(op,
        [&](COperationContext &ctx, UString &errorMessage)
          { return NHashEngine::ComputeFiles(paths, algorithms, ctx, results, &sink, errorMessage); },
        [&] { return NPropertyFormatter::FormatCount((Int64)results.Size(), L"file"); });
    if (hr == S_OK || hr == E_ABORT)
    {
      // queued behind the per-file results, so it sees all of them
      dispatcher.TryEnqueue([strong] { strong->Verify(); });
    }
    else if (!Closed)
      co_await host->Dialogs().ShowErrorAsync(L"Couldn't compute checksums", op->GetErrorMessage());
  }
  catch (...)
  {
    SetIsWorking(false);
    throw;
  }
  SetIsWorking(false);
}

const CAlgorithmOption *CHashViewModel::GetExportAlgorithm() const
{
  const CAlgorithmOption *first = NULL;
  for (unsigned i = 0; i < Algorithms.Size(); i++)
  {
    const CAlgorithmOption *a = CAlgorithmOption::FromObject(Algorithms.GetAt(i));
    if (!a->IsSelected)
      continue;
    if (a->Info.Name.IsPrefixedBy_Ascii_NoCase("SHA"))
      return a;
    if (!first)
      first = a;
  }
  return first;
}

UString CHashViewModel::ExportText(const UString &algorithm) const
{
  return NHashEngine::FormatChecksumFile(_results, algorithm);
}

UString CHashViewModel::CopyAllText() const
{
  UString s;
  FOR_VECTOR (i, _results)
  {
    const CFileHashResult &r = _results[i];
    s += r.RelativePath;
    s += L"\r\n";
    FOR_VECTOR (k, r.Digests)
    {
      const CHashDigest &d = r.Digests[k];
      s += L"  ";
      s += d.Algorithm;
      for (unsigned n = d.Algorithm.Len(); n < 10; n++)
        s.Add_Space();
      s.Add_Space();
      s += d.Hex;
      s += L"\r\n";
    }
  }
  return s;
}
