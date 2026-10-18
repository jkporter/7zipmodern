using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using SevenZip.App.Activation;
using SevenZip.App.Services;
using SevenZip.App.Views;

namespace SevenZip.App;

public partial class App : Application
{
    private readonly LaunchRequest _initial;
    private readonly List<Window> _windows = [];

    public App(LaunchRequest initial)
    {
        _initial = initial;
        Settings = new SettingsService();
        InitializeComponent();
        UnhandledException += (_, e) =>
        {
            System.Diagnostics.Debug.WriteLine(e.Exception);
            e.Handled = true;
        };
    }

    public static new App Current => (App)Application.Current;
    public SettingsService Settings { get; }
    public MainWindow? MainWindow { get; private set; }
    public DispatcherQueue Dispatcher { get; private set; } = null!;

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Dispatcher = DispatcherQueue.GetForCurrentThread();
        Notifications.Register(a => Dispatcher.TryEnqueue(() => Handle(LaunchRequest.FromToast(a))));
        TempFiles.CleanUp();
        _ = JumpListService.UpdateAsync();
        Handle(_initial);
    }

    /// <summary>Entry point for every activation, including ones redirected from later launches.</summary>
    public void Handle(LaunchRequest request)
    {
        if (request.Verb == LaunchVerb.Reveal)
        {
            if (request.Paths.Count > 0)
                ShellActions.RevealInExplorer(request.Paths);
            if (_windows.Count == 0)
                Exit();
            return;
        }

        if (request.RunsInOwnWindow)
        {
            var window = new OperationWindow(request);
            Track(window);
            window.Activate();
            return;
        }

        if (MainWindow is null)
        {
            MainWindow = new MainWindow();
            Track(MainWindow);
            MainWindow.Closed += (_, _) => MainWindow = null;
        }
        MainWindow.Handle(request);
        MainWindow.BringToFront();
    }

    private void Track(Window window)
    {
        _windows.Add(window);
        ThemeHelper.Apply(window);
        window.Closed += (_, _) =>
        {
            _windows.Remove(window);
            if (_windows.Count == 0)
            {
                Notifications.Unregister();
                TempFiles.CleanUp();
            }
        };
    }

    /// <summary>Re-applies theme/backdrop to every window after a settings change.</summary>
    public void ApplyThemeToAll()
    {
        foreach (var w in _windows)
            ThemeHelper.Apply(w);
    }
}
