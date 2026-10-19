// OperationWindow.h

#ifndef ZIP7_INC_OPERATION_WINDOW_H
#define ZIP7_INC_OPERATION_WINDOW_H

#include "../Core/ArchiveSession.h"
#include "../Core/ExtractOptions.h"

#include "DialogService.h"
#include "LaunchRequest.h"
#include "OperationService.h"
#include "PageFactory.h"
#include "WindowHost.h"

/*
Small window for verbs started from File Explorer (successor of 7zG's progress
dialog): optional options step, live progress with pause / cancel / efficiency
mode, then a result summary. It runs in its own process.
*/

typedef std::function<FString(const CArchiveSession &session)> CDestinationFunc;

class COperationWindow Z7_final: public IWindowHost
{
  CLaunchRequest _request;
  winrt::Microsoft::UI::Xaml::Window _window;
  winrt::Microsoft::UI::Xaml::Controls::Grid _root { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Image _titleIcon { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _titleBarText { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _titleText { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ContentPresenter _optionsHost { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::StackPanel _progressPanel { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _detailText { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ProgressBar _progress { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::TextBlock _statsText { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::InfoBar _resultBar { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::ListView _messageList { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton _efficiencyToggle { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Button _primaryButton { nullptr };
  winrt::Microsoft::UI::Xaml::Controls::Button _secondaryButton { nullptr };
  HWND _hwnd;
  CDialogService _dialogs;
  COperationService _operations;

  winrt::com_ptr<COperationViewModel> _current;
  winrt::event_token _progressToken;
  CExtractOptionsPanelRef _optionsPanel;
  bool _optionsMode;
  bool _started;
  bool _finished;
  bool _closed;
  UString _revealPath;
  CObjectVector<COperationMessage> _messages;

  void ResizeClient(int width, int height);
  void SetButtonText(winrt::Microsoft::UI::Xaml::Controls::Button &button, const wchar_t *text);
  winrt::com_ptr<COperationViewModel> Begin(NOperationKind::EEnum kind, const UString &title, const UString &archivePath);
  void OnProgress();
  void End(COperationViewModel &op);

  winrt::fire_and_forget Run();
  void ShowExtractOptions();
  winrt::fire_and_forget ContinueExtract();
  winrt::Windows::Foundation::IAsyncOperation<bool> ExtractOne(UString archive, CDestinationFunc destination,
      bool useTemplate, CExtractOptions options, int openFolder);
  winrt::Windows::Foundation::IAsyncOperation<bool> TestOne(UString archive);
  winrt::Windows::Foundation::IAsyncAction Compress(UStringVector sources, UString format, bool share);
  winrt::fire_and_forget Finish();

  void OnPrimaryClick();
  void OnSecondaryClick();
  void LaunchMain(const char *verb, const UStringVector &paths);

  Z7_CLASS_NO_COPY(COperationWindow)
public:
  Z7_IFACE_IMP(IWindowHost)

  COperationWindow(const CLaunchRequest &request);
  winrt::Microsoft::UI::Xaml::Window GetWindow() const { return _window; }
};

#endif
