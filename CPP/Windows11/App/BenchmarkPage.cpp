// BenchmarkPage.cpp

#include "StdAfx.h"

#include "BenchmarkPage.h"

#include "../../Windows/System.h"

#include "../Dispatcher.h"
#include "../HString.h"
#include "../Xaml.h"

#include "../Core/PropertyFormatter.h"

#include "App.h"
#include "PageFactory.h"
#include "resource.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace NWindows;
using namespace NWindows11;

typedef winrt::Windows::Foundation::IInspectable CObject;

static const struct
{
  const wchar_t *Label;
  UInt64 Size;
}
k_Dictionaries[] =
{
  { L"4 MB", (UInt64)4 << 20 },
  { L"16 MB", (UInt64)16 << 20 },
  { L"32 MB", (UInt64)32 << 20 },
  { L"64 MB", (UInt64)64 << 20 }
};

static const unsigned k_NumPasses = 10;

// C# "{0:N1} MB/s"
static UString FormatSpeed(double mbps)
{
  UString s = NPropertyFormatter::FormatDecimal(mbps, 1);
  s += L" MB/s";
  return s;
}

// C# "P0"
static UString FormatPercent(double fraction)
{
  UString s = NPropertyFormatter::FormatDecimal(fraction * 100, 0);
  s += L'%';
  return s;
}

// One row of the results list.
class CBenchmarkPassItem: public CBindableT<CBenchmarkPassItem>
{
  UString _pass;
  UString _compress;
  UString _decompress;
  UString _ratio;
public:
  CBenchmarkPassItem(const CBenchmarkPass &p)
  {
    _pass = L"Pass ";
    _pass.Add_UInt32(p.Pass);
    _compress = FormatSpeed(p.CompressMBps);
    _decompress = FormatSpeed(p.DecompressMBps);
    _ratio = FormatPercent(p.Ratio);
    AddProperty(L"PassLabel", [this] { return BoxString(_pass); });
    AddProperty(L"CompressText", [this] { return BoxString(_compress); });
    AddProperty(L"DecompressText", [this] { return BoxString(_decompress); });
    AddProperty(L"RatioText", [this] { return BoxString(_ratio); });
  }
};


CTabPageRef CreateBenchmarkPage(IWindowHost * /* host */)
{
  com_ptr<CBenchmarkPage> page = make_self<CBenchmarkPage>();
  page->Init();
  return MakeUiObjectRef<ITabPage>(page);
}

CBenchmarkPage::CBenchmarkPage():
    _dictionaryChoices(single_threaded_observable_vector<CObject>()),
    _passItems(single_threaded_observable_vector<CObject>()),
    _dictionaryIndex(2),
    _numProcessors(NSystem::GetNumberOfProcessors()),
    _isRunning(false),
    _closed(false),
    _cancelRequested(false),
    _context(NULL),
    _compressSpeed(L"\x2014"),
    _decompressSpeed(L"\x2014"),
    _averageText(L"Measures LZMA2 compression and decompression speed with the real engine."
        L" Data is generated in memory; nothing is written to disk.")
{
  if (_numProcessors == 0)
    _numProcessors = 1;
  _threads = _numProcessors;
  for (unsigned i = 0; i < Z7_ARRAY_SIZE(k_Dictionaries); i++)
    _dictionaryChoices.Append(box_value(hstring(k_Dictionaries[i].Label)));

  AddProperty(L"DictionaryIndex", [this] { return box_value((int32_t)_dictionaryIndex); },
      [this](const CObject &v)
      {
        const int i = unbox_value_or<int32_t>(v, -1);
        if (i >= 0 && (unsigned)i < Z7_ARRAY_SIZE(k_Dictionaries))
          _dictionaryIndex = i;
      });
  AddProperty(L"Threads", [this] { return box_value(_threads); },
      [this](const CObject &v) { _threads = unbox_value_or<double>(v, _threads); });
  AddProperty(L"MaxThreads", [this] { return box_value((double)(_numProcessors * 2)); });
  AddProperty(L"IsRunning", [this] { return box_value(_isRunning); });
  AddProperty(L"CompressSpeed", [this] { return BoxString(_compressSpeed); });
  AddProperty(L"DecompressSpeed", [this] { return BoxString(_decompressSpeed); });
  AddProperty(L"AverageText", [this] { return BoxString(_averageText); });
  AddProperty(L"Error", [this] { return BoxString(_error); });
  AddProperty(L"HasError", [this] { return box_value(!_error.IsEmpty()); });
}

