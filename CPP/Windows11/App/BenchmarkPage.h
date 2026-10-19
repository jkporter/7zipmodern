// BenchmarkPage.h

#ifndef ZIP7_INC_BENCHMARK_PAGE_H
#define ZIP7_INC_BENCHMARK_PAGE_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Bindable.h"

#include "../Core/BenchmarkRunner.h"

#include "CompressViewModel.h"
#include "WindowHost.h"

/*
The "Benchmark" tab (C# BenchmarkPage + BenchmarkViewModel). The page is the
DataContext of its markup. Bindable:
  DictionaryIndex (TwoWay), Threads (TwoWay, double), MaxThreads, IsRunning,
  CompressSpeed, DecompressSpeed, AverageText, Error, HasError.
Passes items: PassLabel, CompressText, DecompressText, RatioText.
*/

class CBenchmarkPage:
  public NWindows11::CBindableT<CBenchmarkPage>,
  public ITabPage
{
  winrt::Microsoft::UI::Xaml::FrameworkElement _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Button _startButton { nullptr };
  CUiItemVector _dictionaryChoices;
  CUiItemVector _passItems;
  CRecordVector<CBenchmarkPass> _passes;
  int _dictionaryIndex;
  double _threads;
  UInt32 _numProcessors;
  bool _isRunning;
  bool _closed;
  bool _cancelRequested;
  COperationContext *_context; // the running pass (UI thread)
  UString _compressSpeed;
  UString _decompressSpeed;
  UString _averageText;
  UString _error;

  void SetIsRunning(bool value);
  void SetError(const UString &error);
  void AddPass(const CBenchmarkPass &pass);
  void Cancel();
  winrt::fire_and_forget StartAsync();

public:
  Z7_IFACE_IMP(ITabPage)

  CBenchmarkPage();
  void Init();
};

#endif
