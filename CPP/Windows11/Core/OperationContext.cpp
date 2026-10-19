// OperationContext.cpp

#include "StdAfx.h"

#include "OperationContext.h"

#include "../../Common/IntToString.h"

#include "../../Windows/ErrorMsg.h"

using namespace NWindows;
using namespace NSynchronization;

double COperationProgress::GetFraction() const
{
  if (TotalBytes == 0)
    return 0;
  const double f = (double)CompletedBytes / (double)TotalBytes;
  return f < 0 ? 0 : f > 1 ? 1 : f;
}

double COperationProgress::GetBytesPerSecond() const
{
  if (ElapsedMs < 250)
    return 0;
  return (double)CompletedBytes * 1000 / (double)ElapsedMs;
}

bool COperationProgress::GetRemainingSeconds(UInt64 &seconds) const
{
  seconds = 0;
  const double speed = GetBytesPerSecond();
  if (speed <= 0 || CompletedBytes == 0 || TotalBytes <= CompletedBytes)
    return false;
  seconds = (UInt64)((double)(TotalBytes - CompletedBytes) / speed);
  return true;
}

bool COperationProgress::GetRatio(double &ratio) const
{
  ratio = 0;
  if (CompletedBytes == 0 || OutputBytes == 0)
    return false;
  ratio = (double)OutputBytes / (double)CompletedBytes;
  return true;
}


COperationContext::COperationContext(IOperationProgress *progress, IOperationPrompts *prompts):
    _cancelled(0),
    _paused(0),
    _pausedTotal(0),
    _pauseStarted(0),
    _lastReportTick(0),
    Progress(progress),
    Prompts(prompts),
    TotalBytes(0),
    CompletedBytes(0),
    TotalFiles(0),
    CompletedFiles(0),
    OutputBytes(0),
    ReportIntervalMs(100)
{
  _runGate.Create(true);
  _cancelEvent.Create(false);
  _startTick = ::GetTickCount64();
}

void COperationContext::SetCurrentItem(const UString &item)
{
  CCriticalSectionLock lock(_cs);
  _currentItem = item;
}

UString COperationContext::GetCurrentItem() const
{
  CCriticalSectionLock lock(_cs);
  return _currentItem;
}

static LONG LoadLong(const LONG &v)
{
  return ::InterlockedCompareExchange(const_cast<LONG *>(&v), 0, 0);
}

bool COperationContext::IsCancelled() const { return LoadLong(_cancelled) != 0; }
bool COperationContext::IsPaused() const { return LoadLong(_paused) != 0; }

void COperationContext::Cancel()
{
  ::InterlockedExchange(&_cancelled, 1);
  _cancelEvent.Set();
}

void COperationContext::Pause()
{
  CCriticalSectionLock lock(_cs);
  if (_paused == 0)
  {
    _pauseStarted = ::GetTickCount64();
    ::InterlockedExchange(&_paused, 1);
    _runGate.Reset();
  }
}

void COperationContext::Resume()
{
  CCriticalSectionLock lock(_cs);
  if (_paused != 0)
  {
    _pausedTotal += ::GetTickCount64() - _pauseStarted;
    ::InterlockedExchange(&_paused, 0);
    _runGate.Set();
  }
}

void COperationContext::AddMessage(NMessageSeverity::EEnum severity, const UString &text, const UString &item)
{
  CCriticalSectionLock lock(_cs);
  COperationMessage &m = _messages.AddNew();
  m.Severity = severity;
  m.Text = text;
  m.ItemPath = item;
}

void COperationContext::GetMessages(CObjectVector<COperationMessage> &messages) const
{
  CCriticalSectionLock lock(_cs);
  messages = _messages;
}

bool COperationContext::HasErrors() const
{
  CCriticalSectionLock lock(_cs);
  FOR_VECTOR (i, _messages)
    if (_messages[i].Severity == NMessageSeverity::kError)
      return true;
  return false;
}

UInt64 COperationContext::GetElapsedMs() const
{
  CCriticalSectionLock lock(_cs);
  const UInt64 now = ::GetTickCount64();
  UInt64 paused = _pausedTotal;
  if (IsPaused())
    paused += now - _pauseStarted;
  const UInt64 total = now - _startTick;
  return total > paused ? total - paused : 0;
}

HRESULT COperationContext::Checkpoint()
{
  if (IsPaused() && !IsCancelled())
  {
    HANDLE handles[2] = { _runGate, _cancelEvent };
    ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
  }
  return IsCancelled() ? E_ABORT : S_OK;
}

void COperationContext::GetProgress(COperationProgress &p) const
{
  p.TotalBytes = TotalBytes;
  p.CompletedBytes = CompletedBytes;
  p.TotalFiles = TotalFiles;
  p.CompletedFiles = CompletedFiles;
  p.OutputBytes = OutputBytes;
  p.CurrentItem = GetCurrentItem();
  p.ElapsedMs = GetElapsedMs();
}

void COperationContext::Report(bool force)
{
  if (!Progress)
    return;
  const UInt64 now = ::GetTickCount64();
  if (!force && now - _lastReportTick < ReportIntervalMs)
    return;
  _lastReportTick = now;
  COperationProgress p;
  GetProgress(p);
  Progress->OnProgress(p);
}


void FormatHResultMessage(HRESULT hr, const UString &context, UString &message)
{
  UString detail;
  switch (hr)
  {
    case E_ABORT: detail = "The operation was cancelled."; break;
    case E_OUTOFMEMORY: detail = "There is not enough memory for this operation."; break;
    case E_NOTIMPL: detail = "The operation is not supported for this archive type."; break;
    case E_INVALIDARG: detail = "An option is not valid for this archive type."; break;
    case S_FALSE:
    case k_HRESULT_IsNotArc:
      detail = "The data is not a supported archive or is damaged.";
      break;
    case k_HRESULT_WrongPassword: detail = "The password is incorrect."; break;
    default:
    {
      detail = NError::MyFormatMessage(hr);
      detail.Trim();
      if (detail.IsEmpty())
      {
        char s[16];
        ConvertUInt32ToHex8Digits((UInt32)hr, s);
        detail = "Error 0x";
        detail += s;
      }
    }
  }
  message = context;
  if (!message.IsEmpty())
    message += ": ";
  message += detail;
}
