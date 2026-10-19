// OperationViewModel.h

#ifndef ZIP7_INC_OPERATION_VIEW_MODEL_H
#define ZIP7_INC_OPERATION_VIEW_MODEL_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../../Windows/Synchronization.h"

#include "../Bindable.h"
#include "../Command.h"

#include "../Core/OperationContext.h"

class CDialogService;
class CUiPrompts;

namespace NOperationState
{
  enum EEnum
  {
    kRunning,
    kPaused,
    kSucceeded,
    kCompletedWithErrors,
    kFailed,
    kCancelled
  };
}

/*
One running engine job, shown in the Activity flyout of the main window or in
the compact OperationWindow. Create it on the UI thread:
  auto op = winrt::make_self<COperationViewModel>(NOperationKind::kExtract, title, &host->Dialogs(), archivePath);
and run it with COperationService::RunAsync(). Context is the COperationContext
to pass to the Core call; its progress goes to this object (worker thread) and
is shown on the UI thread; its prompts are dialogs of the window (dialogs ==
NULL: no prompts, e.g. hashing).

Bindable properties ({Binding Name}):
  Title, Detail, Percent (double 0..100), IsIndeterminate, Stats, Summary,
  IsActive, IsFinished, IsPaused, PauseGlyph, PauseLabel, EfficiencyMode (TwoWay),
  TogglePauseCommand, CancelCommand, RevealCommand
All methods except Context's thread-safe ones are for the UI thread.
*/

class COperationViewModel:
  public NWindows11::CBindableT<COperationViewModel>,
  public IOperationProgress
{
  CUiPrompts *_prompts;
public:
  const NOperationKind::EEnum Kind;
  COperationContext Context;
  // copied from Context when the operation completes
  CObjectVector<COperationMessage> Messages;

private:
  winrt::Microsoft::UI::Dispatching::DispatcherQueue _dispatcher;
  UString _title;
  UString _detail;
  double _percent;
  bool _isIndeterminate;
  UString _stats;
  UString _summary;
  UString _errorMessage;
  NOperationState::EEnum _state;
  UString _resultPath;
  bool _efficiency;
  bool _efficiencyEntered;
  bool _started;

  NWindows::NSynchronization::CCriticalSection _progressCs;
  COperationProgress _pendingProgress;
  bool _progressPending;

  winrt::com_ptr<NWindows11::CCommand> _togglePauseCommand;
  winrt::com_ptr<NWindows11::CCommand> _cancelCommand;
  winrt::com_ptr<NWindows11::CCommand> _revealCommand;

  winrt::event<winrt::delegate<> > _progressChanged;
  winrt::event<winrt::delegate<> > _stateChanged;

  void ApplyProgress(const COperationProgress &p);
  void SetState(NOperationState::EEnum state);
  void UpdateEfficiency();

public:
  Z7_IFACE_IMP(IOperationProgress)

  COperationViewModel(NOperationKind::EEnum kind, const UString &title,
      CDialogService *dialogs = NULL, const UString &archivePath = UString());
  ~COperationViewModel();

  const UString &GetTitle() const { return _title; }
  void SetTitle(const UString &title);
  const UString &GetDetail() const { return _detail; }
  double GetPercent() const { return _percent; }
  bool GetIsIndeterminate() const { return _isIndeterminate; }
  const UString &GetStats() const { return _stats; }
  // after completion: the describe() text, "Cancelled", or the error message
  const UString &GetSummary() const { return _summary; }
  // the error text of a failed operation (C# exception message), else ""
  const UString &GetErrorMessage() const { return _errorMessage; }
  NOperationState::EEnum GetState() const { return _state; }
  // destination folder or output file to reveal when done ("" = none)
  const UString &GetResultPath() const { return _resultPath; }
  void SetResultPath(const UString &path) { _resultPath = path; }

  bool IsActive() const { return _state == NOperationState::kRunning || _state == NOperationState::kPaused; }
  bool IsFinished() const { return !IsActive(); }
  bool IsPaused() const { return _state == NOperationState::kPaused; }

  // Windows 11 efficiency mode (EcoQoS + idle priority) while the operation runs
  bool GetEfficiencyMode() const { return _efficiency; }
  void SetEfficiencyMode(bool value);

  void TogglePause();
  void Cancel();
  void Reveal();

  // UI thread events: progress was applied / State changed
  winrt::event_token ProgressChanged(const winrt::delegate<> &handler) { return _progressChanged.add(handler); }
  void ProgressChanged(const winrt::event_token &token) { _progressChanged.remove(token); }
  winrt::event_token StateChanged(const winrt::delegate<> &handler) { return _stateChanged.add(handler); }
  void StateChanged(const winrt::event_token &token) { _stateChanged.remove(token); }

  // used by COperationService
  void Start();
  void Complete(NOperationState::EEnum state, const UString &summary, const UString &errorMessage = UString());
};

#endif
