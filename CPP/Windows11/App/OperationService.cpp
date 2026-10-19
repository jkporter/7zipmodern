// OperationService.cpp

#include "StdAfx.h"

#include "OperationService.h"

#include "../Dispatcher.h"
#include "../HString.h"

#include "App.h"

using namespace winrt;
using namespace winrt::Windows::Foundation;

COperationService::COperationService():
    _hwnd(NULL),
    _operations(single_threaded_observable_vector<IInspectable>())
    {}

unsigned COperationService::GetNumOperations() const
{
  return _operations.Size();
}

COperationViewModel *COperationService::GetOperation(unsigned index) const
{
  return get_self<COperationViewModel>(
      _operations.GetAt(index).as<winrt::Microsoft::UI::Xaml::Data::ICustomPropertyProvider>());
}

bool COperationService::HasActive() const
{
  const unsigned num = GetNumOperations();
  for (unsigned i = 0; i < num; i++)
    if (GetOperation(i)->IsActive())
      return true;
  return false;
}

void COperationService::CancelAll()
{
  const unsigned num = GetNumOperations();
  for (unsigned i = 0; i < num; i++)
  {
    COperationViewModel *op = GetOperation(i);
    if (op->IsActive())
      op->Cancel();
  }
}

void COperationService::Dismiss(COperationViewModel *op)
{
  if (!op->IsFinished())
    return;
  const unsigned num = GetNumOperations();
  for (unsigned i = 0; i < num; i++)
    if (GetOperation(i) == op)
    {
      _operations.RemoveAt(i);
      return;
    }
}

void COperationService::UpdateTaskbar()
{
  unsigned numActive = 0;
  bool indeterminate = false;
  bool allPaused = true;
  double sum = 0;
  const unsigned num = GetNumOperations();
  for (unsigned i = 0; i < num; i++)
  {
    const COperationViewModel *op = GetOperation(i);
    if (!op->IsActive())
      continue;
    numActive++;
    if (op->GetIsIndeterminate())
      indeterminate = true;
    if (!op->IsPaused())
      allPaused = false;
    sum += op->GetPercent() / 100;
  }
  if (numActive == 0)
  {
    _taskbar.Clear(_hwnd);
    return;
  }
  _taskbar.Set(_hwnd, indeterminate ? -1 : sum / numActive, allPaused);
}

void COperationService::Notify(COperationViewModel &op)
{
  if ((_hwnd && ::GetForegroundWindow() == _hwnd) || !g_App.Settings.Current.NotifyOnCompletion)
    return;
  NNotifications::ShowCompleted(op.GetTitle(), op.GetSummary(), op.GetResultPath(),
      op.GetState() == NOperationState::kFailed);
}

fire_and_forget COperationService::RemoveLater(com_ptr<COperationViewModel> op)
{
  const bool ok = (op->GetState() == NOperationState::kSucceeded);
  co_await resume_after(std::chrono::seconds(ok ? 20 : 600));
  try
  {
    co_await NWindows11::ResumeForeground(g_App.Dispatcher);
  }
  catch (...)
  {
    co_return;
  }
  const unsigned num = GetNumOperations();
  for (unsigned i = 0; i < num; i++)
    if (GetOperation(i) == op.get())
    {
      _operations.RemoveAt(i);
      break;
    }
}


IAsyncOperation<int32_t> COperationService::RunAsync(com_ptr<COperationViewModel> op,
    COperationWork work, COperationDescribe describe, COperationDescribe resultPath)
{
  _operations.InsertAt(0, op.as<IInspectable>());
  const event_token progressToken = op->ProgressChanged([this] { UpdateTaskbar(); });
  const event_token stateToken = op->StateChanged([this] { UpdateTaskbar(); });
  op->Start();
  UpdateTaskbar();

  const winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher = g_App.Dispatcher;
  HRESULT hr = S_OK;
  UString errorMessage;
  {
    COperationContext &ctx = op->Context;
    co_await resume_background();
    try
    {
      hr = work(ctx, errorMessage);
    }
    catch (const std::bad_alloc &)
    {
      hr = E_OUTOFMEMORY;
    }
    catch (const hresult_error &e)
    {
      hr = e.code();
      errorMessage = NWindows11::ToUString(e.message());
    }
    catch (...)
    {
      hr = E_FAIL;
    }
    if (hr != S_OK && hr != E_ABORT && ctx.IsCancelled())
      hr = E_ABORT;
    co_await NWindows11::ResumeForeground(dispatcher);
  }

  if (hr == S_OK)
  {
    if (resultPath)
      op->SetResultPath(resultPath());
    op->Complete(NOperationState::kSucceeded, describe ? describe() : UString());
    Notify(*op);
  }
  else if (hr == E_ABORT)
    op->Complete(NOperationState::kCancelled, UString(L"Cancelled"));
  else
  {
    if (errorMessage.IsEmpty())
      FormatHResultMessage(hr, op->GetTitle(), errorMessage);
    op->Complete(NOperationState::kFailed, errorMessage, errorMessage);
    Notify(*op);
  }

  op->ProgressChanged(progressToken);
  op->StateChanged(stateToken);
  UpdateTaskbar();
  // finished jobs stay listed for a while so the user can see the outcome
  RemoveLater(op);
  co_return (int32_t)hr;
}
