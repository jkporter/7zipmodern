// App.cpp

#include "StdAfx.h"

#include "../../Common/MyWindows.h"
#include "../../Common/MyInitGuid.h"

// this file defines the 7-Zip interface GUIDs used by Core
#include "../../7zip/Archive/IArchive.h"
#include "../../7zip/ICoder.h"
#include "../../7zip/IPassword.h"

#include "App.h"

#include "../../Windows/Synchronization.h"

#include "../Xaml.h"
#include "../Control/Converters.h"
#include "../Control/SettingsCard.h"

#include "MainWindow.h"
#include "OperationWindow.h"
#include "PlatformServices.h"
#include "ShellServices.h"
#include "ThemeHelper.h"
#include "resource.h"

// Application.h must be the last include (see the comment there),
// and this file doesn't use "using namespace winrt".
#include "../Application.h"

HINSTANCE g_hInstance = NULL;
CApp g_App;

/*
Interactive requests (open, compress, hash...) are funnelled into one main
window with tabs; quick shell verbs (extract here, test, add to .zip...) run
in their own process with a compact progress window, so several can run side
by side like the classic 7zG.
*/

static const wchar_t * const kMainInstanceKey = L"7-Zip.Main";


void CApp::OnLaunched(const CLaunchRequest &initial)
{
  Dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
  NNotifications::Register([](const winrt::Windows::Foundation::Collections::IMap<winrt::hstring, winrt::hstring> &arguments)
  {
    CLaunchRequest r;
    CLaunchRequest::FromToast(arguments, r);
    g_App.Dispatcher.TryEnqueue([r] { g_App.Handle(r); });
  });
  _notificationsRegistered = true;
  NTempFiles::CleanUp();
  NJumpList::UpdateAsync();
  Handle(initial);
}


void CApp::Handle(const CLaunchRequest &request)
{
  if (request.Verb == NLaunchVerb::kReveal)
  {
    if (!request.Paths.IsEmpty())
      NShellActions::RevealInExplorer(request.Paths);
    if (_windows.empty())
      Exit();
    return;
  }

  if (request.RunsInOwnWindow())
  {
    // the window object lives until the process exits (coroutines may still reference it)
    COperationWindow *window = new COperationWindow(request);
    TrackWindow(window->GetWindow());
    window->GetWindow().Activate();
    return;
  }

  if (!MainWindow)
  {
    MainWindow = new CMainWindow();
    TrackWindow(MainWindow->GetWindow());
  }
  MainWindow->Handle(request);
  MainWindow->BringToFront();
}


void CApp::TrackWindow(const winrt::Microsoft::UI::Xaml::Window &window)
{
  CWindowItem item;
  item.Window = window;
  NThemeHelper::Apply(window);
  item.ClosedToken = window.Closed([window](const winrt::Windows::Foundation::IInspectable &,
      const winrt::Microsoft::UI::Xaml::WindowEventArgs &)
  {
    std::vector<CWindowItem> &list = g_App._windows;
    for (size_t i = 0; i < list.size(); i++)
      if (list[i].Window == window)
      {
        list[i].Window.Closed(list[i].ClosedToken);
        list.erase(list.begin() + (ptrdiff_t)i);
        break;
      }
    if (list.empty())
    {
      if (g_App._notificationsRegistered)
      {
        g_App._notificationsRegistered = false;
        NNotifications::Unregister();
      }
      NTempFiles::CleanUp();
    }
  });
  _windows.push_back(item);
}


void CApp::ApplyThemeToAll()
{
  for (size_t i = 0; i < _windows.size(); i++)
    NThemeHelper::Apply(_windows[i].Window);
}


void CApp::Exit()
{
  winrt::Microsoft::UI::Xaml::Application::Current().Exit();
}


// The XAML Application: loads the shared resources, then hands over to g_App.

class CXamlApp: public NWindows11::CApplicationT<CXamlApp>
{
  CLaunchRequest _initial;
public:
  CXamlApp(const CLaunchRequest &initial): _initial(initial)
  {
    UnhandledException([](const winrt::Windows::Foundation::IInspectable &,
        const winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs &e)
    {
      ::OutputDebugStringW(e.Message().c_str());
      e.Handled(true);
    });
  }

  void OnLaunched(const winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs &)
  {
    AddControlsResources();
    NWindows11::NControl::AddConverters(Resources());
    NWindows11::NControl::AddSettingsCardStyles(Resources());
    Resources().MergedDictionaries().Append(
        NWindows11::NXaml::Load<winrt::Microsoft::UI::Xaml::ResourceDictionary>(IDX_APP_RESOURCES));
    g_App.OnLaunched(_initial);
  }
};


// Hands the activation to the running main instance (must not block on the STA thread itself).
static void RedirectTo(const winrt::Microsoft::Windows::AppLifecycle::AppInstance &target,
    const winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments &args)
{
  ::AllowSetForegroundWindow(target.ProcessId());
  NWindows::NSynchronization::CManualResetEvent done;
  if (done.Create() != 0)
    return;
  [](winrt::Microsoft::Windows::AppLifecycle::AppInstance t,
      winrt::Microsoft::Windows::AppLifecycle::AppActivationArguments a, HANDLE ev) -> winrt::fire_and_forget
  {
    co_await winrt::resume_background();
    try
    {
      co_await t.RedirectActivationToAsync(a);
    }
    catch (...) {}
    ::SetEvent(ev);
  }(target, args, (HANDLE)done);
  ::WaitForSingleObject(done, 10000);
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
  g_hInstance = hInstance;
  NWindows11::CWindowsAppRuntime runtime;
  if (FAILED(runtime.Initialize()))
    return 1;
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  g_App.Settings.Load();

  using namespace winrt::Microsoft::Windows::AppLifecycle;
  CLaunchRequest request;
  AppActivationArguments activation { nullptr };
  try
  {
    activation = AppInstance::GetCurrent().GetActivatedEventArgs();
    CLaunchRequest::FromActivation(activation, request);
  }
  catch (...)
  {
    request = CLaunchRequest();
  }

  if (!request.RunsInOwnWindow() && activation)
  {
    try
    {
      AppInstance main = AppInstance::FindOrRegisterForKey(kMainInstanceKey);
      if (!main.IsCurrent())
      {
        RedirectTo(main, activation);
        return 0;
      }
      main.Activated([](const winrt::Windows::Foundation::IInspectable &, const AppActivationArguments &args)
      {
        // a later launch (any thread)
        CLaunchRequest r;
        CLaunchRequest::FromActivation(args, r);
        if (g_App.Dispatcher)
          g_App.Dispatcher.TryEnqueue([r] { g_App.Handle(r); });
      });
    }
    catch (...) {}
  }

  winrt::Microsoft::UI::Xaml::Application::Start([request](auto &&)
  {
    winrt::make<CXamlApp>(request);
  });
  return 0;
}
