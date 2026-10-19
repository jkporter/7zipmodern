// OperationViewModel.cpp

#include "StdAfx.h"

#include "OperationViewModel.h"

#include "../../Windows/FileFind.h"

#include "../HString.h"

#include "../Core/PropertyFormatter.h"

#include "DialogService.h"
#include "PlatformServices.h"
#include "ShellServices.h"

using namespace winrt;
using namespace NWindows11;

#ifdef _MSC_VER
#pragma warning(disable : 4355) // 'this' used in base member initializer list (Context only stores the pointer)
#endif

COperationViewModel::COperationViewModel(NOperationKind::EEnum kind, const UString &title,
    CDialogService *dialogs, const UString &archivePath):
    _prompts(dialogs ? new CUiPrompts(dialogs, archivePath) : NULL),
    Kind(kind),
    Context(this, _prompts),
    _dispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()),
    _title(title),
    _detail(L"Preparing\x2026"),
    _percent(0),
    _isIndeterminate(true),
    _state(NOperationState::kRunning),
    _efficiency(false),
    _efficiencyEntered(false),
    _started(false),
    _progressPending(false)
{
  if (_prompts)
    _prompts->SetContext(&Context);

  _togglePauseCommand = make_self<CCommand>([this] { TogglePause(); });
  _cancelCommand = make_self<CCommand>([this] { Cancel(); });
  _revealCommand = make_self<CCommand>([this] { Reveal(); });

  AddProperty(L"Title", [this] { return BoxString(_title); });
  AddProperty(L"Detail", [this] { return BoxString(_detail); });
  AddProperty(L"Percent", [this] { return box_value(_percent); });
  AddProperty(L"IsIndeterminate", [this] { return box_value(_isIndeterminate); });
  AddProperty(L"Stats", [this] { return BoxString(_stats); });
  AddProperty(L"Summary", [this] { return BoxString(_summary); });
  AddProperty(L"IsActive", [this] { return box_value(IsActive()); });
  AddProperty(L"IsFinished", [this] { return box_value(IsFinished()); });
  AddProperty(L"IsPaused", [this] { return box_value(IsPaused()); });
  AddProperty(L"PauseGlyph", [this] { return box_value(IsPaused() ? L"\xE768" : L"\xE769"); });
  AddProperty(L"PauseLabel", [this] { return box_value(IsPaused() ? L"Resume" : L"Pause"); });
  AddProperty(L"EfficiencyMode", [this] { return box_value(_efficiency); },
      [this](const CInspectable &v) { SetEfficiencyMode(unbox_value_or<bool>(v, false)); });
  AddProperty(L"TogglePauseCommand", [this] { return _togglePauseCommand.as<CInspectable>(); });
  AddProperty(L"CancelCommand", [this] { return _cancelCommand.as<CInspectable>(); });
  AddProperty(L"RevealCommand", [this] { return _revealCommand.as<CInspectable>(); });
}

COperationViewModel::~COperationViewModel()
{
  if (_efficiencyEntered)
    NEfficiencyMode::Leave();
  delete _prompts;
}

void COperationViewModel::SetTitle(const UString &title)
{
  _title = title;
  RaisePropertyChanged(L"Title");
}

void COperationViewModel::UpdateEfficiency()
{
  const bool want = _efficiency && _started && IsActive();
  if (want == _efficiencyEntered)
    return;
  _efficiencyEntered = want;
  if (want)
    NEfficiencyMode::Enter();
  else
    NEfficiencyMode::Leave();
}

void COperationViewModel::SetEfficiencyMode(bool value)
{
  if (_efficiency == value)
    return;
  _efficiency = value;
  UpdateEfficiency(); // before Start() the mode is only remembered
  RaisePropertyChanged(L"EfficiencyMode");
}

void COperationViewModel::SetState(NOperationState::EEnum state)
{
  if (_state == state)
    return;
  _state = state;
  RaisePropertyChanged(L"IsActive");
  RaisePropertyChanged(L"IsFinished");
  RaisePropertyChanged(L"IsPaused");
  RaisePropertyChanged(L"PauseGlyph");
  RaisePropertyChanged(L"PauseLabel");
  _stateChanged();
}

void COperationViewModel::TogglePause()
{
  if (_state == NOperationState::kRunning)
  {
    Context.Pause();
    SetState(NOperationState::kPaused);
  }
  else if (_state == NOperationState::kPaused)
  {
    Context.Resume();
    SetState(NOperationState::kRunning);
  }
}

void COperationViewModel::Cancel()
{
  // a cancel while paused wakes the pause gate too
  Context.Cancel();
}

