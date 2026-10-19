// DialogService.h

#ifndef ZIP7_INC_DIALOG_SERVICE_H
#define ZIP7_INC_DIALOG_SERVICE_H

#include "../../Common/MyString.h"
#include "../../Common/MyVector.h"

#include "../Core/OperationContext.h"

/*
ContentDialog helpers bound to one window (C# DialogService).
The coroutines can be started on any thread: they switch to the window's UI
thread first. Dialogs are serialised: a second dialog waits until the first
one is closed (WinUI allows only one open ContentDialog per XamlRoot).
The string parameters are taken by value because the coroutines outlive the
caller's arguments.
*/

typedef std::function<UString(const UString &text)> CTextValidator; // returns an error message, "" = valid

class CDialogService
{
  winrt::Microsoft::UI::Xaml::UIElement _root { nullptr };
  winrt::Microsoft::UI::Dispatching::DispatcherQueue _dispatcher { nullptr };
  HWND _hwnd;
  bool _busy;
  std::vector<std::coroutine_handle<> > _waiters;

  struct CGateAwaiter
  {
    CDialogService *Service;
    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume() const {}
  };
  CGateAwaiter EnterGate() { CGateAwaiter a; a.Service = this; return a; }
  void LeaveGate();

  Z7_CLASS_NO_COPY(CDialogService)
public:
  CDialogService(): _hwnd(NULL), _busy(false) {}

  // root: an element of the window's content (its XamlRoot hosts the dialogs)
  void Init(const winrt::Microsoft::UI::Xaml::UIElement &root, HWND hwnd);

  HWND GetHwnd() const { return _hwnd; }
  winrt::Microsoft::UI::Dispatching::DispatcherQueue GetDispatcher() const { return _dispatcher; }

  // Sets XamlRoot, the default style and the theme of the window, then shows the dialog.
  winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
      ShowAsync(winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog);

  // true = the primary button was pressed
  winrt::Windows::Foundation::IAsyncOperation<bool> ConfirmAsync(UString title, UString message,
      UString primary, bool destructive = false);

  // A message with a Close button (used for errors and for information).
  winrt::Windows::Foundation::IAsyncAction ShowErrorAsync(UString title, UString message);

  // Returns the trimmed text, or "" if the user cancelled (an empty text can't be confirmed).
  winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PromptTextAsync(UString title, UString label,
      UString initial, UString primary, CTextValidator validate = nullptr);

  // Lists warnings and errors after an operation (replaces the classic Messages dialog).
  winrt::Windows::Foundation::IAsyncAction ShowMessagesAsync(UString title, CObjectVector<COperationMessage> messages);
};


/*
Engine prompts (password, overwrite) for one operation, shown as dialogs of
the window. The engine calls them on the worker thread; they block until the
user answers, or until the operation is cancelled.
archivePath (may be empty) enables the saved password (Windows Hello) and
"Remember for this archive".
COperationViewModel creates it; pages don't need it directly.
*/

class CUiPrompts Z7_final: public IOperationPrompts
{
  CDialogService *_dialogs;
  UString _archivePath;
  const COperationContext *_context;
  bool _triedVault;
  unsigned _asked;

  Z7_CLASS_NO_COPY(CUiPrompts)
public:
  Z7_IFACE_IMP(IOperationPrompts)

  CUiPrompts(CDialogService *dialogs, const UString &archivePath):
      _dialogs(dialogs), _archivePath(archivePath), _context(NULL),
      _triedVault(false), _asked(0)
      {}
  void SetContext(const COperationContext *context) { _context = context; }
};

#endif
