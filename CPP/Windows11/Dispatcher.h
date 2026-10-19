// Windows11/Dispatcher.h

#ifndef ZIP7_INC_WINDOWS11_DISPATCHER_H
#define ZIP7_INC_WINDOWS11_DISPATCHER_H

#include "WinUI.h"

namespace NWindows11 {

/*
co_await ResumeForeground(queue) continues a coroutine on the thread of the
DispatcherQueue (the UI thread), like co_await winrt::resume_background()
moves it to the thread pool. It throws if the queue has been shut down.
*/

class CResumeForeground
{
  winrt::Microsoft::UI::Dispatching::DispatcherQueue _queue;
  winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority _priority;
  bool _failed;
public:
  CResumeForeground(const winrt::Microsoft::UI::Dispatching::DispatcherQueue &queue,
      winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority priority):
      _queue(queue), _priority(priority), _failed(false)
      {}

  bool await_ready() const { return _queue.HasThreadAccess(); }

  bool await_suspend(std::coroutine_handle<> handle)
  {
    if (_queue.TryEnqueue(_priority, [handle] { handle(); }))
      return true;
    _failed = true;
    return false;
  }

  void await_resume() const
  {
    if (_failed)
      throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_INVALID_STATE), L"The UI thread is shutting down");
  }
};

inline CResumeForeground ResumeForeground(const winrt::Microsoft::UI::Dispatching::DispatcherQueue &queue,
    winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority priority =
        winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal)
{
  return CResumeForeground(queue, priority);
}

}

#endif