void CBenchmarkPage::Init()
{
  _root = NXaml::Load<FrameworkElement>(IDX_BENCHMARK_PAGE);
  NXaml::FindName<Controls::ItemsControl>(_root, L"DictionaryBox").ItemsSource(_dictionaryChoices);
  NXaml::FindName<Controls::ItemsControl>(_root, L"PassesList").ItemsSource(_passItems);
  _root.DataContext(*this);
  _startButton = NXaml::FindName<Controls::Button>(_root, L"StartButton");
  _startButton.Click([this](auto &&, auto &&) { StartAsync(); });
}

// ---- ITabPage ----

NPageKind::EEnum CBenchmarkPage::GetPageKind() { return NPageKind::kBenchmark; }

FrameworkElement CBenchmarkPage::GetRoot() { return _root; }

UString CBenchmarkPage::GetTabTitle() { return UString(L"Benchmark"); }

Controls::IconSource CBenchmarkPage::GetTabIcon()
{
  Controls::FontIconSource icon;
  icon.Glyph(L"\xEC4A");
  return icon;
}

UString CBenchmarkPage::GetArchivePath() { return UString(); }

void CBenchmarkPage::OnTabClosed()
{
  _closed = true;
  // (the C# page let a running benchmark finish in the background)
  Cancel();
}

// ---- benchmark ----

void CBenchmarkPage::Cancel()
{
  _cancelRequested = true;
  if (_context)
    _context->Cancel();
}

void CBenchmarkPage::SetIsRunning(bool value)
{
  _isRunning = value;
  RaisePropertyChanged(L"IsRunning");
  if (_startButton)
    _startButton.Content(box_value(hstring(value ? L"Stop" : L"Start")));
}

void CBenchmarkPage::SetError(const UString &error)
{
  _error = error;
  RaisePropertyChanged(L"Error");
  RaisePropertyChanged(L"HasError");
}

void CBenchmarkPage::AddPass(const CBenchmarkPass &pass)
{
  _passes.Add(pass);
  _passItems.Append(make<CBenchmarkPassItem>(pass));
  _compressSpeed = FormatSpeed(pass.CompressMBps);
  _decompressSpeed = FormatSpeed(pass.DecompressMBps);

  double compress = 0, decompress = 0, ratio = 0;
  FOR_VECTOR (i, _passes)
  {
    compress += _passes[i].CompressMBps;
    decompress += _passes[i].DecompressMBps;
    ratio += _passes[i].Ratio;
  }
  const double n = (double)_passes.Size();
  _averageText = L"Average of ";
  _averageText.Add_UInt32(_passes.Size());
  _averageText += L" passes: compress ";
  _averageText += FormatSpeed(compress / n);
  _averageText += L" \x00B7 decompress ";
  _averageText += FormatSpeed(decompress / n);
  _averageText += L" \x00B7 ratio ";
  _averageText += FormatPercent(ratio / n);

  RaisePropertyChanged(L"CompressSpeed");
  RaisePropertyChanged(L"DecompressSpeed");
  RaisePropertyChanged(L"AverageText");
}

fire_and_forget CBenchmarkPage::StartAsync()
{
  if (_isRunning)
  {
    Cancel();
    co_return;
  }
  auto strong = get_strong();
  SetError(UString());
  _passes.Clear();
  _passItems.Clear();
  SetIsRunning(true);
  _cancelRequested = false;
  const UInt64 dictionary = k_Dictionaries[_dictionaryIndex].Size;
  const UInt32 threads = (UInt32)_threads;
  HRESULT hr = S_OK;
  UString errorMessage;
  try
  {
    CByteBuffer data;
    co_await resume_background();
    NBenchmarkRunner::GenerateData((size_t)MyMax(dictionary * 2, (UInt64)32 << 20), data);
    co_await ResumeForeground(g_App.Dispatcher);

    for (UInt32 pass = 1; pass <= k_NumPasses && !_cancelRequested; pass++)
    {
      COperationContext context;
      CBenchmarkPass result;
      _context = &context; // Cancel() reaches the running pass through it
      co_await resume_background();
      hr = NBenchmarkRunner::RunPass(pass, data, dictionary, threads, context, result, errorMessage);
      co_await ResumeForeground(g_App.Dispatcher);
      _context = NULL;
      if (hr != S_OK)
        break;
      if (!_closed)
        AddPass(result);
    }
  }
  catch (const hresult_error &)
  {
    _context = NULL;
    co_return; // the UI thread is shutting down
  }
  if (hr != S_OK && hr != E_ABORT && !_closed)
  {
    if (errorMessage.IsEmpty())
      FormatHResultMessage(hr, UString(L"Benchmark"), errorMessage);
    SetError(errorMessage);
  }
  SetIsRunning(false);
}
