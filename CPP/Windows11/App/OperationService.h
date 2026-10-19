// OperationService.h

#ifndef ZIP7_INC_OPERATION_SERVICE_H
#define ZIP7_INC_OPERATION_SERVICE_H

#include "../../Common/MyString.h"

#include "OperationViewModel.h"
#include "PlatformServices.h"

/*
Runs engine work off the UI thread and mirrors progress to the taskbar button
and, when the window isn't in front, to a completion notification (C# OperationService).
Several jobs can run at once; nothing is modal. One instance per window.

  CExtractResult result;
  auto op = winrt::make_self<COperationViewModel>(NOperationKind::kTest, L"Testing a.7z", &host->Dialogs(), path);
  const HRESULT hr = co_await host->Operations().RunAsync(op,
      [&](COperationContext &ctx, UString &errorMessage)
        { return NExtractEngine::Test(*session, NULL, ctx, result, errorMessage); },
      [&] { return UString(L"No errors"); },    // describe: summary on success (optional)
      [&] { return result.Destination; });      // resultPath: what "Show in folder" reveals (optional)

RunAsync() must be started (and awaited) on the UI thread. The work function
runs on a thread-pool thread; describe/resultPath run on the UI thread after
the work succeeded. Locals of the awaiting coroutine can be captured by
reference, because the caller is suspended until RunAsync() completes.
Result (an HRESULT as int32_t):
  S_OK     success: op state Succeeded or CompletedWithErrors (Context has error messages)
  E_ABORT  cancelled: op state Cancelled
  other    failed: op state Failed, op->GetErrorMessage() is the text for the user
           (from the work function's errorMessage, or FormatHResultMessage)
Finished operations stay in the list for a while (20 s after success, 10 min
otherwise), so the user can see the outcome in the Activity flyout.
*/

typedef std::function<HRESULT(COperationContext &context, UString &errorMessage)> COperationWork;
typedef std::function<UString()> COperationDescribe;

class COperationService
{
  HWND _hwnd;
  CTaskbarProgress _taskbar;
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable> _operations;

  winrt::fire_and_forget RemoveLater(winrt::com_ptr<COperationViewModel> op);
  void Notify(COperationViewModel &op);

  Z7_CLASS_NO_COPY(COperationService)
public:
  COperationService();
  void Init(HWND hwnd) { _hwnd = hwnd; }

  // COperationViewModel objects, newest first (ItemsSource of the Activity list)
  winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
      GetOperations() const { return _operations; }
  unsigned GetNumOperations() const;
  COperationViewModel *GetOperation(unsigned index) const;
  bool HasActive() const;
  void CancelAll();
  void Dismiss(COperationViewModel *op); // removes a finished operation from the list
  void UpdateTaskbar();

  winrt::Windows::Foundation::IAsyncOperation<int32_t> RunAsync(winrt::com_ptr<COperationViewModel> op,
      COperationWork work, COperationDescribe describe = nullptr, COperationDescribe resultPath = nullptr);
};

#endif
