// Windows11/Command.h

#ifndef ZIP7_INC_WINDOWS11_COMMAND_H
#define ZIP7_INC_WINDOWS11_COMMAND_H

#include "WinUI.h"

namespace NWindows11 {

typedef std::function<void()> CCommandAction;
typedef std::function<bool()> CCommandPredicate;

/*
ICommand for {Binding ...Command}: runs Action on the UI thread.
Call RaiseCanExecuteChanged() when the CanExecute() result may change.
*/

class CCommand: public winrt::implements<CCommand, winrt::Microsoft::UI::Xaml::Input::ICommand>
{
  CCommandAction _action;
  CCommandPredicate _canExecute;
  winrt::event<winrt::Windows::Foundation::EventHandler<winrt::Windows::Foundation::IInspectable> > _canExecuteChanged;
public:
  CCommand(const CCommandAction &action, const CCommandPredicate &canExecute = nullptr):
      _action(action), _canExecute(canExecute)
      {}

  void RaiseCanExecuteChanged() { _canExecuteChanged(*this, nullptr); }

  bool CanExecute(const winrt::Windows::Foundation::IInspectable &) { return !_canExecute || _canExecute(); }
  void Execute(const winrt::Windows::Foundation::IInspectable &)
  {
    if (CanExecute(nullptr) && _action)
      _action();
  }
  winrt::event_token CanExecuteChanged(
      const winrt::Windows::Foundation::EventHandler<winrt::Windows::Foundation::IInspectable> &handler)
  {
    return _canExecuteChanged.add(handler);
  }
  void CanExecuteChanged(const winrt::event_token &token) noexcept { _canExecuteChanged.remove(token); }
};

}

#endif