void COperationViewModel::Reveal()
{
  if (_resultPath.IsEmpty())
    return;
  if (Kind == NOperationKind::kExtract && NWindows::NFile::NFind::DoesDirExist_FollowLink(us2fs(_resultPath)))
    NShellActions::OpenFolder(_resultPath);
  else
    NShellActions::RevealInExplorer(_resultPath);
}


// worker thread: keep the newest snapshot and post one update at a time to the UI thread

void COperationViewModel::OnProgress(const COperationProgress &progress)
{
  bool post;
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_progressCs);
    _pendingProgress = progress;
    post = !_progressPending;
    _progressPending = true;
  }
  if (!post || !_dispatcher)
    return;
  com_ptr<COperationViewModel> strong = get_strong();
  if (!_dispatcher.TryEnqueue([strong]
      {
        COperationProgress p;
        {
          NWindows::NSynchronization::CCriticalSectionLock lock(strong->_progressCs);
          p = strong->_pendingProgress;
          strong->_progressPending = false;
        }
        strong->ApplyProgress(p);
      }))
  {
    NWindows::NSynchronization::CCriticalSectionLock lock(_progressCs);
    _progressPending = false;
  }
}


static void AddPart(UString &s, const UString &part)
{
  if (!s.IsEmpty())
    s += L" \x00B7 ";
  s += part;
}

void COperationViewModel::ApplyProgress(const COperationProgress &p)
{
  if (!IsActive())
    return;
  if (p.TotalBytes > 0)
  {
    if (_isIndeterminate)
    {
      _isIndeterminate = false;
      RaisePropertyChanged(L"IsIndeterminate");
    }
    _percent = p.GetFraction() * 100;
    RaisePropertyChanged(L"Percent");
  }
  if (!p.CurrentItem.IsEmpty() && p.CurrentItem != _detail)
  {
    _detail = p.CurrentItem;
    RaisePropertyChanged(L"Detail");
  }

  UString stats;
  if (p.TotalBytes > 0)
  {
    UString s = NPropertyFormatter::FormatSize(p.CompletedBytes);
    s += " of ";
    s += NPropertyFormatter::FormatSize(p.TotalBytes);
    AddPart(stats, s);
  }
  const double speed = p.GetBytesPerSecond();
  if (speed > 0)
  {
    UString s = NPropertyFormatter::FormatSize((UInt64)speed);
    s += "/s";
    AddPart(stats, s);
  }
  UInt64 remaining;
  if (p.GetRemainingSeconds(remaining))
  {
    UString s;
    if (remaining >= 3600)
    {
      s.Add_UInt64(remaining / 3600);
      s += "h ";
      s.Add_UInt64(remaining / 60 % 60);
      s += "m left";
    }
    else if (remaining >= 60)
    {
      s.Add_UInt64(remaining / 60);
      s += " min ";
      s.Add_UInt64(remaining % 60);
      s += "s left";
    }
    else
    {
      s.Add_UInt64(remaining < 1 ? 1 : remaining);
      s += " s left";
    }
    AddPart(stats, s);
  }
  double ratio;
  if ((Kind == NOperationKind::kCompress || Kind == NOperationKind::kUpdate) && p.GetRatio(ratio))
  {
    UString s (L"ratio ");
    s.Add_UInt64((UInt64)(ratio * 100 + 0.5));
    s += "%";
    AddPart(stats, s);
  }
  if (p.TotalFiles > 1)
  {
    UString s = NPropertyFormatter::FormatNumber(p.CompletedFiles);
    s += " / ";
    s += NPropertyFormatter::FormatCount((Int64)p.TotalFiles, L"file");
    AddPart(stats, s);
  }
  if (stats != _stats)
  {
    _stats = stats;
    RaisePropertyChanged(L"Stats");
  }
  _progressChanged();
}


void COperationViewModel::Start()
{
  _started = true;
  UpdateEfficiency();
}

void COperationViewModel::Complete(NOperationState::EEnum state, const UString &summary, const UString &errorMessage)
{
  Context.GetMessages(Messages);
  if (state == NOperationState::kSucceeded && Context.HasErrors())
    state = NOperationState::kCompletedWithErrors;
  _summary = summary;
  _errorMessage = errorMessage;
  if (_isIndeterminate)
  {
    _isIndeterminate = false;
    RaisePropertyChanged(L"IsIndeterminate");
  }
  if (state == NOperationState::kSucceeded || state == NOperationState::kCompletedWithErrors)
  {
    _percent = 100;
    RaisePropertyChanged(L"Percent");
  }
  _detail = summary;
  RaisePropertyChanged(L"Summary");
  RaisePropertyChanged(L"Detail");
  _state = state;
  UpdateEfficiency();
  _state = NOperationState::kRunning; // SetState() raises the notifications
  SetState(state);
}
